/* multithreading.c */

#include "multithreading.h"
#include "audio_c_api.h"
#include "config.h"
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "synth.h"
#include "udp.h"

#ifndef NO_SFML
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif // NO_SFML

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*------------------------------------------------------------------------------
    Helper Functions
------------------------------------------------------------------------------*/

/*
// Wait for DMX color update using condition variable
static void WaitForDMXColorUpdate(DMXContext *ctx) {
  pthread_mutex_lock(&ctx->mutex);
  while (!ctx->colorUpdated) {
    pthread_cond_wait(&ctx->cond, &ctx->mutex);
  }
  pthread_mutex_unlock(&ctx->mutex);
}
*/

void initDoubleBuffer(DoubleBuffer *db) {
  if (pthread_mutex_init(&db->mutex, NULL) != 0) {
    fprintf(stderr, "Error: Mutex initialization failed\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_cond_init(&db->cond, NULL) != 0) {
    fprintf(stderr, "Error: Condition variable initialization failed\n");
    exit(EXIT_FAILURE);
  }

  db->activeBuffer_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->activeBuffer_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->activeBuffer_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

  db->processingBuffer_R =
      (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->processingBuffer_G =
      (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->processingBuffer_B =
      (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

  // Allocate persistent image buffers for audio continuity
  db->lastValidImage_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->lastValidImage_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
  db->lastValidImage_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

  if (!db->activeBuffer_R || !db->activeBuffer_G || !db->activeBuffer_B ||
      !db->processingBuffer_R || !db->processingBuffer_G ||
      !db->processingBuffer_B || !db->lastValidImage_R ||
      !db->lastValidImage_G || !db->lastValidImage_B) {
    fprintf(stderr, "Error: Allocation of image buffers failed\n");
    exit(EXIT_FAILURE);
  }

  // Initialize persistent image with black (zero)
  memset(db->lastValidImage_R, 0, CIS_MAX_PIXELS_NB);
  memset(db->lastValidImage_G, 0, CIS_MAX_PIXELS_NB);
  memset(db->lastValidImage_B, 0, CIS_MAX_PIXELS_NB);

  db->dataReady = 0;
  db->lastValidImageExists = 0;
  db->udp_frames_received = 0;
  db->audio_frames_processed = 0;
  db->last_udp_frame_time = time(NULL);
}

void cleanupDoubleBuffer(DoubleBuffer *db) {
  if (db) {
    free(db->activeBuffer_R);
    free(db->activeBuffer_G);
    free(db->activeBuffer_B);
    free(db->processingBuffer_R);
    free(db->processingBuffer_G);
    free(db->processingBuffer_B);
    free(db->lastValidImage_R);
    free(db->lastValidImage_G);
    free(db->lastValidImage_B);

    db->activeBuffer_R = NULL;
    db->activeBuffer_G = NULL;
    db->activeBuffer_B = NULL;
    db->processingBuffer_R = NULL;
    db->processingBuffer_G = NULL;
    db->processingBuffer_B = NULL;
    db->lastValidImage_R = NULL;
    db->lastValidImage_G = NULL;
    db->lastValidImage_B = NULL;

    pthread_mutex_destroy(&db->mutex);
    pthread_cond_destroy(&db->cond);
  }
}

void swapBuffers(DoubleBuffer *db) {
  uint8_t *temp = NULL;

  temp = db->activeBuffer_R;
  db->activeBuffer_R = db->processingBuffer_R;
  db->processingBuffer_R = temp;

  temp = db->activeBuffer_G;
  db->activeBuffer_G = db->processingBuffer_G;
  db->processingBuffer_G = temp;

  temp = db->activeBuffer_B;
  db->activeBuffer_B = db->processingBuffer_B;
  db->processingBuffer_B = temp;
}

/**
 * @brief Update the persistent image buffer with latest valid image
 * @param db DoubleBuffer structure
 * @note Mutex must be locked before calling this function
 */
void updateLastValidImage(DoubleBuffer *db) {
  // Copy processing buffer to persistent image buffer
  memcpy(db->lastValidImage_R, db->processingBuffer_R, CIS_MAX_PIXELS_NB);
  memcpy(db->lastValidImage_G, db->processingBuffer_G, CIS_MAX_PIXELS_NB);
  memcpy(db->lastValidImage_B, db->processingBuffer_B, CIS_MAX_PIXELS_NB);

  db->lastValidImageExists = 1;
  db->udp_frames_received++;
  db->last_udp_frame_time = time(NULL);

  // Debug logs removed for production use
}

/**
 * @brief Get the last valid image for audio processing (thread-safe)
 * @param db DoubleBuffer structure
 * @param out_R Output buffer for red channel
 * @param out_G Output buffer for green channel
 * @param out_B Output buffer for blue channel
 */
void getLastValidImageForAudio(DoubleBuffer *db, uint8_t *out_R, uint8_t *out_G,
                               uint8_t *out_B) {
  pthread_mutex_lock(&db->mutex);

  if (db->lastValidImageExists) {
    memcpy(out_R, db->lastValidImage_R, CIS_MAX_PIXELS_NB);
    memcpy(out_G, db->lastValidImage_G, CIS_MAX_PIXELS_NB);
    memcpy(out_B, db->lastValidImage_B, CIS_MAX_PIXELS_NB);
    db->audio_frames_processed++;
  } else {
    // If no valid image exists, use black (silence)
    memset(out_R, 0, CIS_MAX_PIXELS_NB);
    memset(out_G, 0, CIS_MAX_PIXELS_NB);
    memset(out_B, 0, CIS_MAX_PIXELS_NB);
    // Debug log removed for production use
  }

  pthread_mutex_unlock(&db->mutex);
}

/**
 * @brief Check if a valid image exists for audio processing
 * @param db DoubleBuffer structure
 * @return 1 if valid image exists, 0 otherwise
 */
int hasValidImageForAudio(DoubleBuffer *db) {
  pthread_mutex_lock(&db->mutex);
  int exists = db->lastValidImageExists;
  pthread_mutex_unlock(&db->mutex);
  return exists;
}

/*------------------------------------------------------------------------------
    Thread Implementations
------------------------------------------------------------------------------*/
// Assume that Context, DoubleBuffer, packet_Image, UDP_MAX_NB_PACKET_PER_LINE,
// IMAGE_DATA_HEADER and swapBuffers() are defined elsewhere.
// It is also assumed that the Context structure now contains a boolean field
// 'enableImageTransform' to toggle image transformation at runtime.

void *udpThread(void *arg) {
  Context *ctx = (Context *)arg;
  DoubleBuffer *db = ctx->doubleBuffer;
  int s = ctx->socket;
  struct sockaddr_in *si_other = ctx->si_other;
  socklen_t slen = sizeof(*si_other);
  ssize_t recv_len;
  struct packet_Image packet;

  // Local variables for reassembling line fragments
  uint32_t currentLineId = 0;
  int *receivedFragments =
      (int *)calloc(UDP_MAX_NB_PACKET_PER_LINE, sizeof(int));
  if (receivedFragments == NULL) {
    perror("Error allocating receivedFragments");
    exit(EXIT_FAILURE);
  }
  uint32_t fragmentCount = 0;

  while (ctx->running) {
    recv_len = recvfrom(s, &packet, sizeof(packet), 0,
                        (struct sockaddr *)si_other, &slen);
    if (recv_len < 0) {
      continue;
    }

    if (packet.type != IMAGE_DATA_HEADER) {
      continue;
    }

    if (currentLineId != packet.line_id) {
      currentLineId = packet.line_id;
      memset(receivedFragments, 0, packet.total_fragments * sizeof(int));
      fragmentCount = 0;
    }

    uint32_t offset = packet.fragment_id * packet.fragment_size;
    if (!receivedFragments[packet.fragment_id]) {
      receivedFragments[packet.fragment_id] = 1;
      fragmentCount++;
      memcpy(&db->activeBuffer_R[offset], packet.imageData_R,
             packet.fragment_size);
      memcpy(&db->activeBuffer_G[offset], packet.imageData_G,
             packet.fragment_size);
      memcpy(&db->activeBuffer_B[offset], packet.imageData_B,
             packet.fragment_size);
    }

    if (fragmentCount == packet.total_fragments) {
#if ENABLE_IMAGE_TRANSFORM
      if (ctx->enableImageTransform) {
        int lineSize = packet.total_fragments * packet.fragment_size;
        for (int i = 0; i < lineSize; i++) {
          // Retrieve original RGB values
          unsigned char r = db->activeBuffer_R[i];
          unsigned char g = db->activeBuffer_G[i];
          unsigned char b = db->activeBuffer_B[i];

          // Step 2: Calculate perceived luminance: Y = 0.299 * r + 0.587 * g +
          // 0.114 * b
          double luminance = 0.299 * r + 0.587 * g + 0.114 * b;

          // Step 3: Inversion and normalization:
          // Y_inv = 255 - Y, then I = Y_inv / 255.
          double invertedLuminance = 255.0 - luminance;
          double intensity = invertedLuminance / 255.0;

          // Step 4: Gamma correction: I_corr = intensity^(IMAGE_GAMMA)
          double correctedIntensity = pow(intensity, IMAGE_GAMMA);

          // Step 5: Modulate original RGB channels by the corrected intensity.
          db->activeBuffer_R[i] = (uint8_t)round(r * correctedIntensity);
          db->activeBuffer_G[i] = (uint8_t)round(g * correctedIntensity);
          db->activeBuffer_B[i] = (uint8_t)round(b * correctedIntensity);
        }
      }
#endif
      pthread_mutex_lock(&db->mutex);
      swapBuffers(db);
      updateLastValidImage(db); // Save image for audio persistence
      db->dataReady = 1;
      pthread_cond_signal(&db->cond);
      pthread_mutex_unlock(&db->mutex);
    }
  }

  free(receivedFragments);
  return NULL;
}

void *dmxSendingThread(void *arg) {
  DMXContext *dmxCtx = (DMXContext *)arg;
  unsigned char frame[DMX_FRAME_SIZE];

  // Vérifier si le descripteur de fichier DMX est valide
  if (dmxCtx->fd < 0) {
    fprintf(
        stderr,
        "DMX thread started with invalid file descriptor, exiting thread\n");
    return NULL;
  }

  while (dmxCtx->running && keepRunning) {
    // Vérifier si le descripteur de fichier est toujours valide
    if (dmxCtx->fd < 0) {
      fprintf(stderr, "DMX file descriptor became invalid, exiting thread\n");
      break;
    }

    // Vérifier immédiatement si un signal d'arrêt a été reçu
    if (!dmxCtx->running || !keepRunning) {
      break;
    }

    // Réinitialiser la trame DMX et définir le start code
    memset(frame, 0, DMX_FRAME_SIZE);
    frame[0] = 0;

    // Pour chaque spot, insérer les 3 canaux (R, G, B) à partir de l'adresse
    // définie
    for (int i = 0; i < DMX_NUM_SPOTS; i++) {
      int base = spotChannels[i];
      if ((base + 2) < DMX_FRAME_SIZE) {
        frame[base + 0] = dmxCtx->spots[i].red;
        frame[base + 1] = dmxCtx->spots[i].green;
        frame[base + 2] = dmxCtx->spots[i].blue;
      } else {
        fprintf(stderr, "DMX address out of bounds for spot %d\n", i);
      }
    }

    // Envoyer la trame DMX seulement si le fd est valide et que
    // l'application est toujours en cours d'exécution
    if (dmxCtx->running && keepRunning && dmxCtx->fd >= 0 &&
        send_dmx_frame(dmxCtx->fd, frame, DMX_FRAME_SIZE) < 0) {
      perror("Error sending DMX frame");
      // En cas d'erreur répétée, on peut quitter le thread
      if (errno == EBADF || errno == EIO) {
        fprintf(stderr, "Critical DMX error, exiting thread\n");
        break;
      }
    }

    // Utiliser un sleep interruptible qui vérifie périodiquement si un signal
    // d'arrêt a été reçu
    for (int i = 0; i < 5; i++) { // 5 * 5ms = 25ms total
      if (!dmxCtx->running || !keepRunning) {
        break;
      }
      usleep(5000); // 5ms
    }
  }

  printf("DMX thread terminating...\n");

  // Fermer le descripteur de fichier seulement s'il est valide
  if (dmxCtx->fd >= 0) {
    close(dmxCtx->fd);
    dmxCtx->fd = -1;
  }
  return NULL;
}

void *audioProcessingThread(void *arg) {
  Context *context = (Context *)arg;
  DoubleBuffer *db = context->doubleBuffer;
  // Local buffers for synth_AudioProcess to avoid holding mutex during synth
  uint8_t local_R[CIS_MAX_PIXELS_NB];
  uint8_t local_G[CIS_MAX_PIXELS_NB];
  uint8_t local_B[CIS_MAX_PIXELS_NB];

  // Timeout configuration for non-blocking audio processing
  struct timespec timeout;
  const long TIMEOUT_MS =
      10; // 10ms timeout - audio continues even without new frames

  printf("[AUDIO] Audio processing thread started with 10ms timeout\n");

  while (context->running) {
    // Calculate timeout time (current time + TIMEOUT_MS)
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += TIMEOUT_MS * 1000000L; // Convert ms to nanoseconds
    if (timeout.tv_nsec >= 1000000000L) {
      timeout.tv_sec += 1;
      timeout.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&db->mutex);

    // Wait for new data with timeout, or continue if timeout expires
    int wait_result = 0;
    while (!db->dataReady && context->running) {
      wait_result = pthread_cond_timedwait(&db->cond, &db->mutex, &timeout);
      if (wait_result == ETIMEDOUT) {
        // Timeout occurred - continue with last valid image
        break;
      }
    }

    if (!context->running) {
      pthread_mutex_unlock(&db->mutex);
      break;
    }

    static uint64_t audio_log_counter = 0;

    if (db->dataReady) {
      // New data available - copy fresh image and reset dataReady flag
      memcpy(local_R, db->processingBuffer_R, CIS_MAX_PIXELS_NB);
      memcpy(local_G, db->processingBuffer_G, CIS_MAX_PIXELS_NB);
      memcpy(local_B, db->processingBuffer_B, CIS_MAX_PIXELS_NB);

      db->dataReady = 0; // Mark as consumed

      // Debug logs removed for production use
      ++audio_log_counter;

    } else {
      // Timeout occurred or no new data - use persistent image
      // Debug logs removed for production use
    }

    pthread_mutex_unlock(&db->mutex);

    // Always get the most recent valid image for audio processing
    // This ensures audio continuity even when UDP stream stops
    getLastValidImageForAudio(db, local_R, local_G, local_B);

    // Call synthesis routine with image data (fresh, persistent, or test)
    synth_AudioProcess(local_R, local_G, local_B);
  }

  printf("[AUDIO] Audio processing thread terminated\n");
  return NULL;
}
