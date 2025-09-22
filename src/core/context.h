#ifndef CONTEXT_H
#define CONTEXT_H

#include "audio_c_api.h"
#include "audio_image_buffers.h"
#include "config.h"
#include "dmx.h"
#include "doublebuffer.h"
#include <pthread.h>
#include <time.h>
#include <stdint.h>

/* Synthesis mode enums (moved from shared.h) */
typedef enum {
    IFFT_MODE = 0,
    DWAVE_MODE,
    MENU_MODE
} synthModeTypeDef;

typedef enum {
    CV_ON = 0,
    KEYBOARD_ON
} synthCVTypeDef;

typedef enum {
    NORMAL_READ = 0,
    NORMAL_REVERSE_READ,
    DUAL_READ
} synthReadModeTypeDef;

/* Synthesis parameters structure (moved from shared.h) */
struct params {
    int32_t start_frequency;
    int32_t comma_per_semitone;
    int32_t ifft_attack;
    int32_t ifft_release;
    int32_t volume;
};

/* Shared synthesis variables structure (moved from shared.h) */
struct shared_var {
    synthModeTypeDef mode;
    synthCVTypeDef CV_or_Keyboard;
    synthReadModeTypeDef directRead_Mode;
    int32_t synth_process_cnt;
};

/* Global synthesis variables (moved from shared.h) */
extern struct shared_var shared_var;
extern volatile struct params params;
extern volatile int32_t cvData[];
extern volatile int32_t audioBuff[];

extern int params_size;

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
  // New flexible system - replace static array with dynamic pointer
  DMXSpot *spots;          // Dynamic array of spots
  int num_spots;           // Number of spots allocated
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  // libftdi support for Linux
  int use_libftdi;    // 0 = traditional fd, 1 = libftdi
#ifdef __linux__
  struct ftdi_context *ftdi; // libftdi context (Linux primary)
#endif
} DMXContext;

// Global DMX context instance
extern DMXContext dmx_ctx;

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
