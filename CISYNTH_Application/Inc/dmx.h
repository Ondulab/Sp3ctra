#ifndef DMX_H
#define DMX_H

#include <stddef.h>    // for size_t

// Function prototype for sending a DMX frame-
void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue, double redFactor, double greenFactor, double blueFactor);
void computeAverageColor(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorGamma(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorWeighted(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
void computeAverageColorSegmented(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, uint8_t *avgR, uint8_t *avgG, uint8_t *avgB);
int send_dmx_frame(int fd, unsigned char *frame, size_t len);
int init_Dmx(void);

#endif // DMX_H
