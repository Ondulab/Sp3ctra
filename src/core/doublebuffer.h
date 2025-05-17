#ifndef DOUBLEBUFFER_H
#define DOUBLEBUFFER_H

#include <pthread.h>
#include <stdint.h>

typedef struct DoubleBuffer {
  uint8_t *activeBuffer_R;
  uint8_t *activeBuffer_G;
  uint8_t *activeBuffer_B;
  uint8_t *processingBuffer_R;
  uint8_t *processingBuffer_G;
  uint8_t *processingBuffer_B;
  uint8_t dataReady;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} DoubleBuffer;

// Function prototypes
void initDoubleBuffer(DoubleBuffer *db);
void cleanupDoubleBuffer(DoubleBuffer *db);
void swapBuffers(DoubleBuffer *db); // swapBuffers is also in multithreading.c

#endif /* DOUBLEBUFFER_H */
