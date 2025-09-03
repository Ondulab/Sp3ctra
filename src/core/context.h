#ifndef CONTEXT_H
#define CONTEXT_H

#include "audio_c_api.h"
#include "audio_image_buffers.h"
#include "config.h"
#include "dmx.h"
#include "doublebuffer.h"
#include <pthread.h>
#include <time.h>

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
  DoubleBuffer *doubleBuffer;           // Legacy double buffer (for display)
  AudioImageBuffers *audioImageBuffers; // New dual buffer system for audio
  DMXContext *dmxCtx;
  volatile int running; // Ajout du flag de terminaison pour Context

  /* IMU + Auto-volume state (protected by imu_mutex) */
  pthread_mutex_t imu_mutex; /* Protects IMU and auto-volume fields */
  float imu_x_filtered;      /* Low-pass filtered accelerometer X */
  time_t last_imu_time;      /* Last IMU packet arrival time (seconds) */
  int imu_has_value;         /* 0/1: initial IMU value set */

  /* Auto-volume state (mirror of AutoVolume for observability) */
  float auto_volume_current;      /* Current applied master volume (0..1) */
  float auto_volume_target;       /* Target volume computed from IMU */
  time_t auto_last_activity_time; /* Last time activity detected */
  int auto_is_active;             /* 0/1 */
} Context;

#endif /* CONTEXT_H */
