#ifndef DMX_H
#define DMX_H

#include "config.h"
#include <signal.h>  // for sig_atomic_t
#include <stdbool.h> // for bool type
#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t

// Structure pour représenter un blob (groupe de pixels similaires)
typedef struct {
  uint16_t startIdx;        // Index de début du blob dans la zone
  uint16_t count;           // Nombre de pixels dans le blob
  uint8_t avgR, avgG, avgB; // Couleur moyenne du blob
  double significance;      // Significativité moyenne du blob
} Blob;

typedef struct {
  // uint8_t channel;  // Start
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  // uint8_t dimmer;
  uint8_t white;
  // uint8_t mode;
  // uint8_t strobo;
} DMXSpot;

extern const uint8_t spotChannels[DMX_NUM_SPOTS];
extern volatile sig_atomic_t keepRunning;

// Function prototypes
void intHandler(int dummy);

// Fonctions pour la détection des blobs
bool isSignificant(uint8_t r, uint8_t g, uint8_t b);
bool isColorSimilar(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2,
                    uint8_t b2);
void growBlob(const uint8_t *buffer_R, const uint8_t *buffer_G,
              const uint8_t *buffer_B, bool *processed, Blob *blob,
              size_t start, size_t end);
int detectBlobs(const uint8_t *buffer_R, const uint8_t *buffer_G,
                const uint8_t *buffer_B, size_t start, size_t end, Blob *blobs,
                double *pixelSignificance);

// Fonctions de traitement DMX
void computeAverageColorPerZone(const uint8_t *buffer_R,
                                const uint8_t *buffer_G,
                                const uint8_t *buffer_B, size_t numPixels,
                                DMXSpot spots[DMX_NUM_SPOTS]);
void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue,
                       double redFactor, double greenFactor, double blueFactor);
int send_dmx_frame(int fd, unsigned char *frame, size_t len);
int init_Dmx(const char *port, int silent);

#endif // DMX_H
