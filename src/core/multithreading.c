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

#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*------------------------------------------------------------------------------
    Helper Functions
------------------------------------------------------------------------------*/

// Wait for the double buffer to be ready
static void WaitForDoubleBuffer(DoubleBuffer *db) {
  pthread_mutex_lock(&db->mutex);
  while (!db->dataReady) {
    pthread_cond_wait(&db->cond, &db->mutex);
  }
  pthread_mutex_unlock(&db->mutex);
}

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

  if (!db->activeBuffer_R || !db->activeBuffer_G || !db->activeBuffer_B ||
      !db->processingBuffer_R || !db->processingBuffer_G ||
      !db->processingBuffer_B) {
    fprintf(stderr, "Error: Allocation of image buffers failed\n");
    exit(EXIT_FAILURE);
  }

  db->dataReady = 0;
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

  while (context->running) {
    // Wait until one of the buffers is ready for processing
    WaitForDoubleBuffer(db);

    // Mark double buffer as free, swap it
    pthread_mutex_lock(&db->mutex);
    db->dataReady = 0;
    swapBuffers(db);
    pthread_mutex_unlock(&db->mutex);

    // Call your synthesis routine to fill the audio buffers
    synth_AudioProcess(db->processingBuffer_R, db->processingBuffer_G,
                       db->processingBuffer_B);
  }

  return NULL;
}
