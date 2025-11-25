/* multithreading.c */

#include "multithreading.h"
#include "audio_c_api.h"
#include "auto_volume.h"
#include "config.h"
#include "config_instrument.h"
#include "config_loader.h"
#include "config_synth_additive.h" /* For IMU_FILTER_ALPHA_X, AUTO_VOLUME_POLL_MS */
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "synth_additive.h"
#include "udp.h"
#include "image_debug.h"
#include "logger.h"
#include "../processing/image_preprocessor.h"
#include "../processing/image_sequencer.h"
#include "../synthesis/photowave/synth_photowave.h"
#include <time.h>

/* External sequencer instance */
extern ImageSequencer *g_image_sequencer;

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
  int nb_pixels;
  
  if (pthread_mutex_init(&db->mutex, NULL) != 0) {
    log_error("THREAD", "Mutex initialization failed");
    exit(EXIT_FAILURE);
  }
  if (pthread_cond_init(&db->cond, NULL) != 0) {
    log_error("THREAD", "Condition variable initialization failed");
    exit(EXIT_FAILURE);
  }

  nb_pixels = get_cis_pixels_nb();
  
  db->activeBuffer_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->activeBuffer_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->activeBuffer_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));

  db->processingBuffer_R =
      (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->processingBuffer_G =
      (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->processingBuffer_B =
      (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));

  // Allocate persistent image buffers for audio continuity
  db->lastValidImage_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->lastValidImage_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  db->lastValidImage_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));

  if (!db->activeBuffer_R || !db->activeBuffer_G || !db->activeBuffer_B ||
      !db->processingBuffer_R || !db->processingBuffer_G ||
      !db->processingBuffer_B || !db->lastValidImage_R ||
      !db->lastValidImage_G || !db->lastValidImage_B) {
    log_error("THREAD", "Allocation of image buffers failed");
    exit(EXIT_FAILURE);
  }

  // Initialize persistent image with black (zero)
  memset(db->lastValidImage_R, 0, nb_pixels);
  memset(db->lastValidImage_G, 0, nb_pixels);
  memset(db->lastValidImage_B, 0, nb_pixels);

  db->dataReady = 0;
  db->lastValidImageExists = 0;
  db->udp_frames_received = 0;
  db->audio_frames_processed = 0;
  db->last_udp_frame_time = time(NULL);
  
  // 🔧 BUGFIX: Initialize preprocessed_data with safe default values
  // This prevents bus errors when audio thread starts before first UDP frame
  
  /* Initialize additive synthesis data */
  memset(db->preprocessed_data.additive.grayscale, 0, sizeof(db->preprocessed_data.additive.grayscale));
  memset(db->preprocessed_data.additive.notes, 0, sizeof(db->preprocessed_data.additive.notes));
  db->preprocessed_data.additive.contrast_factor = 1.0f;
  
  /* Initialize polyphonic synthesis data */
#ifndef DISABLE_POLYPHONIC
  memset(db->preprocessed_data.polyphonic.grayscale, 0, sizeof(db->preprocessed_data.polyphonic.grayscale));
  memset(db->preprocessed_data.polyphonic.magnitudes, 0, sizeof(db->preprocessed_data.polyphonic.magnitudes));
  db->preprocessed_data.polyphonic.valid = 0;
#endif
  
  /* Initialize photowave synthesis data */
  memset(db->preprocessed_data.photowave.r, 0, sizeof(db->preprocessed_data.photowave.r));
  memset(db->preprocessed_data.photowave.g, 0, sizeof(db->preprocessed_data.photowave.g));
  memset(db->preprocessed_data.photowave.b, 0, sizeof(db->preprocessed_data.photowave.b));
  
  /* Initialize stereo with center panning (equal-power law) */
  for (int i = 0; i < PREPROCESS_MAX_NOTES; i++) {
    db->preprocessed_data.stereo.pan_positions[i] = 0.0f;  // Center
    db->preprocessed_data.stereo.left_gains[i] = 0.707f;   // -3dB (equal power)
    db->preprocessed_data.stereo.right_gains[i] = 0.707f;  // -3dB (equal power)
  }
  
  /* Initialize DMX with black */
#ifdef USE_DMX
  memset(&db->preprocessed_data.dmx, 0, sizeof(db->preprocessed_data.dmx));
#endif
  
  db->preprocessed_data.timestamp_us = 0;
  
  log_info("THREAD", "DoubleBuffer preprocessed_data initialized with safe defaults");
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
  int nb_pixels = get_cis_pixels_nb();
  
  // Copy processing buffer to persistent image buffer
  memcpy(db->lastValidImage_R, db->processingBuffer_R, nb_pixels);
  memcpy(db->lastValidImage_G, db->processingBuffer_G, nb_pixels);
  memcpy(db->lastValidImage_B, db->processingBuffer_B, nb_pixels);

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
  int nb_pixels = get_cis_pixels_nb();
  
  pthread_mutex_lock(&db->mutex);

  if (db->lastValidImageExists) {
    memcpy(out_R, db->lastValidImage_R, nb_pixels);
    memcpy(out_G, db->lastValidImage_G, nb_pixels);
    memcpy(out_B, db->lastValidImage_B, nb_pixels);
    db->audio_frames_processed++;
  } else {
    // If no valid image exists, use black (silence)
    memset(out_R, 0, nb_pixels);
    memset(out_G, 0, nb_pixels);
    memset(out_B, 0, nb_pixels);
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
  Context *ctx;
  DoubleBuffer *db;
  AudioImageBuffers *audioBuffers;
  int s;
  struct sockaddr_in *si_other;
  socklen_t slen;
  ssize_t recv_len;
  struct packet_Image packet;
  int nb_pixels;
  uint8_t *mixed_R;
  uint8_t *mixed_G;
  uint8_t *mixed_B;
  uint32_t currentLineId;
  int *receivedFragments;
  uint32_t fragmentCount;
  uint8_t *audio_write_R;
  uint8_t *audio_write_G;
  uint8_t *audio_write_B;
  int audio_write_started;
  
  /* Initialize variables */
  ctx = (Context *)arg;
  db = ctx->doubleBuffer;
  audioBuffers = ctx->audioImageBuffers;
  s = ctx->socket;
  si_other = ctx->si_other;
  slen = sizeof(*si_other);
  nb_pixels = get_cis_pixels_nb();
  mixed_R = NULL;
  mixed_G = NULL;
  mixed_B = NULL;
  currentLineId = 0;
  fragmentCount = 0;
  audio_write_R = NULL;
  audio_write_G = NULL;
  audio_write_B = NULL;
  audio_write_started = 0;

  /* Allocate receivedFragments */
  receivedFragments = (int *)calloc(UDP_MAX_NB_PACKET_PER_LINE, sizeof(int));
  if (receivedFragments == NULL) {
    log_error("THREAD", "Error allocating receivedFragments: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  
  /* Allocate mixed buffers dynamically */
  mixed_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  mixed_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  mixed_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  
  if (!mixed_R || !mixed_G || !mixed_B) {
    log_error("THREAD", "Failed to allocate mixed buffers");
    if (mixed_R) free(mixed_R);
    if (mixed_G) free(mixed_G);
    if (mixed_B) free(mixed_B);
    free(receivedFragments);
    exit(EXIT_FAILURE);
  }

  log_info("THREAD", "UDP thread started with dual buffer system");
  log_info("THREAD", "Listening for packets on socket %d, expecting IMAGE_DATA_HEADER (0x%02X)", s, IMAGE_DATA_HEADER);

  while (ctx->running) {
    recv_len = recvfrom(s, &packet, sizeof(packet), 0,
                        (struct sockaddr *)si_other, &slen);
    if (recv_len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        log_error("THREAD", "recvfrom error: %s", strerror(errno));
      }
      continue;
    }

#ifdef DEBUG_UDP
    // Debug: Log every received packet
    log_debug("UDP", "Received packet: size=%zd bytes, type=0x%02X", recv_len, packet.type);
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
        log_debug("IMU", "First IMU packet received! raw_x=%.6f", raw_x);
#endif
      } else {
        ctx->imu_x_filtered = IMU_FILTER_ALPHA_X * raw_x +
                              (1.0f - IMU_FILTER_ALPHA_X) * ctx->imu_x_filtered;
      }
      ctx->last_imu_time = time(NULL);
      pthread_mutex_unlock(&ctx->imu_mutex);

#ifdef DEBUG_IMU_PACKETS
      log_debug("IMU", "raw_x=%.6f filtered=%.6f threshold=%.6f active=%s", raw_x,
                ctx->imu_x_filtered, g_additive_config.imu_active_threshold_x,
                (fabsf(ctx->imu_x_filtered) >= g_additive_config.imu_active_threshold_x) ? "YES" : "NO");
#endif
#ifdef DEBUG_UDP
      log_debug("UDP", "IMU raw_x=%.6f filtered=%.6f", raw_x, ctx->imu_x_filtered);
#endif
      continue;
    }

    if (packet.type != IMAGE_DATA_HEADER) {
#ifdef DEBUG_UDP
      log_debug("UDP", "Ignoring packet with type 0x%02X (expected 0x%02X)", packet.type, IMAGE_DATA_HEADER);
#endif
      continue;
    }

#ifdef DEBUG_UDP
    log_debug("UDP", "Processing IMAGE_DATA packet: line_id=%u, fragment_id=%u/%u, size=%u",
              packet.line_id, packet.fragment_id, packet.total_fragments, packet.fragment_size);
#endif

    if (currentLineId != packet.line_id) {
      // If we had a previous incomplete line, log it
      if (currentLineId != 0 && fragmentCount > 0) {
#ifdef DEBUG_UDP
        log_debug("UDP", "INCOMPLETE LINE DISCARDED: line_id=%u had %u/%d fragments",
                  currentLineId, fragmentCount, UDP_MAX_NB_PACKET_PER_LINE);
#endif

        // Complete the incomplete audio buffer write if it was started
        if (audio_write_started) {
          audio_image_buffers_complete_write(audioBuffers);
          audio_write_started = 0;
#ifdef DEBUG_UDP
          log_debug("UDP", "Completed partial audio buffer write for incomplete line");
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
        log_debug("UDP", "Started audio buffer write for line_id=%u", packet.line_id);
#endif
      } else {
        audio_write_started = 0;
        log_warning("THREAD", "Failed to start audio buffer write");
      }
    }

    // Validate fragment_id to prevent buffer overflow
    if (packet.fragment_id >= UDP_MAX_NB_PACKET_PER_LINE) {
      log_error("THREAD", "fragment_id %u exceeds maximum %u, ignoring packet",
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
    log_debug("UDP", "Fragment count: %u/%u for line %u", fragmentCount, packet.total_fragments, packet.line_id);
#endif

    if (fragmentCount == packet.total_fragments) {
      PreprocessedImageData preprocessed_temp;
      
#ifdef DEBUG_UDP
      log_debug("UDP", "COMPLETE LINE RECEIVED! line_id=%u, %u fragments", packet.line_id, fragmentCount);
#endif
      /* Complete line received */

      /* Complete audio buffer write and swap */
      if (audio_write_started) {
        audio_image_buffers_complete_write(audioBuffers);
        audio_write_started = 0;
      }

      /* 🎬 NEW ARCHITECTURE: Sequencer BEFORE preprocessing
       * 1. Sequencer mixes RGB (live + sequences)
       * 2. Preprocessing calculates grayscale/pan/DMX from MIXED RGB
       * 3. Display shows the MIXED RGB colors
       */
      
      /* Step 1: Mix RGB through sequencer (or passthrough if no sequencer) */
      if (g_image_sequencer) {
        if (image_sequencer_process_frame(g_image_sequencer,
                                          db->activeBuffer_R, db->activeBuffer_G, db->activeBuffer_B,
                                          mixed_R, mixed_G, mixed_B) != 0) {
          log_error("THREAD", "Sequencer processing failed, using live RGB");
          memcpy(mixed_R, db->activeBuffer_R, nb_pixels);
          memcpy(mixed_G, db->activeBuffer_G, nb_pixels);
          memcpy(mixed_B, db->activeBuffer_B, nb_pixels);
        }
      } else {
        /* No sequencer: passthrough live RGB */
        memcpy(mixed_R, db->activeBuffer_R, nb_pixels);
        memcpy(mixed_G, db->activeBuffer_G, nb_pixels);
        memcpy(mixed_B, db->activeBuffer_B, nb_pixels);
      }
      
      /* Step 2: Preprocess the MIXED RGB (pan calculated from mixed color temperature) */
      if (image_preprocess_frame(mixed_R, mixed_G, mixed_B, &preprocessed_temp) != 0) {
        log_error("THREAD", "Image preprocessing failed");
      }
      
      /* Step 2.5: FFT is already calculated in preprocess_polyphonic() */
      /* No additional action needed - FFT data is in preprocessed_temp.polyphonic */

      /* 🎵 PHOTOWAVE FIX: Pass grayscale image data to Photowave synthesis thread
       * This connects the scanner data pipeline to Photowave for audio generation
       * Note: Photowave will convert RGB to grayscale internally, so we pass mixed_R
       */
      synth_photowave_set_image_line(&g_photowave_state, 
                                     mixed_R, 
                                     nb_pixels);

      /* Step 3: Update display buffers with MIXED RGB (fixes N&B display issue) */
      pthread_mutex_lock(&db->mutex);
      
      /* CRITICAL FIX: Copy mixed RGB to activeBuffer so display shows colors */
      memcpy(db->activeBuffer_R, mixed_R, nb_pixels);
      memcpy(db->activeBuffer_G, mixed_G, nb_pixels);
      memcpy(db->activeBuffer_B, mixed_B, nb_pixels);
      
      swapBuffers(db);
      updateLastValidImage(db);
      db->preprocessed_data = preprocessed_temp;
      db->dataReady = 1;
      pthread_cond_signal(&db->cond);
      pthread_mutex_unlock(&db->mutex);
      
      /* 🎨 DISPLAY FIX: Update global display buffers with MIXED RGB colors
       * This replaces the grayscale→RGB conversion in synth_additive.c
       */
      
      /* DEBUG: Pixel difference check - DISABLED (too verbose in production) */
      /*
      if (++diff_log_counter % 1000 == 0) {
        diff_count = 0;
        for (i = 0; i < nb_pixels; i++) {
          if (mixed_R[i] != db->activeBuffer_R[i]) diff_count++;
        }
        log_debug("UDP", "Pixels different: %d/%d (%.1f%%)",
                  diff_count, nb_pixels, (diff_count * 100.0f) / nb_pixels);
      }
      */
      
      pthread_mutex_lock(&g_displayable_synth_mutex);
      memcpy(g_displayable_synth_R, mixed_R, nb_pixels);
      memcpy(g_displayable_synth_G, mixed_G, nb_pixels);
      memcpy(g_displayable_synth_B, mixed_B, nb_pixels);
      pthread_mutex_unlock(&g_displayable_synth_mutex);

      /* Capture raw scanner data only when new UDP data arrives
       * Function handles runtime enable/disable internally
       */
      image_debug_capture_raw_scanner_line(db->processingBuffer_R, 
                                          db->processingBuffer_G, 
                                          db->processingBuffer_B);
    }
  }

  log_info("THREAD", "UDP thread terminating");
  
  // Free allocated buffers
  if (mixed_R) free(mixed_R);
  if (mixed_G) free(mixed_G);
  if (mixed_B) free(mixed_B);
  free(receivedFragments);
  
  return NULL;
}

void *dmxSendingThread(void *arg) {
  DMXContext *dmxCtx = (DMXContext *)arg;
  unsigned char frame[DMX_FRAME_SIZE];

  // Check if DMX file descriptor is valid
  if (dmxCtx->fd < 0) {
    log_error("THREAD", "DMX thread started with invalid file descriptor, exiting thread");
    return NULL;
  }

  while (dmxCtx->running && keepRunning) {
    // Check if file descriptor is still valid
    if (dmxCtx->fd < 0) {
      log_error("THREAD", "DMX file descriptor became invalid, exiting thread");
      break;
    }

    // Check immediately if a stop signal has been received
    if (!dmxCtx->running || !keepRunning) {
      break;
    }

    // Reset DMX frame and set start code
    memset(frame, 0, DMX_FRAME_SIZE);
    frame[0] = 0;

    // For each spot, insert the 3 channels (R, G, B) starting from the address
    // defined in the new flexible structure
    for (int i = 0; i < dmxCtx->num_spots; i++) {
      int base = dmxCtx->spots[i].start_channel;
      if ((base + 2) < DMX_FRAME_SIZE) {
        frame[base + 0] = dmxCtx->spots[i].data.rgb.red;
        frame[base + 1] = dmxCtx->spots[i].data.rgb.green;
        frame[base + 2] = dmxCtx->spots[i].data.rgb.blue;
      } else {
        log_error("THREAD", "DMX address out of bounds for spot %d", i);
      }
    }

    // Send DMX frame only if fd is valid and the
    // application is still running
    if (dmxCtx->running && keepRunning && dmxCtx->fd >= 0 &&
        send_dmx_frame(dmxCtx->fd, frame, DMX_FRAME_SIZE) < 0) {
      log_error("THREAD", "Error sending DMX frame: %s", strerror(errno));
      // In case of repeated error, we can exit the thread
      if (errno == EBADF || errno == EIO) {
        log_error("THREAD", "Critical DMX error, exiting thread");
        break;
      }
    }

    // Use an interruptible sleep that periodically checks if a stop signal
    // has been received
    for (int i = 0; i < 5; i++) { // 5 * 5ms = 25ms total
      if (!dmxCtx->running || !keepRunning) {
        break;
      }
      usleep(5000); // 5ms
    }
  }

  log_info("THREAD", "DMX thread terminating");

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

  log_info("THREAD", "Audio processing thread started with lock-free dual buffer system");
  log_info("THREAD", "Real-time audio processing guaranteed - no timeouts, no blocking!");

  // OPTIMIZATION: Set real-time priority for audio processing thread
  // This ensures the thread gets CPU time even under system load
#ifdef __linux__
  struct sched_param param;
  param.sched_priority = 70; // Same priority as RtAudio callback
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0) {
    log_info("THREAD", "Audio processing thread set to RT priority 70 (SCHED_FIFO)");
  } else {
    log_warning("THREAD", "Failed to set RT priority (may need CAP_SYS_NICE capability)");
  }
#endif

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

  log_info("THREAD", "Audio processing thread terminated");
  return NULL;
}
