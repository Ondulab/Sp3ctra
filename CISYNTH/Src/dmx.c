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

#include "config.h"
#include "dmx.h"

const uint8_t spotChannels[NUM_SPOTS] = {10, 20, 30, 40, 50, 60, 70};

volatile sig_atomic_t keepRunning = 1;
int fd;

void intHandler(int dummy)
{
    (void)dummy;
    keepRunning = 0;
}

void computeAverageColorPerZone(const uint8_t *buffer_R,
                                const uint8_t *buffer_G,
                                const uint8_t *buffer_B,
                                size_t numPixels,
                                DMXSpot spots[NUM_SPOTS])
{
    size_t zoneSize = numPixels / NUM_SPOTS;
    static int initialized[NUM_SPOTS] = {0};
    static double smoothR[NUM_SPOTS] = {0.0};
    static double smoothG[NUM_SPOTS] = {0.0};
    static double smoothB[NUM_SPOTS] = {0.0};
    static double smoothW[NUM_SPOTS] = {0.0};  // Pour le canal blanc
    double alpha = 0.95; // Facteur de lissage

    // Facteurs de correction du profil couleur pour RGB (à ajuster si besoin)
    double redFactor   = RED_FACTOR;
    double greenFactor = 0.3;
    double blueFactor  = 0.1;

    for (size_t i = 0; i < NUM_SPOTS; i++)
    {
        size_t start = i * zoneSize;
        size_t end = (i == NUM_SPOTS - 1) ? numPixels : start + zoneSize;
        unsigned long sumR = 0, sumG = 0, sumB = 0;
        size_t count = end - start;

        for (size_t j = start; j < end; j++)
        {
            sumR += buffer_R[j];
            sumG += buffer_G[j];
            sumB += buffer_B[j];
        }

        double avgR = (double)sumR / count;
        double avgG = (double)sumG / count;
        double avgB = (double)sumB / count;

        // 1. Calcul de la luminance de l'image
        double Y_image = 0.299 * avgR + 0.587 * avgG + 0.114 * avgB;

        // 2. Inversion de la luminance
        double Y_son = 255.0 - Y_image;

        // 3. Normalisation de l'intensité
        double I_spots = Y_son / 255.0;

        // 4 & 5. Application de l'intensité aux couleurs d'origine avec correction gamma (gamma = 4.0)
        double gamma = 2.0;
        double I_spots_corr = pow(I_spots, gamma);
        double finalR = avgR * I_spots_corr;
        double finalG = avgG * I_spots_corr;
        double finalB = avgB * I_spots_corr;

        // Calcul du canal blanc, ici défini comme la moyenne des valeurs finales RGB
        double finalW = (finalR + finalG + finalB) / 3.0;

        // Lissage temporel (moyenne mobile exponentielle) pour éviter les variations brusques
        if (!initialized[i])
        {
            smoothR[i] = finalR;
            smoothG[i] = finalG;
            smoothB[i] = finalB;
            smoothW[i] = finalW;
            initialized[i] = 1;
        }
        else
        {
            smoothR[i] = alpha * smoothR[i] + (1.0 - alpha) * finalR;
            smoothG[i] = alpha * smoothG[i] + (1.0 - alpha) * finalG;
            smoothB[i] = alpha * smoothB[i] + (1.0 - alpha) * finalB;
            smoothW[i] = alpha * smoothW[i] + (1.0 - alpha) * finalW;
        }

        spots[i].dimmer  = 255; // Intensité maximale par défaut
        spots[i].red     = (uint8_t)smoothR[i];
        spots[i].green   = (uint8_t)smoothG[i];
        spots[i].blue    = (uint8_t)smoothB[i];
        spots[i].white   = (uint8_t)smoothW[i];  // Affectation de la valeur du canal blanc
        spots[i].mode    = 0;
        spots[i].strobo  = 0;
        spots[i].channel = spotChannels[i];

        // Application de la correction du profil couleur aux canaux RGB
        applyColorProfile(&spots[i].red, &spots[i].green, &spots[i].blue,
                          redFactor, greenFactor, blueFactor);
    }
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
