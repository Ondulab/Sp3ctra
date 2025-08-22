#ifndef AUDIO_IMAGE_BUFFERS_H
#define AUDIO_IMAGE_BUFFERS_H

#include "config.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

/**
 * @brief Dual buffer system for audio-image data with atomic rotation
 *
 * This system provides continuous access to complete image lines for IFFT
 * synthesis while allowing UDP thread to write new data without blocking audio
 * processing.
 *
 * Key features:
 * - Separate R, G, B memory areas for stereo effects
 * - Atomic buffer rotation (no mutex on read side)
 * - Real-time audio processing guaranteed
 * - Graceful handling of scanner disconnection
 */
typedef struct AudioImageBuffers {
  // Buffer 0 - RGB channels separated for memory contiguity
  uint8_t *buffer0_R;
  uint8_t *buffer0_G;
  uint8_t *buffer0_B;

  // Buffer 1 - RGB channels separated for memory contiguity
  uint8_t *buffer1_R;
  uint8_t *buffer1_G;
  uint8_t *buffer1_B;

  // Atomic buffer selection (0 or 1)
  // - read_buffer_index: which buffer IFFT should read from
  // - write_buffer_index: which buffer UDP should write to
  atomic_int read_buffer_index;
  atomic_int write_buffer_index;

  // Write protection mutex (only for UDP thread)
  pthread_mutex_t write_mutex;

  // Statistics and monitoring
  uint64_t lines_received;
  uint64_t lines_processed;
  uint64_t buffer_swaps;

  // Initialization flag
  uint8_t initialized;

} AudioImageBuffers;

// Function prototypes
int audio_image_buffers_init(AudioImageBuffers *buffers);
void audio_image_buffers_cleanup(AudioImageBuffers *buffers);

// UDP thread functions (with write protection)
int audio_image_buffers_start_write(AudioImageBuffers *buffers, uint8_t **out_R,
                                    uint8_t **out_G, uint8_t **out_B);
void audio_image_buffers_complete_write(AudioImageBuffers *buffers);

// IFFT thread functions (lock-free read)
void audio_image_buffers_get_read_pointers(AudioImageBuffers *buffers,
                                           uint8_t **out_R, uint8_t **out_G,
                                           uint8_t **out_B);

// Utility functions
void audio_image_buffers_get_stats(AudioImageBuffers *buffers,
                                   uint64_t *lines_received,
                                   uint64_t *lines_processed,
                                   uint64_t *buffer_swaps);

#endif /* AUDIO_IMAGE_BUFFERS_H */
