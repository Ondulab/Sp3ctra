#ifndef DMX_H
#define DMX_H

#include <stddef.h>    // for size_t

typedef struct
{
    //uint8_t channel;  // Start
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    //uint8_t dimmer;
    uint8_t white;
    //uint8_t mode;
    //uint8_t strobo;
} DMXSpot;

extern const uint8_t spotChannels[DMX_NUM_SPOTS];
extern volatile sig_atomic_t keepRunning;

// Function prototype for sending a DMX frame-
void intHandler(int dummy);
void computeAverageColorPerZone(const uint8_t *buffer_R, const uint8_t *buffer_G, const uint8_t *buffer_B, size_t numPixels, DMXSpot spots[DMX_NUM_SPOTS]);
void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue, double redFactor, double greenFactor, double blueFactor);
int send_dmx_frame(int fd, unsigned char *frame, size_t len);
int init_Dmx(void);

#endif // DMX_H
