#ifndef CONTEXT_H
#define CONTEXT_H

#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include "audio.h"
#include "config.h"
#include "dmx.h"
#include "doublebuffer.h"

typedef struct
{
    int fd;
    int running;
    int colorUpdated;
    DMXSpot spots[DMX_NUM_SPOTS];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} DMXContext;

typedef struct
{
    sfRenderWindow *window;
    int socket;
    struct sockaddr_in *si_other;
    struct sockaddr_in *si_me;
    AudioData *audioData;
    DoubleBuffer *doubleBuffer;
    DMXContext *dmxCtx;
    volatile int running;   // Ajout du flag de terminaison pour Context
#if ENABLE_IMAGE_TRANSFORM
    bool enableImageTransform;
#endif
} Context;

#endif /* CONTEXT_H */
