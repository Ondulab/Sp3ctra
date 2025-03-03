#ifndef CONTEXT_H
#define CONTEXT_H

#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include "audio.h"

typedef struct
{
    int fd;
    volatile int running;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint8_t avgR;
    uint8_t avgG;
    uint8_t avgB;
    volatile int colorUpdated;
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
    // Ajouter d'autres champs selon les besoins
} Context;

#endif /* CONTEXT_H */
