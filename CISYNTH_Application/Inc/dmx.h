#ifndef DMX_H
#define DMX_H

#include <stddef.h>    // for size_t

// DMX configuration
#define DMX_PORT "/dev/tty.usbserial-AD0JUL0N"
#define DMX_BAUD 250000
#define DMX_CHANNELS 512
#define FRAME_SIZE (DMX_CHANNELS + 1)

// Function prototype for sending a DMX frame
int init_Dmx(void);
void computeAverageColor(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorGamma(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorWeighted(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorSegmented(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);

int send_dmx_frame(int fd, unsigned char *frame, size_t len);

#endif // DMX_H
