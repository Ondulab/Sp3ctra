/*
 * synth_additive_threading.h
 *
 * Thread pool management for additive synthesis
 * Contains persistent thread pool and parallel processing functionality
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_THREADING_H__
#define __SYNTH_ADDITIVE_THREADING_H__

/* Includes ------------------------------------------------------------------*/
#include "../../core/config.h"
#include "../../config/config_synth_additive.h"
#include <stdint.h>
#include <pthread.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief  Structure for persistent thread pool worker optimized for synthesis
 */
typedef struct synth_thread_worker_s {
  int thread_id;      // Thread ID (0, 1, 2)
  int start_note;     // Start note for this thread
  int end_note;       // End note for this thread
  int32_t *imageData; // Input image data (shared)

  // Local output buffers per thread - Float32 (legacy)
  float thread_additiveBuffer[AUDIO_BUFFER_SIZE];
  float thread_sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  float thread_maxVolumeBuffer[AUDIO_BUFFER_SIZE];
  
  // Stereo buffers for direct L/R accumulation (always present) - Float32
  // In mono mode: L = R = duplicated signal
  // In stereo mode: L and R with per-oscillator panning
  float thread_additiveBuffer_L[AUDIO_BUFFER_SIZE];
  float thread_additiveBuffer_R[AUDIO_BUFFER_SIZE];

  // Local output buffers per thread - Q24 (new)
  q24_t thread_additiveBuffer_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;
  q24_t thread_sumVolumeBuffer_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;
  q24_t thread_maxVolumeBuffer_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;
  
  // Stereo buffers Q24 (new)
  q24_t thread_additiveBuffer_L_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;
  q24_t thread_additiveBuffer_R_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;

  // Local work buffers (avoids VLA on stack) - Float32
  int32_t imageBuffer_q31[MAX_NUMBER_OF_NOTES / 3 + 100]; // +100 for safety
  float imageBuffer_f32[MAX_NUMBER_OF_NOTES / 3 + 100];
  float waveBuffer[AUDIO_BUFFER_SIZE];
  float volumeBuffer[AUDIO_BUFFER_SIZE];
  
  // Local work buffers - Q24 (new)
  q24_t waveBuffer_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;
  q24_t volumeBuffer_q24[AUDIO_BUFFER_SIZE] Q24_CACHE_ALIGN;

  // Pre-computed waves[] data (read-only)
  int32_t precomputed_new_idx[MAX_NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];
  float precomputed_wave_data[MAX_NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];
  float precomputed_volume[MAX_NUMBER_OF_NOTES / 3 + 100];
  float precomputed_volume_increment[MAX_NUMBER_OF_NOTES / 3 + 100];
  float precomputed_volume_decrement[MAX_NUMBER_OF_NOTES / 3 + 100];
  
  // Pre-computed pan positions and gains for each note
  float precomputed_pan_position[MAX_NUMBER_OF_NOTES / 3 + 100];
  float precomputed_left_gain[MAX_NUMBER_OF_NOTES / 3 + 100];
  float precomputed_right_gain[MAX_NUMBER_OF_NOTES / 3 + 100];

  // Debug capture: per-note per-sample volumes (current and target) for this buffer
  float captured_current_volume[MAX_NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];
  float captured_target_volume[MAX_NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];

  // Synchronization
  pthread_mutex_t work_mutex;
  pthread_cond_t work_cond;
  volatile int work_ready;
  volatile int work_done;

} synth_thread_worker_t;

/* Exported function prototypes ----------------------------------------------*/

/* Thread pool management */
int synth_init_thread_pool(void);
int synth_start_worker_threads(void);
void synth_shutdown_thread_pool(void);

/* Thread processing functions */
void *synth_persistent_worker_thread(void *arg);
void synth_process_worker_range(synth_thread_worker_t *worker);
void synth_process_worker_range_q24(synth_thread_worker_t *worker);
void synth_precompute_wave_data(int32_t *imageData);

/* Thread pool access for synthesis core */
extern synth_thread_worker_t thread_pool[3];
extern volatile int synth_pool_initialized;
extern volatile int synth_pool_shutdown;

#endif /* __SYNTH_ADDITIVE_THREADING_H__ */
