/*
 * synth_additive_threading.c
 *
 * Thread pool management for additive synthesis
 * Contains persistent thread pool and parallel processing functionality
 *
 * Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_threading.h"
#include "synth_additive_algorithms.h"
#include "synth_additive_math.h"
#include "wave_generation.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../config/config_debug.h"
#include "../../utils/image_debug.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef __linux__
#include <sched.h>
#endif

/* External declarations -----------------------------------------------------*/
#ifdef DEBUG_OSC
extern debug_additive_osc_config_t g_debug_osc_config;
#endif

/* Global variables ----------------------------------------------------------*/

// Pool of persistent threads
synth_thread_worker_t thread_pool[3];
static pthread_t worker_threads[3];
volatile int synth_pool_initialized = 0;
volatile int synth_pool_shutdown = 0;

// Global mutex to protect access to waves[] data during pre-computation
static pthread_mutex_t waves_global_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Private function implementations ------------------------------------------*/

/**
 * @brief  Initialize the persistent thread pool
 * @retval 0 on success, -1 on error
 */
int synth_init_thread_pool(void) {
  if (synth_pool_initialized)
    return 0;

  int current_notes = get_current_number_of_notes();
  int notes_per_thread = current_notes / 3;

  for (int i = 0; i < 3; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    // Worker configuration
    worker->thread_id = i;
    worker->start_note = i * notes_per_thread;
    worker->end_note = (i == 2) ? current_notes : (i + 1) * notes_per_thread;
    worker->work_ready = 0;
    worker->work_done = 0;

    // CRITICAL FIX: Initialize all buffers to zero to prevent garbage values
    memset(worker->thread_additiveBuffer, 0, sizeof(worker->thread_additiveBuffer));
    memset(worker->thread_sumVolumeBuffer, 0, sizeof(worker->thread_sumVolumeBuffer));
    memset(worker->thread_maxVolumeBuffer, 0, sizeof(worker->thread_maxVolumeBuffer));
    
    // CRITICAL FIX: Initialize stereo buffers to zero (always present)
    memset(worker->thread_additiveBuffer_L, 0, sizeof(worker->thread_additiveBuffer_L));
    memset(worker->thread_additiveBuffer_R, 0, sizeof(worker->thread_additiveBuffer_R));
    
    // Initialize Q24 buffers to zero
    memset(worker->thread_additiveBuffer_q24, 0, sizeof(worker->thread_additiveBuffer_q24));
    memset(worker->thread_sumVolumeBuffer_q24, 0, sizeof(worker->thread_sumVolumeBuffer_q24));
    memset(worker->thread_maxVolumeBuffer_q24, 0, sizeof(worker->thread_maxVolumeBuffer_q24));
    memset(worker->thread_additiveBuffer_L_q24, 0, sizeof(worker->thread_additiveBuffer_L_q24));
    memset(worker->thread_additiveBuffer_R_q24, 0, sizeof(worker->thread_additiveBuffer_R_q24));

    // Initialize work buffers
    memset(worker->imageBuffer_q31, 0, sizeof(worker->imageBuffer_q31));
    memset(worker->imageBuffer_f32, 0, sizeof(worker->imageBuffer_f32));
    memset(worker->waveBuffer, 0, sizeof(worker->waveBuffer));
    memset(worker->volumeBuffer, 0, sizeof(worker->volumeBuffer));
    
    // Initialize Q24 work buffers
    memset(worker->waveBuffer_q24, 0, sizeof(worker->waveBuffer_q24));
    memset(worker->volumeBuffer_q24, 0, sizeof(worker->volumeBuffer_q24));
    
    // Initialize precomputed data arrays
    memset(worker->precomputed_new_idx, 0, sizeof(worker->precomputed_new_idx));
    memset(worker->precomputed_wave_data, 0, sizeof(worker->precomputed_wave_data));
    memset(worker->precomputed_volume, 0, sizeof(worker->precomputed_volume));
    memset(worker->precomputed_volume_increment, 0, sizeof(worker->precomputed_volume_increment));
    memset(worker->precomputed_volume_decrement, 0, sizeof(worker->precomputed_volume_decrement));
    
    memset(worker->precomputed_pan_position, 0, sizeof(worker->precomputed_pan_position));
    memset(worker->precomputed_left_gain, 0, sizeof(worker->precomputed_left_gain));
    memset(worker->precomputed_right_gain, 0, sizeof(worker->precomputed_right_gain));

    // Initialize synchronization
    if (pthread_mutex_init(&worker->work_mutex, NULL) != 0) {
      printf("Error initializing mutex for thread %d\n", i);
      return -1;
    }
    if (pthread_cond_init(&worker->work_cond, NULL) != 0) {
      printf("Error initializing condition for thread %d\n", i);
      return -1;
    }
  }

  synth_pool_initialized = 1;
  return 0;
}

/**
 * @brief  Main function for persistent worker threads
 * @param  arg Pointer to synth_thread_worker_t structure
 * @retval NULL pointer
 */
void *synth_persistent_worker_thread(void *arg) {
  synth_thread_worker_t *worker = (synth_thread_worker_t *)arg;

  while (!synth_pool_shutdown) {
    // Wait for work
    pthread_mutex_lock(&worker->work_mutex);
    while (!worker->work_ready && !synth_pool_shutdown) {
      pthread_cond_wait(&worker->work_cond, &worker->work_mutex);
    }
    pthread_mutex_unlock(&worker->work_mutex);

    if (synth_pool_shutdown)
      break;

    // Perform the work - select Q24 or Float32 based on configuration
#if ENABLE_Q24_PROCESSING
    synth_process_worker_range_q24(worker);
#else
    synth_process_worker_range(worker);
#endif

    // Signal that work is done
    pthread_mutex_lock(&worker->work_mutex);
    worker->work_done = 1;
    worker->work_ready = 0;
    pthread_mutex_unlock(&worker->work_mutex);
  }

  return NULL;
}

/**
 * @brief  Process a range of notes for a given worker (Q24 version)
 * @param  worker Pointer to worker structure
 * @retval None
 */
void synth_process_worker_range_q24(synth_thread_worker_t *worker) {
  int32_t buff_idx, note, local_note_idx;
  static int q24_logged = 0;
  /* DEBUG DISABLED
  if (!q24_logged) {
    printf("[Q24 WORKER] Using Q24 path in workers\n");
    q24_logged = 1;
  }
  */
  int nz_notes = 0;  // count notes with non-zero target volume this cycle

  // Initialize Q24 output buffers to zero
  fill_q24(0, worker->thread_additiveBuffer_q24, AUDIO_BUFFER_SIZE);
  fill_q24(0, worker->thread_sumVolumeBuffer_q24, AUDIO_BUFFER_SIZE);
  fill_q24(0, worker->thread_maxVolumeBuffer_q24, AUDIO_BUFFER_SIZE);
  
  // Initialize Q24 stereo buffers
  fill_q24(0, worker->thread_additiveBuffer_L_q24, AUDIO_BUFFER_SIZE);
  fill_q24(0, worker->thread_additiveBuffer_R_q24, AUDIO_BUFFER_SIZE);

  // Initialize corresponding FLOAT accumulators (to avoid Q24 saturation during sums)
  fill_float(0.0f, worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0.0f, worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);

  // Use centralized preprocessing algorithm (still int32)
  process_image_preprocessing(worker->imageData, worker->imageBuffer_q31, 
                             worker->start_note, worker->end_note);

  // Use centralized RELATIVE_MODE algorithm (still int32)
  apply_relative_mode(worker->imageBuffer_q31, worker->start_note, worker->end_note);

  // Main note processing - Q24 version
  for (note = worker->start_note; note < worker->end_note; note++) {
    local_note_idx = note - worker->start_note;
    
    // Convert image data to Q24 target volume
    float target_volume_f = (float)worker->imageBuffer_q31[local_note_idx];
    if (target_volume_f > 0.0f) nz_notes++;
    
    // Apply gamma mapping to float version
    apply_gamma_mapping(&target_volume_f, 1);
    
    // Convert to Q24 target volume
    q24_t target_volume_q24 = FLOAT_TO_Q24(target_volume_f / (float)VOLUME_AMP_RESOLUTION);

    // Generate Q24 waveform samples directly from Q24 table
    for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      int32_t new_idx = (waves[note].current_idx + waves[note].octave_coeff);
      if ((uint32_t)new_idx >= waves[note].area_size) {
        new_idx -= waves[note].area_size;
      }
      worker->waveBuffer_q24[buff_idx] = *(waves[note].start_ptr_q24 + new_idx);
      waves[note].current_idx = new_idx;
    }

    // Apply Q24 GAP_LIMITER algorithm
    apply_gap_limiter_ramp_q24(note, target_volume_q24, worker->volumeBuffer_q24);

    // Apply Q24 volume scaling to waveform
    mult_q24(worker->waveBuffer_q24, worker->volumeBuffer_q24, worker->waveBuffer_q24, AUDIO_BUFFER_SIZE);

    // Accumulate into FLOAT buffers to avoid Q24 saturation during summations
    // 1) Convert per-sample volumes (Q24) to float and accumulate
    q24_to_float_array(worker->volumeBuffer_q24, worker->volumeBuffer, AUDIO_BUFFER_SIZE);
    add_float(worker->volumeBuffer, worker->thread_sumVolumeBuffer,
              worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);

    // 2) Convert wave samples (post-volume) to float and accumulate
    q24_to_float_array(worker->waveBuffer_q24, worker->waveBuffer, AUDIO_BUFFER_SIZE);
    add_float(worker->waveBuffer, worker->thread_additiveBuffer,
              worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);

    /* DEBUG DISABLED: per-sample worker trace */

    // Update max volume buffer (Q24)
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      if (worker->volumeBuffer_q24[buff_idx] > worker->thread_maxVolumeBuffer_q24[buff_idx]) {
        worker->thread_maxVolumeBuffer_q24[buff_idx] = worker->volumeBuffer_q24[buff_idx];
      }
    }

    // Stereo processing (Q24)
#ifdef STEREO_MODE
    // Get Q24 pan gains
    q24_t left_gain_q24 = FLOAT_TO_Q24(worker->precomputed_left_gain[local_note_idx]);
    q24_t right_gain_q24 = FLOAT_TO_Q24(worker->precomputed_right_gain[local_note_idx]);
    
    // Apply panning gains and accumulate to stereo buffers
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      q24_t left_sample = Q24_MULTIPLY(worker->waveBuffer_q24[buff_idx], left_gain_q24);
      q24_t right_sample = Q24_MULTIPLY(worker->waveBuffer_q24[buff_idx], right_gain_q24);
      
      // Accumulate with overflow protection
      int64_t temp_L = (int64_t)worker->thread_additiveBuffer_L_q24[buff_idx] + left_sample;
      int64_t temp_R = (int64_t)worker->thread_additiveBuffer_R_q24[buff_idx] + right_sample;
      
      worker->thread_additiveBuffer_L_q24[buff_idx] = (temp_L > Q24_MAX) ? Q24_MAX : 
                                                      (temp_L < Q24_MIN) ? Q24_MIN : (q24_t)temp_L;
      worker->thread_additiveBuffer_R_q24[buff_idx] = (temp_R > Q24_MAX) ? Q24_MAX : 
                                                      (temp_R < Q24_MIN) ? Q24_MIN : (q24_t)temp_R;
    }
#else
    // Mono mode: duplicate to both channels
    add_q24(worker->waveBuffer_q24, worker->thread_additiveBuffer_L_q24,
            worker->thread_additiveBuffer_L_q24, AUDIO_BUFFER_SIZE);
    add_q24(worker->waveBuffer_q24, worker->thread_additiveBuffer_R_q24,
            worker->thread_additiveBuffer_R_q24, AUDIO_BUFFER_SIZE);
#endif

    // Additive summation (Q24)
    add_q24(worker->waveBuffer_q24, worker->thread_additiveBuffer_q24,
            worker->thread_additiveBuffer_q24, AUDIO_BUFFER_SIZE);
    
    // Volume summation (Q24)
    add_q24(worker->volumeBuffer_q24, worker->thread_sumVolumeBuffer_q24,
            worker->thread_sumVolumeBuffer_q24, AUDIO_BUFFER_SIZE);
  }

  /* DEBUG DISABLED: worker-level summary */
}

/**
 * @brief  Process a range of notes for a given worker (Float32 version)
 * @param  worker Pointer to worker structure
 * @retval None
 */
void synth_process_worker_range(synth_thread_worker_t *worker) {
  int32_t buff_idx, note, local_note_idx;
  static int f32_logged = 0;
  if (!f32_logged) {
    printf("[Q24 WORKER] Using legacy Float32 path in workers\n");
    f32_logged = 1;
  }

  // Initialize output buffers to zero
  fill_float(0, worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_maxVolumeBuffer, AUDIO_BUFFER_SIZE);
  
  // Initialize stereo buffers - CRITICAL FIX: must zero these buffers! (always present)
  fill_float(0, worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);

  // Use centralized preprocessing algorithm
  process_image_preprocessing(worker->imageData, worker->imageBuffer_q31, 
                             worker->start_note, worker->end_note);

  // Use centralized RELATIVE_MODE algorithm
  apply_relative_mode(worker->imageBuffer_q31, worker->start_note, worker->end_note);

    // Main note processing
    for (note = worker->start_note; note < worker->end_note; note++) {
        local_note_idx = note - worker->start_note;
        worker->imageBuffer_f32[local_note_idx] =
            (float)worker->imageBuffer_q31[local_note_idx];

        // Use centralized gamma mapping algorithm
        apply_gamma_mapping(&worker->imageBuffer_f32[local_note_idx], 1);

        // Use centralized waveform generation algorithm
        generate_waveform_samples(note, worker->waveBuffer, 
                                 worker->precomputed_wave_data[local_note_idx]);

        // Use centralized GAP_LIMITER algorithm
        apply_gap_limiter_ramp(note, worker->imageBuffer_f32[local_note_idx], 
                              worker->volumeBuffer);

        // Debug capture: copy per-sample volumes for this note into worker buffers (fast path)
        memcpy(&worker->captured_current_volume[local_note_idx][0], worker->volumeBuffer, sizeof(float) * AUDIO_BUFFER_SIZE);
        fill_float(waves[note].target_volume, &worker->captured_target_volume[local_note_idx][0], AUDIO_BUFFER_SIZE);

    // Apply volume scaling to the current note waveform
    mult_float(worker->waveBuffer, worker->volumeBuffer, worker->waveBuffer,
               AUDIO_BUFFER_SIZE);

    for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;) {
      if (worker->volumeBuffer[buff_idx] >
          worker->thread_maxVolumeBuffer[buff_idx]) {
        worker->thread_maxVolumeBuffer[buff_idx] =
            worker->volumeBuffer[buff_idx];
      }
    }

    // Always fill stereo buffers (unified approach)
#ifdef STEREO_MODE
    // Stereo mode: Apply per-oscillator panning and accumulate to L/R buffers
    float left_gain = worker->precomputed_left_gain[local_note_idx];
    float right_gain = worker->precomputed_right_gain[local_note_idx];
    
    // Create temporary buffers for L/R channels
    float waveBuffer_L[AUDIO_BUFFER_SIZE];
    float waveBuffer_R[AUDIO_BUFFER_SIZE];
    
    // Apply panning gains
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      waveBuffer_L[buff_idx] = worker->waveBuffer[buff_idx] * left_gain;
      waveBuffer_R[buff_idx] = worker->waveBuffer[buff_idx] * right_gain;
    }
    
    // Accumulate to stereo buffers
    add_float(waveBuffer_L, worker->thread_additiveBuffer_L,
              worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
    add_float(waveBuffer_R, worker->thread_additiveBuffer_R,
              worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);
#else
    // Mono mode: Duplicate mono signal to both L/R channels (center panning)
    // This creates a unified architecture where stereo buffers are always filled
    add_float(worker->waveBuffer, worker->thread_additiveBuffer_L,
              worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
    add_float(worker->waveBuffer, worker->thread_additiveBuffer_R,
              worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);
#endif

    // Additive summation for mono or combined processing
    add_float(worker->waveBuffer, worker->thread_additiveBuffer,
              worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);
    // Volume summation (local to thread)
    add_float(worker->volumeBuffer, worker->thread_sumVolumeBuffer,
              worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);

    // Commit phase continuity: set waves[note].current_idx to the last precomputed index for this buffer
    waves[note].current_idx = worker->precomputed_new_idx[local_note_idx][AUDIO_BUFFER_SIZE - 1];
  }
}

/**
 * @brief  Pre-compute waves[] data in parallel to avoid contention
 * @param  imageData Input image data
 * @retval None
 */
void synth_precompute_wave_data(int32_t *imageData) {
  // ✅ OPTIMIZATION: Parallelized pre-computation to balance CPU load

  // Phase 1: Image data assignment (thread-safe, read-only)
  for (int i = 0; i < 3; i++) {
    thread_pool[i].imageData = imageData;
  }

  // Phase 2: Parallel pre-computation of waves[] data by ranges
  pthread_mutex_lock(&waves_global_mutex);

  // Use workers to pre-compute in parallel
  for (int i = 0; i < 3; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    for (int note = worker->start_note; note < worker->end_note; note++) {
      int local_note_idx = note - worker->start_note;

      // Pre-compute waveform data
      // Preserve phase continuity: compute indices locally, do not write back to waves[].current_idx here
      uint32_t cur_idx = waves[note].current_idx;
      for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        int32_t new_idx = (cur_idx + waves[note].octave_coeff);
        if ((uint32_t)new_idx >= waves[note].area_size) {
          new_idx -= waves[note].area_size;
        }

        worker->precomputed_new_idx[local_note_idx][buff_idx] = new_idx;
        worker->precomputed_wave_data[local_note_idx][buff_idx] =
            (*(waves[note].start_ptr + new_idx));
        cur_idx = (uint32_t)new_idx;
      }
      // Workers will commit the last index per note after processing using the precomputed indices

#ifdef GAP_LIMITER
      // ✅ GAP_LIMITER: Don't pre-compute volume - threads access it directly
      // The increment/decrement parameters are thread-safe in read-only mode
      worker->precomputed_volume_increment[local_note_idx] =
          waves[note].volume_increment;
      worker->precomputed_volume_decrement[local_note_idx] =
          waves[note].volume_decrement;
#endif

#ifdef STEREO_MODE
      // Use lock-free pan system to get current gains
      float left_gain, right_gain, pan_position;
      lock_free_pan_read(note, &left_gain, &right_gain, &pan_position);
      
      worker->precomputed_pan_position[local_note_idx] = pan_position;
      worker->precomputed_left_gain[local_note_idx] = left_gain;
      worker->precomputed_right_gain[local_note_idx] = right_gain;
#endif
    }
  }

  pthread_mutex_unlock(&waves_global_mutex);
}

/**
 * @brief  Start worker threads with CPU affinity
 * @retval 0 on success, -1 on error
 */
int synth_start_worker_threads(void) {
  for (int i = 0; i < 3; i++) {
    if (pthread_create(&worker_threads[i], NULL, synth_persistent_worker_thread,
                       &thread_pool[i]) != 0) {
      printf("Error creating worker thread %d\n", i);
      return -1;
    }

    // ✅ OPTIMIZATION: CPU affinity to balance load on Pi5
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Distribute threads on CPUs 1, 2, 3 (leave CPU 0 for system)
    CPU_SET(i + 1, &cpuset);

    int result =
        pthread_setaffinity_np(worker_threads[i], sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
      printf("Worker thread %d assigned to CPU %d\n", i, i + 1);
    } else {
      printf("Cannot assign thread %d to CPU %d (error: %d)\n", i,
             i + 1, result);
    }
#endif
  }
  return 0;
}

/**
 * @brief  Stop the persistent thread pool
 * @retval None
 */
void synth_shutdown_thread_pool(void) {
  if (!synth_pool_initialized)
    return;

  synth_pool_shutdown = 1;

  // Wake up all threads
  for (int i = 0; i < 3; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    pthread_cond_signal(&thread_pool[i].work_cond);
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }

  // Wait for all threads to terminate
  for (int i = 0; i < 3; i++) {
    pthread_join(worker_threads[i], NULL);
    pthread_mutex_destroy(&thread_pool[i].work_mutex);
    pthread_cond_destroy(&thread_pool[i].work_cond);
  }

#ifdef STEREO_MODE
  // Cleanup lock-free pan gains system
  lock_free_pan_cleanup();
  printf("🔧 LOCK_FREE_PAN: System cleaned up\n");
#endif

  synth_pool_initialized = 0;
}
