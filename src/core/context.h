#ifndef CONTEXT_H
#define CONTEXT_H

#include "audio_c_api.h"
#include "config.h"
#include "dmx.h"
#include "doublebuffer.h"

#ifdef __LINUX__
// Vérifier si SFML est désactivé
#ifdef NO_SFML
// Déclarations simplifiées pour compilation sans SFML
// Utiliser la forme struct pour permettre les pointeurs opaques
typedef struct sfRenderWindow sfRenderWindow;
typedef struct sfEvent sfEvent;
// Ajoutez d'autres types si nécessaire, ex:
// typedef struct sfTexture sfTexture;
#else
// SFML disponible sur Linux
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif // NO_SFML
#else  // Pas __LINUX__ (par exemple macOS)
// Sur les autres plateformes (comme macOS), vérifier si SFML est désactivé
#ifdef NO_SFML
// Déclarations simplifiées pour compilation sans SFML
// Utiliser la forme struct pour permettre les pointeurs opaques
typedef struct sfRenderWindow sfRenderWindow;
typedef struct sfEvent sfEvent;
// Ajoutez d'autres types si nécessaire
#else
// SFML disponible sur macOS (et NO_SFML n'est pas défini)
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif // NO_SFML
#endif // __LINUX__

typedef struct {
  int fd;
  int running;
  int colorUpdated;
  DMXSpot spots[DMX_NUM_SPOTS];
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} DMXContext;

typedef struct {
  sfRenderWindow *window;
  int socket;
  struct sockaddr_in *si_other;
  struct sockaddr_in *si_me;
  AudioData *audioData;
  DoubleBuffer *doubleBuffer;
  DMXContext *dmxCtx;
  volatile int running; // Ajout du flag de terminaison pour Context
#if ENABLE_IMAGE_TRANSFORM
  bool enableImageTransform;
#endif
} Context;

#endif /* CONTEXT_H */
