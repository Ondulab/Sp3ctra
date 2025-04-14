#ifndef DOUBLEBUFFER_H
#define DOUBLEBUFFER_H

#include <stdint.h>
#include <pthread.h>

typedef struct DoubleBuffer
{
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

#endif /* DOUBLEBUFFER_H */
