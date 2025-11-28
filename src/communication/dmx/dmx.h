#ifndef DMX_H
#define DMX_H

#include "config.h"
#include <signal.h>  // for sig_atomic_t
#include <stdbool.h> // for bool type
#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t

#ifdef __linux__
#include <ftdi.h>
#endif

// Structure representing a blob (group of similar pixels)
typedef struct {
  uint16_t startIdx;        // Start index of the blob in the zone
  uint16_t count;           // Number of pixels in the blob
  uint8_t avgR, avgG, avgB; // Average color of the blob
  double significance;      // Average significance of the blob
} Blob;

// Structure for RGB spots (3 channels)
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} DMXSpotRGB;

// Structure for RGBW spots (4 channels) - for future extension
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
} DMXSpotRGBW;

// Union to support different spot types (extensible)
typedef union {
    DMXSpotRGB rgb;
    DMXSpotRGBW rgbw;  // For future use
} DMXSpotData;

// Main spot structure with type and start channel
typedef struct {
    DMXSpotType type;
    uint16_t start_channel;  // DMX start channel for this spot (supports 0-65535)
    DMXSpotData data;
} DMXSpot;

// Compatibility structure with legacy system (deprecated)
typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
} DMXSpotLegacy;

extern volatile sig_atomic_t keepRunning;

// Function prototypes
void intHandler(int dummy);

// Functions for blob detection
bool isSignificant(uint8_t r, uint8_t g, uint8_t b);
bool isColorSimilar(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2,
                    uint8_t b2);
void growBlob(const uint8_t *buffer_R, const uint8_t *buffer_G,
              const uint8_t *buffer_B, bool *processed, Blob *blob,
              size_t start, size_t end);
int detectBlobs(const uint8_t *buffer_R, const uint8_t *buffer_G,
                const uint8_t *buffer_B, size_t start, size_t end, Blob *blobs,
                double *pixelSignificance);

// New flexible initialization functions
int dmx_init_configuration(int num_spots, DMXSpotType spot_type, int start_channel);
void dmx_generate_channel_mapping(DMXSpot spots[], int num_spots, DMXSpotType spot_type, int start_channel);

// DMX processing functions
void computeAverageColorPerZone(const uint8_t *buffer_R,
                                const uint8_t *buffer_G,
                                const uint8_t *buffer_B, size_t numPixels,
                                DMXSpot spots[], int num_spots);
void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue,
                       double redFactor, double greenFactor, double blueFactor);
int send_dmx_frame(int fd, unsigned char *frame, size_t len);
int init_Dmx(const char *port, int silent);

#ifdef __linux__
// Linux-specific baud rate configuration functions  
int set_custom_baudrate_termios2(int fd, int baud, int silent);
int set_custom_baudrate_ftdi(int fd, int baud, int silent);

// libftdi-specific functions
int init_dmx_linux_libftdi(int silent);
int send_dmx_break_libftdi(struct ftdi_context *ftdi);
int send_dmx_frame_libftdi(struct ftdi_context *ftdi, unsigned char *frame, size_t len);
void cleanup_dmx_libftdi(void);
#endif

// Platform-specific initialization functions
#ifdef __APPLE__
int init_dmx_macos(const char *port, int silent);
#else
int init_dmx_linux_standard(const char *port, int silent);
#endif

#endif // DMX_H
