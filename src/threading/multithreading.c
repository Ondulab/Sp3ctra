/* multithreading.c */

#include "multithreading.h"
#include "audio_c_api.h"
#include "auto_volume.h"
#include "config.h"
#include "config_loader.h"
#include "config_synth_additive.h" /* For IMU_FILTER_ALPHA_X, AUTO_VOLUME_POLL_MS */
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "synth_additive.h"
#include "udp.h"
#include "image_debug.h"
#include "../processing/image_preprocessor.h"
#include <time.h>

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
  
  // 🔧 BUGFIX: Initialize preprocessed_data with safe default values
  // This prevents bus errors when audio thread starts before first UDP frame
  memset(db->preprocessed_data.grayscale, 0, sizeof(db->preprocessed_data.grayscale));
  db->preprocessed_data.contrast_factor = 1.0f;
  
  // Initialize stereo with center panning (equal-power law)
  for (int i = 0; i < PREPROCESS_MAX_NOTES; i++) {
    db->preprocessed_data.stereo.pan_positions[i] = 0.0f;  // Center
    db->preprocessed_data.stereo.left_gains[i] = 0.707f;   // -3dB (equal power)
    db->preprocessed_data.stereo.right_gains[i] = 0.707f;  // -3dB (equal power)
  }
  
  // Initialize DMX with black
  memset(&db->preprocessed_data.dmx, 0, sizeof(db->preprocessed_data.dmx));
  
  db->preprocessed_data.timestamp_us = 0;
  
  printf("[INIT] DoubleBuffer preprocessed_data initialized with safe defaults\n");
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
  AudioImageBuffers *audioBuffers = ctx->audioImageBuffers;
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

  // Pointers for audio buffer writing
  uint8_t *audio_write_R = NULL;
  uint8_t *audio_write_G = NULL;
  uint8_t *audio_write_B = NULL;
  int audio_write_started = 0;

  printf("[UDP] UDP thread started with dual buffer system\n");
  printf("[UDP] Listening for packets on socket %d, expecting "
         "IMAGE_DATA_HEADER (0x%02X)\n",
         s, IMAGE_DATA_HEADER);

  while (ctx->running) {
    recv_len = recvfrom(s, &packet, sizeof(packet), 0,
                        (struct sockaddr *)si_other, &slen);
    if (recv_len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        printf("[UDP] recvfrom error: %s\n", strerror(errno));
      }
      continue;
    }

#ifdef DEBUG_UDP
    // Debug: Log every received packet
    printf("[UDP] Received packet: size=%zd bytes, type=0x%02X\n", recv_len,
           packet.type);
#endif

    if (packet.type == IMU_DATA_HEADER) {
      /* Lightweight IMU packet handling: update filtered X in Context.
         Keep this code fast and non-blocking. Comments in English as per
         project conventions. */
      struct packet_IMU *imu = (struct packet_IMU *)&packet;
      pthread_mutex_lock(&ctx->imu_mutex);
      float raw_x = imu->acc[0]; // X axis accelerometer
      
      if (!ctx->imu_has_value) {
        ctx->imu_x_filtered = raw_x;
        ctx->imu_has_value = 1;
#ifdef DEBUG_IMU_PACKETS
        printf("[IMU] First IMU packet received! raw_x=%.6f\n", raw_x);
#endif
      } else {
        ctx->imu_x_filtered = IMU_FILTER_ALPHA_X * raw_x +
                              (1.0f - IMU_FILTER_ALPHA_X) * ctx->imu_x_filtered;
      }
      ctx->last_imu_time = time(NULL);
      pthread_mutex_unlock(&ctx->imu_mutex);

#ifdef DEBUG_IMU_PACKETS
      printf("[IMU] raw_x=%.6f filtered=%.6f threshold=%.6f active=%s\n", raw_x,
             ctx->imu_x_filtered, g_additive_config.imu_active_threshold_x,
             (fabsf(ctx->imu_x_filtered) >= g_additive_config.imu_active_threshold_x) ? "YES"
                                                                    : "NO");
#endif
#ifdef DEBUG_UDP
      printf("[UDP][IMU] raw_x=%.6f filtered=%.6f\n", raw_x,
             ctx->imu_x_filtered);
#endif
      continue;
    }

    if (packet.type != IMAGE_DATA_HEADER) {
#ifdef DEBUG_UDP
      printf("[UDP] Ignoring packet with type 0x%02X (expected 0x%02X)\n",
             packet.type, IMAGE_DATA_HEADER);
#endif
      continue;
    }

#ifdef DEBUG_UDP
    printf("[UDP] Processing IMAGE_DATA packet: line_id=%u, fragment_id=%u/%u, "
           "size=%u\n",
           packet.line_id, packet.fragment_id, packet.total_fragments,
           packet.fragment_size);
#endif

    if (currentLineId != packet.line_id) {
      // If we had a previous incomplete line, log it
      if (currentLineId != 0 && fragmentCount > 0) {
#ifdef DEBUG_UDP
        printf("[UDP] ⚠️  INCOMPLETE LINE DISCARDED: line_id=%u had %u/%d "
               "fragments\n",
               currentLineId, fragmentCount, UDP_MAX_NB_PACKET_PER_LINE);
#endif

        // Complete the incomplete audio buffer write if it was started
        if (audio_write_started) {
          audio_image_buffers_complete_write(audioBuffers);
          audio_write_started = 0;
#ifdef DEBUG_UDP
          printf("[UDP] Completed partial audio buffer write for incomplete "
                 "line\n");
#endif
        }
      }

      // New line started - prepare for writing
      currentLineId = packet.line_id;
      memset(receivedFragments, 0, UDP_MAX_NB_PACKET_PER_LINE * sizeof(int));
      fragmentCount = 0;

      // Start writing to audio buffers for new line
      if (audio_image_buffers_start_write(audioBuffers, &audio_write_R,
                                          &audio_write_G,
                                          &audio_write_B) == 0) {
        audio_write_started = 1;
#ifdef DEBUG_UDP
        printf("[UDP] Started audio buffer write for line_id=%u\n",
               packet.line_id);
#endif
      } else {
        audio_write_started = 0;
        printf("[UDP] Warning: Failed to start audio buffer write\n");
      }
    }

    // Validate fragment_id to prevent buffer overflow
    if (packet.fragment_id >= UDP_MAX_NB_PACKET_PER_LINE) {
      printf(
          "[UDP] ERROR: fragment_id %u exceeds maximum %u, ignoring packet\n",
          packet.fragment_id, UDP_MAX_NB_PACKET_PER_LINE);
      continue;
    }

    uint32_t offset = packet.fragment_id * packet.fragment_size;
    if (!receivedFragments[packet.fragment_id]) {
      receivedFragments[packet.fragment_id] = 1;
      fragmentCount++;

      // Write to legacy double buffer (for display)
      memcpy(&db->activeBuffer_R[offset], packet.imageData_R,
             packet.fragment_size);
      memcpy(&db->activeBuffer_G[offset], packet.imageData_G,
             packet.fragment_size);
      memcpy(&db->activeBuffer_B[offset], packet.imageData_B,
             packet.fragment_size);

      // Write to new audio buffers (for continuous audio)
      if (audio_write_started) {
        memcpy(&audio_write_R[offset], packet.imageData_R,
               packet.fragment_size);
        memcpy(&audio_write_G[offset], packet.imageData_G,
               packet.fragment_size);
        memcpy(&audio_write_B[offset], packet.imageData_B,
               packet.fragment_size);
      }
    }

#ifdef DEBUG_UDP
    printf("[UDP] Fragment count: %u/%u for line %u\n", fragmentCount,
           packet.total_fragments, packet.line_id);
#endif

    if (fragmentCount == packet.total_fragments) {
#ifdef DEBUG_UDP
      printf("[UDP] ✅ COMPLETE LINE RECEIVED! line_id=%u, %u fragments\n",
             packet.line_id, fragmentCount);
#endif
      // Complete line received

      // Complete audio buffer write and swap
      if (audio_write_started) {
        audio_image_buffers_complete_write(audioBuffers);
        audio_write_started = 0;
      }

      // 🎯 NEW: Preprocess the complete image (grayscale, contrast, stereo, DMX)
      // This is done ONCE per received image in the UDP thread
      PreprocessedImageData preprocessed_temp;
      if (image_preprocess_frame(db->activeBuffer_R, db->activeBuffer_G, 
                                 db->activeBuffer_B, &preprocessed_temp) == 0) {
#ifdef DEBUG_UDP
        printf("[UDP] Preprocessing complete: contrast=%.3f, timestamp=%llu\n",
               preprocessed_temp.contrast_factor, preprocessed_temp.timestamp_us);
#endif
      } else {
        printf("[UDP] ERROR: Image preprocessing failed\n");
      }

      // Handle legacy double buffer (for display) and preprocessed data
      pthread_mutex_lock(&db->mutex);
      swapBuffers(db);
      updateLastValidImage(db); // Save image for audio persistence
      
      // Store preprocessed data in single mutex-protected buffer
      db->preprocessed_data = preprocessed_temp;
      
      db->dataReady = 1;
      pthread_cond_signal(&db->cond);
      pthread_mutex_unlock(&db->mutex);

      // Capture raw scanner data only when new UDP data arrives
      // Function handles runtime enable/disable internally
      image_debug_capture_raw_scanner_line(db->processingBuffer_R, 
                                          db->processingBuffer_G, 
                                          db->processingBuffer_B);
    }
  }

  printf("[UDP] UDP thread terminating\n");
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
    // définie dans la nouvelle structure flexible
    for (int i = 0; i < dmxCtx->num_spots; i++) {
      int base = dmxCtx->spots[i].start_channel;
      if ((base + 2) < DMX_FRAME_SIZE) {
        frame[base + 0] = dmxCtx->spots[i].data.rgb.red;
        frame[base + 1] = dmxCtx->spots[i].data.rgb.green;
        frame[base + 2] = dmxCtx->spots[i].data.rgb.blue;
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
  AudioImageBuffers *audioBuffers = context->audioImageBuffers;

  // Local buffers for synth_AudioProcess - lock-free access!
  uint8_t *audio_read_R = NULL;
  uint8_t *audio_read_G = NULL;
  uint8_t *audio_read_B = NULL;

  printf("[AUDIO] Audio processing thread started with lock-free dual buffer "
         "system\n");
  printf("[AUDIO] Real-time audio processing guaranteed - no timeouts, no "
         "blocking!\n");

  while (context->running) {
    // Get current read pointers atomically (no mutex, no blocking!)
    audio_image_buffers_get_read_pointers(audioBuffers, &audio_read_R,
                                          &audio_read_G, &audio_read_B);

    // Call synthesis routine directly with stable image data
    // This will NEVER block, even if scanner disconnects!
    synth_AudioProcess(audio_read_R, audio_read_G, audio_read_B, context->doubleBuffer);

    /* Auto-volume periodic update (lightweight). Runs in audioProcessingThread
       (non-RT) to avoid doing work in the RtAudio callback. */
    if (gAutoVolumeInstance) {
      static uint64_t last_auto_ms = 0;
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      uint64_t now =
          (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
      if (last_auto_ms == 0) {
        last_auto_ms = now;
      }
      uint64_t dt = (now > last_auto_ms) ? (now - last_auto_ms) : 0;
      if (dt >= (uint64_t)AUTO_VOLUME_POLL_MS) {
        auto_volume_step(gAutoVolumeInstance, (unsigned int)dt);
        last_auto_ms = now;
      }
    }

    // Small sleep to prevent excessive CPU usage
    // This is the only delay in the audio thread
    usleep(100); // 0.1ms - much smaller than before
  }

  printf("[AUDIO] Audio processing thread terminated\n");
  return NULL;
}
