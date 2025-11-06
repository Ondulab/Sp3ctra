/* audio_image_buffers.c */

#include "audio_image_buffers.h"
#include "config.h"
#include "config_instrument.h"
#include "error.h"
#include "logger.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Initialize the dual buffer system for audio-image data
 * @param buffers Pointer to AudioImageBuffers structure
 * @return 0 on success, -1 on error
 */
int audio_image_buffers_init(AudioImageBuffers *buffers) {
  int nb_pixels;
  int i;
  float phase;
  uint8_t test_value;

  if (!buffers) {
    fprintf(stderr, "ERROR: AudioImageBuffers pointer is NULL\n");
    return -1;
  }

  // Initialize all pointers to NULL for safe cleanup
  memset(buffers, 0, sizeof(AudioImageBuffers));

  // Get runtime pixel count
  nb_pixels = get_cis_pixels_nb();

  // Allocate Buffer 0 - RGB channels separated for memory contiguity
  buffers->buffer0_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  buffers->buffer0_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  buffers->buffer0_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));

  // Allocate Buffer 1 - RGB channels separated for memory contiguity
  buffers->buffer1_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  buffers->buffer1_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  buffers->buffer1_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));

  // Check all allocations
  if (!buffers->buffer0_R || !buffers->buffer0_G || !buffers->buffer0_B ||
      !buffers->buffer1_R || !buffers->buffer1_G || !buffers->buffer1_B) {
    fprintf(stderr, "ERROR: Failed to allocate audio image buffers\n");
    audio_image_buffers_cleanup(buffers);
    return -1;
  }

  // Initialize buffers with test pattern to ensure audio synthesis works
  // This provides immediate audio feedback even without scanner data
  for (i = 0; i < nb_pixels; i++) {
    // Create a simple test pattern: sine wave for audio testing
    phase = (float)i / nb_pixels * 2.0f * M_PI * 4.0f; // 4 cycles
    test_value = (uint8_t)(127.0f + 127.0f * sin(phase));

    buffers->buffer0_R[i] = test_value;
    buffers->buffer0_G[i] = test_value / 2; // Different pattern for G
    buffers->buffer0_B[i] = test_value / 4; // Different pattern for B

    buffers->buffer1_R[i] = test_value;
    buffers->buffer1_G[i] = test_value / 2;
    buffers->buffer1_B[i] = test_value / 4;
  }

  log_info("BUFFERS", "Audio image buffers initialized with test pattern for immediate audio feedback");

  // Initialize atomic indices
  // Buffer 0 starts as read buffer, Buffer 1 starts as write buffer
  atomic_init(&buffers->read_buffer_index, 0);
  atomic_init(&buffers->write_buffer_index, 1);

  // Initialize write mutex
  if (pthread_mutex_init(&buffers->write_mutex, NULL) != 0) {
    fprintf(stderr, "ERROR: Failed to initialize write mutex\n");
    audio_image_buffers_cleanup(buffers);
    return -1;
  }

  // Initialize statistics
  buffers->lines_received = 0;
  buffers->lines_processed = 0;
  buffers->buffer_swaps = 0;
  buffers->initialized = 1;

  log_info("BUFFERS", "Dual buffer system initialized: 2 x %d pixels x 3 channels", nb_pixels);
  log_info("BUFFERS", "Initial state: Buffer 0 = read, Buffer 1 = write");

  return 0;
}

/**
 * @brief Cleanup the dual buffer system
 * @param buffers Pointer to AudioImageBuffers structure
 */
void audio_image_buffers_cleanup(AudioImageBuffers *buffers) {
  if (!buffers) {
    return;
  }

  // Free all allocated memory
  if (buffers->buffer0_R) {
    free(buffers->buffer0_R);
    buffers->buffer0_R = NULL;
  }
  if (buffers->buffer0_G) {
    free(buffers->buffer0_G);
    buffers->buffer0_G = NULL;
  }
  if (buffers->buffer0_B) {
    free(buffers->buffer0_B);
    buffers->buffer0_B = NULL;
  }
  if (buffers->buffer1_R) {
    free(buffers->buffer1_R);
    buffers->buffer1_R = NULL;
  }
  if (buffers->buffer1_G) {
    free(buffers->buffer1_G);
    buffers->buffer1_G = NULL;
  }
  if (buffers->buffer1_B) {
    free(buffers->buffer1_B);
    buffers->buffer1_B = NULL;
  }

  // Destroy mutex if initialized
  if (buffers->initialized) {
    pthread_mutex_destroy(&buffers->write_mutex);
  }

  // Reset structure
  memset(buffers, 0, sizeof(AudioImageBuffers));

  log_info("BUFFERS", "Audio image buffers cleanup completed");
}

/**
 * @brief Start writing to the current write buffer (UDP thread)
 * @param buffers Pointer to AudioImageBuffers structure
 * @param out_R Pointer to receive R channel write buffer
 * @param out_G Pointer to receive G channel write buffer
 * @param out_B Pointer to receive B channel write buffer
 * @return 0 on success, -1 on error
 */
int audio_image_buffers_start_write(AudioImageBuffers *buffers, uint8_t **out_R,
                                    uint8_t **out_G, uint8_t **out_B) {
  if (!buffers || !buffers->initialized) {
    fprintf(stderr, "ERROR: AudioImageBuffers not initialized\n");
    return -1;
  }

  if (!out_R || !out_G || !out_B) {
    fprintf(stderr, "ERROR: Output pointers are NULL\n");
    return -1;
  }

  // Lock write mutex to protect against concurrent UDP writes
  pthread_mutex_lock(&buffers->write_mutex);

  // Get current write buffer index atomically
  int write_idx = atomic_load(&buffers->write_buffer_index);

  // Return pointers to the current write buffer
  if (write_idx == 0) {
    *out_R = buffers->buffer0_R;
    *out_G = buffers->buffer0_G;
    *out_B = buffers->buffer0_B;
  } else {
    *out_R = buffers->buffer1_R;
    *out_G = buffers->buffer1_G;
    *out_B = buffers->buffer1_B;
  }

  // Note: Mutex remains locked until complete_write() is called
  return 0;
}

/**
 * @brief Complete writing and perform atomic buffer swap (UDP thread)
 * @param buffers Pointer to AudioImageBuffers structure
 */
void audio_image_buffers_complete_write(AudioImageBuffers *buffers) {
  if (!buffers || !buffers->initialized) {
    fprintf(stderr, "ERROR: AudioImageBuffers not initialized\n");
    return;
  }

  // Perform atomic buffer swap
  // The write buffer becomes the new read buffer
  // The old read buffer becomes the new write buffer
  int old_write_idx = atomic_load(&buffers->write_buffer_index);
  int old_read_idx = atomic_load(&buffers->read_buffer_index);

  // Atomic swap: exchange read and write indices
  atomic_store(&buffers->read_buffer_index, old_write_idx);
  atomic_store(&buffers->write_buffer_index, old_read_idx);

  // Update statistics
  buffers->lines_received++;
  buffers->buffer_swaps++;

  // Unlock write mutex
  pthread_mutex_unlock(&buffers->write_mutex);

  // Debug log (can be disabled for production)
#ifdef DEBUG_BUFFERS
  static uint64_t log_counter = 0;
  if ((log_counter++ % 1000) == 0) { // Log every 1000 swaps
    printf("AudioImageBuffers: Swapped buffers (read=%d, write=%d) - %llu "
           "lines received\n",
           atomic_load(&buffers->read_buffer_index),
           atomic_load(&buffers->write_buffer_index), buffers->lines_received);
  }
#endif
}

/**
 * @brief Get read pointers for Additive synthesis processing (lock-free)
 * @param buffers Pointer to AudioImageBuffers structure
 * @param out_R Pointer to receive R channel read buffer
 * @param out_G Pointer to receive G channel read buffer
 * @param out_B Pointer to receive B channel read buffer
 */
void audio_image_buffers_get_read_pointers(AudioImageBuffers *buffers,
                                           uint8_t **out_R, uint8_t **out_G,
                                           uint8_t **out_B) {
  if (!buffers || !buffers->initialized) {
    fprintf(stderr, "ERROR: AudioImageBuffers not initialized\n");
    return;
  }

  if (!out_R || !out_G || !out_B) {
    fprintf(stderr, "ERROR: Output pointers are NULL\n");
    return;
  }

  // Get current read buffer index atomically (no mutex needed!)
  int read_idx = atomic_load(&buffers->read_buffer_index);

  // Return pointers to the current read buffer
  if (read_idx == 0) {
    *out_R = buffers->buffer0_R;
    *out_G = buffers->buffer0_G;
    *out_B = buffers->buffer0_B;
  } else {
    *out_R = buffers->buffer1_R;
    *out_G = buffers->buffer1_G;
    *out_B = buffers->buffer1_B;
  }

  // Update statistics (note: this is not thread-safe but only for monitoring)
  buffers->lines_processed++;
}

/**
 * @brief Get buffer statistics
 * @param buffers Pointer to AudioImageBuffers structure
 * @param lines_received Pointer to receive lines received count
 * @param lines_processed Pointer to receive lines processed count
 * @param buffer_swaps Pointer to receive buffer swaps count
 */
void audio_image_buffers_get_stats(AudioImageBuffers *buffers,
                                   uint64_t *lines_received,
                                   uint64_t *lines_processed,
                                   uint64_t *buffer_swaps) {
  if (!buffers || !buffers->initialized) {
    if (lines_received)
      *lines_received = 0;
    if (lines_processed)
      *lines_processed = 0;
    if (buffer_swaps)
      *buffer_swaps = 0;
    return;
  }

  if (lines_received)
    *lines_received = buffers->lines_received;
  if (lines_processed)
    *lines_processed = buffers->lines_processed;
  if (buffer_swaps)
    *buffer_swaps = buffers->buffer_swaps;
}
