#ifndef DOUBLEBUFFER_H
#define DOUBLEBUFFER_H

#include "config.h"
#include <pthread.h>
#include <stdint.h>
#include <time.h>

typedef struct DoubleBuffer {
  uint8_t *activeBuffer_R;
  uint8_t *activeBuffer_G;
  uint8_t *activeBuffer_B;
  uint8_t *processingBuffer_R;
  uint8_t *processingBuffer_G;
  uint8_t *processingBuffer_B;

  // Persistent image buffer for audio continuity
  uint8_t *lastValidImage_R;
  uint8_t *lastValidImage_G;
  uint8_t *lastValidImage_B;
  uint8_t lastValidImageExists; // Flag to indicate if we have a valid image

  uint8_t dataReady;
  pthread_mutex_t mutex;
  pthread_cond_t cond;

  // Statistics for monitoring
  uint64_t udp_frames_received;
  uint64_t audio_frames_processed;
  time_t last_udp_frame_time;
} DoubleBuffer;

// Function prototypes
void initDoubleBuffer(DoubleBuffer *db);
void cleanupDoubleBuffer(DoubleBuffer *db);
void swapBuffers(DoubleBuffer *db); // swapBuffers is also in multithreading.c

// New functions for persistent image management
void updateLastValidImage(DoubleBuffer *db);
void getLastValidImageForAudio(DoubleBuffer *db, uint8_t *out_R, uint8_t *out_G,
                               uint8_t *out_B);
int hasValidImageForAudio(DoubleBuffer *db);

#endif /* DOUBLEBUFFER_H */
