#include <IOKit/serial/ioss.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"
#include "dmx.h"

// DMX addresses for each spot (3 channels per spot)
const uint8_t spotChannels[DMX_NUM_SPOTS] = {10, 20, 30, 40, 50};

volatile sig_atomic_t keepRunning = 1;
int fd;

void intHandler(int dummy) {
  (void)dummy;
  keepRunning = 0;
}

void computeAverageColorPerZone(const uint8_t *buffer_R,
                                const uint8_t *buffer_G,
                                const uint8_t *buffer_B, size_t numPixels,
                                DMXSpot spots[DMX_NUM_SPOTS]) {
  size_t zoneSize = numPixels / DMX_NUM_SPOTS;
  static int initialized[DMX_NUM_SPOTS] = {0};
  static double smoothR[DMX_NUM_SPOTS] = {0.0};
  static double smoothG[DMX_NUM_SPOTS] = {0.0};
  static double smoothB[DMX_NUM_SPOTS] = {0.0};
  static double smoothW[DMX_NUM_SPOTS] = {
      0.0}; // White channel (unused in DMX output)
  double alpha = DMX_SMOOTHING_FACTOR;

  // Correction factors for RGB (à ajuster si besoin)
  double redFactor = DMX_RED_FACTOR;
  double greenFactor = DMX_GREEN_FACTOR;
  double blueFactor = DMX_BLUE_FACTOR;

  for (size_t i = 0; i < DMX_NUM_SPOTS; i++) {
    size_t start = i * zoneSize;
    size_t end = (i == DMX_NUM_SPOTS - 1) ? numPixels : start + zoneSize;
    unsigned long sumR = 0, sumG = 0, sumB = 0;
    size_t count = end - start;

    for (size_t j = start; j < end; j++) {
      sumR += buffer_R[j];
      sumG += buffer_G[j];
      sumB += buffer_B[j];
    }

    double avgR = (double)sumR / count;
    double avgG = (double)sumG / count;
    double avgB = (double)sumB / count;

    // Calcul de la luminance de l'image
    double Y_image = 0.299 * avgR + 0.587 * avgG + 0.114 * avgB;

    // Inversion de la luminance
    double Y_son = 255.0 - Y_image;

    // Normalisation de l'intensité
    double I_spots = Y_son / 255.0;

    // Application de la correction gamma (gamma = DMX_GAMMA)
    double gamma = DMX_GAMMA;
    double I_spots_corr = pow(I_spots, gamma);
    double finalR = avgR * I_spots_corr;
    double finalG = avgG * I_spots_corr;
    double finalB = avgB * I_spots_corr;

    // Calcul du canal blanc (moyenne des valeurs RGB)
    double finalW = (finalR + finalG + finalB) / 3.0;

    // Lissage temporel (exponential moving average)
    if (!initialized[i]) {
      smoothR[i] = finalR;
      smoothG[i] = finalG;
      smoothB[i] = finalB;
      smoothW[i] = finalW;
      initialized[i] = 1;
    } else {
      smoothR[i] = alpha * smoothR[i] + (1.0 - alpha) * finalR;
      smoothG[i] = alpha * smoothG[i] + (1.0 - alpha) * finalG;
      smoothB[i] = alpha * smoothB[i] + (1.0 - alpha) * finalB;
      smoothW[i] = alpha * smoothW[i] + (1.0 - alpha) * finalW;
    }

    // En mode 3 canaux, seuls les canaux RGB sont utilisés.
    spots[i].red = (uint8_t)smoothR[i];
    spots[i].green = (uint8_t)smoothG[i];
    spots[i].blue = (uint8_t)smoothB[i];
    spots[i].white = (uint8_t)smoothW[i]; // Conserve la valeur au cas où

    // Application de la correction du profil couleur sur RGB
    applyColorProfile(&spots[i].red, &spots[i].green, &spots[i].blue, redFactor,
                      greenFactor, blueFactor);
  }
}

void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue,
                       double redFactor, double greenFactor,
                       double blueFactor) {
  double newRed = (*red) * redFactor;
  double newGreen = (*green) * greenFactor;
  double newBlue = (*blue) * blueFactor;

  // Clamp values to 255.
  if (newRed > 255.0) {
    newRed = 255.0;
  }
  if (newGreen > 255.0) {
    newGreen = 255.0;
  }
  if (newBlue > 255.0) {
    newBlue = 255.0;
  }

  *red = (uint8_t)newRed;
  *green = (uint8_t)newGreen;
  *blue = (uint8_t)newBlue;
}

int send_dmx_frame(int fd, unsigned char *frame, size_t len) {
  // Set break condition (100 µs) then clear and wait for 12 µs (Mark After
  // Break)
  if (ioctl(fd, TIOCSBRK) < 0) {
    perror("Error setting break condition");
    return -1;
  }
  usleep(100); // 100 µs break

  if (ioctl(fd, TIOCCBRK) < 0) {
    perror("Error clearing break condition");
    return -1;
  }
  usleep(12); // 12 µs Mark After Break

  ssize_t written = write(fd, frame, len);
  if (written < 0) {
    perror("Error writing frame");
    return -1;
  }
  if (tcdrain(fd) < 0) {
    perror("Error draining output");
    return -1;
  }

  return 0;
}

int init_Dmx(const char *port, int silent) {
  int fd;
  struct termios tty;

  // Handle Ctrl+C
  signal(SIGINT, intHandler);

  // Open serial port
  fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (!silent)
      perror("Error opening serial port");
    return -1;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    if (!silent)
      perror("Error getting flags");
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
    if (!silent)
      perror("Error setting flags");
    close(fd);
    return -1;
  }

  if (tcgetattr(fd, &tty) != 0) {
    if (!silent)
      perror("Error from tcgetattr");
    close(fd);
    return -1;
  }

  cfmakeraw(&tty);

  tty.c_cflag &= ~PARENB; // No parity
  tty.c_cflag &= ~CSTOPB; // 1 stop bit by default
  tty.c_cflag |= CSTOPB;  // Activate 2 stop bits
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8; // 8 data bits

  tty.c_cflag |= CLOCAL; // Ignore modem control lines
  tty.c_cflag |= CREAD;  // Enable receiver

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10; // Timeout 1 sec

  if (!silent) {
    printf("Baud rate: %lu\n", cfgetispeed(&tty));
    printf("c_cflag: 0x%lx\n", tty.c_cflag);
    printf("c_iflag: 0x%lx\n", tty.c_iflag);
    printf("c_oflag: 0x%lx\n", tty.c_oflag);
    printf("c_lflag: 0x%lx\n", tty.c_lflag);
  }

  speed_t speed = 9600; // Remplacez par B115200 si nécessaire
  if (ioctl(fd, IOSSIOSPEED, &speed) < 0) {
    if (!silent)
      perror("Error setting custom baud rate");
  } else {
    if (!silent)
      printf("Custom baud rate set successfully!\n");
  }

  if (!silent)
    printf("Baud rate after setting: %lu\n", cfgetispeed(&tty));

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    if (!silent) {
      perror("Error from tcsetattr");
      printf("Errno: %d, %s\n", errno, strerror(errno));
    }
    close(fd);
    return -1;
  }

  int status;
  if (ioctl(fd, TIOCMGET, &status) < 0) {
    if (!silent)
      perror("Error getting modem status");
    close(fd);
    return -1;
  }
  status &= ~(TIOCM_DTR | TIOCM_RTS);
  if (ioctl(fd, TIOCMSET, &status) < 0) {
    if (!silent)
      perror("Error setting modem status");
    close(fd);
    return -1;
  }

  speed = DMX_BAUD;
  if (ioctl(fd, IOSSIOSPEED, &speed) < 0) {
    if (!silent)
      perror("Error setting custom baud rate");
    close(fd);
    return -1;
  }

  if (!silent)
    printf("Serial port opened and configured successfully.\n");
  return fd;
}
