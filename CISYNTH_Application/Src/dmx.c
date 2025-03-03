#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <IOKit/serial/ioss.h>

#include "dmx.h"

#define DMX_PORT "/dev/tty.usbserial-AD0JUL0N"
#define DMX_BAUD 250000
#define DMX_CHANNELS 512
#define FRAME_SIZE (DMX_CHANNELS + 1)

volatile sig_atomic_t keepRunning = 1;
int fd;

void intHandler(int dummy)
{
    (void)dummy;
    keepRunning = 0;
}

// Function to apply color profile correction to RGB values using multiplicative factors
void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue, double redFactor, double greenFactor, double blueFactor)
{
    double newRed = (*red) * redFactor;
    double newGreen = (*green) * greenFactor;
    double newBlue = (*blue) * blueFactor;

    // Clamp the values to 255
    if (newRed > 255.0)
    {
        newRed = 255.0;
    }
    if (newGreen > 255.0)
    {
        newGreen = 255.0;
    }
    if (newBlue > 255.0)
    {
        newBlue = 255.0;
    }

    *red = (uint8_t)newRed;
    *green = (uint8_t)newGreen;
    *blue = (uint8_t)newBlue;
}

void computeAverageColor(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB)
{
    unsigned long sumR = 0;
    unsigned long sumG = 0;
    unsigned long sumB = 0;

    for (size_t i = 0; i < numPixels; i++)
    {
        sumR += buffer_R[i];
        sumG += buffer_G[i];
        sumB += buffer_B[i];
    }

    *avgR = (uint8_t)(sumR / numPixels);
    *avgG = (uint8_t)(sumG / numPixels);
    *avgB = (uint8_t)(sumB / numPixels);
}

void computeAverageColorGamma(const uint8_t *buffer_R,
                              const uint8_t *buffer_G,
                              const uint8_t *buffer_B,
                              size_t numPixels,
                              uint8_t *avgR,
                              uint8_t *avgG,
                              uint8_t *avgB)
{
    unsigned long sumR = 0;
    unsigned long sumG = 0;
    unsigned long sumB = 0;

    for (size_t i = 0; i < numPixels; i++)
    {
        sumR += buffer_R[i];
        sumG += buffer_G[i];
        sumB += buffer_B[i];
    }

    double gamma = 2.2;
    double averageR = (double)sumR / numPixels;
    double averageG = (double)sumG / numPixels;
    double averageB = (double)sumB / numPixels;

    double normR = averageR / 255.0;
    double normG = averageG / 255.0;
    double normB = averageB / 255.0;

    normR = pow(normR, gamma);
    normG = pow(normG, gamma);
    normB = pow(normB, gamma);

    *avgR = (uint8_t)(normR * 255.0);
    *avgG = (uint8_t)(normG * 255.0);
    *avgB = (uint8_t)(normB * 255.0);
}

void computeAverageColorWeighted(const uint8_t *buffer_R,
                                 const uint8_t *buffer_G,
                                 const uint8_t *buffer_B,
                                 size_t numPixels,
                                 uint8_t *avgR,
                                 uint8_t *avgG,
                                 uint8_t *avgB)
{
    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    double totalWeight = 0.0;
    double center = (double)(numPixels - 1) / 2.0;

    // Weighted sum computation
    for (size_t i = 0; i < numPixels; i++)
    {
        double weight = 1.0 - fabs((double)i - center) / center;
        sumR += buffer_R[i] * weight;
        sumG += buffer_G[i] * weight;
        sumB += buffer_B[i] * weight;
        totalWeight += weight;
    }

    double weightedR = sumR / totalWeight;
    double weightedG = sumG / totalWeight;
    double weightedB = sumB / totalWeight;

    // Gamma correction (fixed gamma = 2.2)
    double gamma = 2.2;
    double normR = weightedR / 255.0;
    double normG = weightedG / 255.0;
    double normB = weightedB / 255.0;
    normR = pow(normR, gamma);
    normG = pow(normG, gamma);
    normB = pow(normB, gamma);

    double blendedR = normR * 255.0;
    double blendedG = normG * 255.0;
    double blendedB = normB * 255.0;

    // Temporal smoothing with exponential moving average
    static int initialized = 0;
    static double smoothR = 0.0;
    static double smoothG = 0.0;
    static double smoothB = 0.0;
    double alpha = 0.8; // smoothing factor : higher alpha => more inertia

    if (!initialized)
    {
        smoothR = blendedR;
        smoothG = blendedG;
        smoothB = blendedB;
        initialized = 1;
    }
    else
    {
        smoothR = alpha * smoothR + (1.0 - alpha) * blendedR;
        smoothG = alpha * smoothG + (1.0 - alpha) * blendedG;
        smoothB = alpha * smoothB + (1.0 - alpha) * blendedB;
    }

    *avgR = (uint8_t)(smoothR);
    *avgG = (uint8_t)(smoothG);
    *avgB = (uint8_t)(smoothB);
}

void computeAverageColorSegmented(const uint8_t *buffer_R,
                                  const uint8_t *buffer_G,
                                  const uint8_t *buffer_B,
                                  size_t numPixels,
                                  uint8_t *avgR,
                                  uint8_t *avgG,
                                  uint8_t *avgB)
{
    const size_t segments = 4;
    size_t segmentSize = numPixels / segments;
    uint32_t segR[segments];
    uint32_t segG[segments];
    uint32_t segB[segments];

    for (size_t i = 0; i < segments; i++)
    {
        segR[i] = 0;
        segG[i] = 0;
        segB[i] = 0;
    }

    uint32_t globalR = 0;
    uint32_t globalG = 0;
    uint32_t globalB = 0;

    // Compute global sums
    for (size_t i = 0; i < numPixels; i++)
    {
        globalR += buffer_R[i];
        globalG += buffer_G[i];
        globalB += buffer_B[i];
    }

    // Compute sums per segment
    for (size_t seg = 0; seg < segments; seg++)
    {
        size_t start = seg * segmentSize;
        size_t end = (seg == segments - 1) ? numPixels : start + segmentSize;

        for (size_t i = start; i < end; i++)
        {
            segR[seg] += buffer_R[i];
            segG[seg] += buffer_G[i];
            segB[seg] += buffer_B[i];
        }
    }

    // Identify the brightest segment
    double maxBrightness = 0.0;
    size_t maxSeg = 0;
    uint8_t segAvgR = 0;
    uint8_t segAvgG = 0;
    uint8_t segAvgB = 0;

    for (size_t seg = 0; seg < segments; seg++)
    {
        size_t count = (seg == segments - 1) ? (numPixels - seg * segmentSize) : segmentSize;
        uint8_t currentAvgR = (uint8_t)(segR[seg] / count);
        uint8_t currentAvgG = (uint8_t)(segG[seg] / count);
        uint8_t currentAvgB = (uint8_t)(segB[seg] / count);

        double brightness = 0.2126 * currentAvgR + 0.7152 * currentAvgG + 0.0722 * currentAvgB;
        if (brightness > maxBrightness)
        {
            maxBrightness = brightness;
            maxSeg = seg;
            segAvgR = currentAvgR;
            segAvgG = currentAvgG;
            segAvgB = currentAvgB;
        }
    }

    // Compute global average
    uint8_t globalAvgR = (uint8_t)(globalR / numPixels);
    uint8_t globalAvgG = (uint8_t)(globalG / numPixels);
    uint8_t globalAvgB = (uint8_t)(globalB / numPixels);

    // Blend between the global average and the brightest segment average
    double blendFactor = 0.5;
    double blendedR = (1.0 - blendFactor) * globalAvgR + blendFactor * segAvgR;
    double blendedG = (1.0 - blendFactor) * globalAvgG + blendFactor * segAvgG;
    double blendedB = (1.0 - blendFactor) * globalAvgB + blendFactor * segAvgB;

    // Apply gamma correction
    double gamma = 2.2;
    double normR = blendedR / 255.0;
    double normG = blendedG / 255.0;
    double normB = blendedB / 255.0;

    normR = pow(normR, gamma);
    normG = pow(normG, gamma);
    normB = pow(normB, gamma);

    *avgR = (uint8_t)(normR * 255.0);
    *avgG = (uint8_t)(normG * 255.0);
    *avgB = (uint8_t)(normB * 255.0);
}

int send_dmx_frame(int fd, unsigned char *frame, size_t len)
{
    // Set break condition (100 µs) then clear and wait for 12 µs (Mark After Break)
    if (ioctl(fd, TIOCSBRK) < 0)
    {
        perror("Error setting break condition");
        return -1;
    }
    usleep(100);  // 100 µs break

    if (ioctl(fd, TIOCCBRK) < 0)
    {
        perror("Error clearing break condition");
        return -1;
    }
    usleep(12);   // 12 µs Mark After Break

    ssize_t written = write(fd, frame, len);
    if (written < 0)
    {
        perror("Error writing frame");
        return -1;
    }
    // Optionnel : attendre que le buffer soit vidé
    if (tcdrain(fd) < 0)
    {
        perror("Error draining output");
        return -1;
    }

    return 0;
}

int init_Dmx(void)
{
    int fd;
    struct termios tty;

    // Handle Ctrl+C interrupt
    signal(SIGINT, intHandler);

    // Open serial port
    fd = open(DMX_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Error opening serial port");
        return -1;
    }

    // Remove non-blocking flag
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("Error getting flags");
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1)
    {
        perror("Error setting flags");
        close(fd);
        return -1;
    }

    // Get current serial port settings
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("Error from tcgetattr");
        close(fd);
        return -1;
    }

    // Set raw mode
    cfmakeraw(&tty);

    // Configure: 8 data bits, no parity, 2 stop bits
    tty.c_cflag &= ~PARENB;      // No parity
    tty.c_cflag &= ~CSTOPB;      // 1 stop bit by default
    tty.c_cflag |= CSTOPB;       // Activate 2 stop bits
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;          // 8 data bits

    tty.c_cflag |= CLOCAL;       // Ignore modem control lines
    tty.c_cflag |= CREAD;        // Enable receiver

    // Set read timeout: VMIN = 0, VTIME = 10 (1 sec)
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    // Apply settings immediately
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("Error from tcsetattr");
        close(fd);
        return -1;
    }

    // Disable DTR and RTS
    int status;
    if (ioctl(fd, TIOCMGET, &status) < 0)
    {
        perror("Error getting modem status");
        close(fd);
        return -1;
    }
    status &= ~(TIOCM_DTR | TIOCM_RTS);
    if (ioctl(fd, TIOCMSET, &status) < 0)
    {
        perror("Error setting modem status");
        close(fd);
        return -1;
    }
    
    // Set custom baud rate
    speed_t speed = DMX_BAUD;
    if (ioctl(fd, IOSSIOSPEED, &speed) < 0)
    {
        perror("Error setting custom baud rate");
        close(fd);
        return -1;
    }

    printf("Serial port opened and configured successfully.\n");
    return fd;
}
