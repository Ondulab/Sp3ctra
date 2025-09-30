/*
 * synth_additive_threading.c
 *
 * Thread pool management for additive synthesis
 * Contains persistent thread pool and parallel processing functionality
 *
 * Author: zhonx
 */

#define _GNU_SOURCE  // Required for CPU_ZERO, CPU_SET, pthread_setaffinity_np

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_threading.h"
#include "synth_additive_algorithms.h"
#include "synth_additive_math.h"
#include "pow_approx.h"
#include "wave_generation.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../config/config_debug.h"
#include "../../config/config_loader.h"
#include "../../utils/image_debug.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>

#ifdef __linux__
#include <sched.h>
#endif

// Runtime-gated capture buffers: lazy allocation only when capture is enabled
static inline int synth_ensure_capture_buffers(synth_thread_worker_t *worker) {
  // Fast path: if capture disabled, do nothing
  if (!image_debug_is_oscillator_capture_enabled()) {
    return 0;
  }

  int buf = g_sp3ctra_config.audio_buffer_size;
  int notes_this = worker->end_note - worker->start_note;
  if (buf <= 0 || notes_this <= 0) return -1;

  size_t total = (size_t)buf * (size_t)notes_this;

  // If already allocated with the correct capacity, nothing to do
  if (worker->captured_current_volume && worker->captured_target_volume &&
      worker->capture_capacity_elements == total) {
    return 0;
  }

  // (Re)allocate with new capacity
  if (worker->captured_current_volume) {
    free(worker->captured_current_volume);
    worker->captured_current_volume = NULL;
  }
  if (worker->captured_target_volume) {
    free(worker->captured_target_volume);
    worker->captured_target_volume = NULL;
  }

  worker->captured_current_volume = (float *)calloc(total, sizeof(float));
  worker->captured_target_volume  = (float *)calloc(total, sizeof(float));
  if (!worker->captured_current_volume || !worker->captured_target_volume) {
    // Cleanup on partial failure
    if (worker->captured_current_volume) { free(worker->captured_current_volume); worker->captured_current_volume = NULL; }
    if (worker->captured_target_volume)  { free(worker->captured_target_volume);  worker->captured_target_volume  = NULL; }
    worker->capture_capacity_elements = 0;
    return -1;
  }
  worker->capture_capacity_elements = total;
  return 0;
}

// If capture is disabled at runtime, immediately release capture buffers to free memory
static inline void synth_release_capture_buffers_if_disabled(synth_thread_worker_t *worker) {
  if (worker->capture_capacity_elements && !image_debug_is_oscillator_capture_enabled()) {
    if (worker->captured_current_volume) { free(worker->captured_current_volume); worker->captured_current_volume = NULL; }
    if (worker->captured_target_volume)  { free(worker->captured_target_volume);  worker->captured_target_volume  = NULL; }
    worker->capture_capacity_elements = 0;
  }
}

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

/* RT-safe double buffering system */
rt_safe_buffer_t g_rt_additive_buffer = {0};
rt_safe_buffer_t g_rt_stereo_L_buffer = {0};  
rt_safe_buffer_t g_rt_stereo_R_buffer = {0};

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
    {
      int buf = g_sp3ctra_config.audio_buffer_size;
      int notes_this = worker->end_note - worker->start_note;
      
      // Allocate dynamic buffers
      worker->thread_additiveBuffer = (float*)calloc(buf, sizeof(float));
      worker->thread_sumVolumeBuffer = (float*)calloc(buf, sizeof(float));
      worker->thread_maxVolumeBuffer = (float*)calloc(buf, sizeof(float));
      worker->thread_additiveBuffer_L = (float*)calloc(buf, sizeof(float));
      worker->thread_additiveBuffer_R = (float*)calloc(buf, sizeof(float));
      worker->waveBuffer = (float*)calloc(buf, sizeof(float));
      worker->volumeBuffer = (float*)calloc(buf, sizeof(float));
      
      // Work buffers (dynamically sized based on notes_per_thread)
      worker->imageBuffer_q31 = (int32_t*)calloc(notes_this, sizeof(int32_t));
      worker->imageBuffer_f32 = (float*)calloc(notes_this, sizeof(float));
      
      // Precomputed arrays per note x per sample
      size_t total = (size_t)notes_this * (size_t)buf;
      worker->precomputed_new_idx = (int32_t*)calloc(total, sizeof(int32_t));
      worker->precomputed_wave_data = (float*)calloc(total, sizeof(float));
      
      // Precomputed volume and pan data (per note)
      worker->precomputed_volume = (float*)calloc(notes_this, sizeof(float));
      worker->precomputed_pan_position = (float*)calloc(notes_this, sizeof(float));
      worker->precomputed_left_gain = (float*)calloc(notes_this, sizeof(float));
      worker->precomputed_right_gain = (float*)calloc(notes_this, sizeof(float));
      
      // Last applied gains for ramping (per note)
      worker->last_left_gain = (float*)calloc(notes_this, sizeof(float));
      worker->last_right_gain = (float*)calloc(notes_this, sizeof(float));
      
      // Capture buffers are now lazy-allocated only when capture is enabled
      worker->captured_current_volume = NULL;
      worker->captured_target_volume = NULL;
      worker->capture_capacity_elements = 0;
      
      // Persist stereo temp buffers to avoid VLA on worker stack
      worker->temp_waveBuffer_L = (float*)calloc(buf, sizeof(float));
      worker->temp_waveBuffer_R = (float*)calloc(buf, sizeof(float));
      
      // Check all allocations
      if (!worker->thread_additiveBuffer || !worker->thread_sumVolumeBuffer || !worker->thread_maxVolumeBuffer ||
          !worker->thread_additiveBuffer_L || !worker->thread_additiveBuffer_R || !worker->waveBuffer || !worker->volumeBuffer ||
          !worker->imageBuffer_q31 || !worker->imageBuffer_f32 ||
          !worker->precomputed_new_idx || !worker->precomputed_wave_data || !worker->precomputed_volume ||
          !worker->precomputed_pan_position || !worker->precomputed_left_gain || !worker->precomputed_right_gain ||
          !worker->last_left_gain || !worker->last_right_gain ||
          !worker->temp_waveBuffer_L || !worker->temp_waveBuffer_R) {
        printf("Error allocating worker buffers for thread %d\n", i);
        return -1;
      }
      
      // Initialize last pan gains for per-buffer ramping (center equal-power)
      for (int idx = 0; idx < notes_this; idx++) {
        worker->last_left_gain[idx] = 0.707f;
        worker->last_right_gain[idx] = 0.707f;
      }
    }

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

    // Perform the work (Float32 path)
    synth_process_worker_range(worker);

    // Signal that work is done
    pthread_mutex_lock(&worker->work_mutex);
    worker->work_done = 1;
    worker->work_ready = 0;
    pthread_mutex_unlock(&worker->work_mutex);
  }

  return NULL;
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
    printf("[Float32 WORKER] Using Float32 path in workers\n");
    f32_logged = 1;
  }

  // Release capture buffers if capture was disabled since last buffer
  synth_release_capture_buffers_if_disabled(worker);

  // Initialize output buffers to zero
  fill_float(0, worker->thread_additiveBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_sumVolumeBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_maxVolumeBuffer, g_sp3ctra_config.audio_buffer_size);

  // Initialize stereo buffers - CRITICAL FIX: must zero these buffers! (always present)
  fill_float(0, worker->thread_additiveBuffer_L, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_additiveBuffer_R, g_sp3ctra_config.audio_buffer_size);

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
        const float* pre_wave = worker->precomputed_wave_data + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size;
        generate_waveform_samples(note, worker->waveBuffer, pre_wave);

        // Use centralized GAP_LIMITER algorithm (phase-weighted; pass precomputed waveform)
        apply_gap_limiter_ramp(note,
                               worker->imageBuffer_f32[local_note_idx],
                               pre_wave,
                               worker->volumeBuffer);

        // Debug capture: copy per-sample volumes for this note into worker buffers (fast path)
        // Runtime-gated capture: allocate lazily and copy only if enabled
        if (image_debug_is_oscillator_capture_enabled()) {
          if (synth_ensure_capture_buffers(worker) == 0) {
            memcpy(worker->captured_current_volume + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size,
                   worker->volumeBuffer,
                   sizeof(float) * (size_t)g_sp3ctra_config.audio_buffer_size);
            fill_float(waves[note].target_volume,
                       worker->captured_target_volume + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size,
                       (size_t)g_sp3ctra_config.audio_buffer_size);
          }
        }

    // Apply volume scaling to the current note waveform
    mult_float(worker->waveBuffer, worker->volumeBuffer, worker->waveBuffer,
               g_sp3ctra_config.audio_buffer_size);

    for (buff_idx = g_sp3ctra_config.audio_buffer_size; --buff_idx >= 0;) {
      if (worker->volumeBuffer[buff_idx] >
          worker->thread_maxVolumeBuffer[buff_idx]) {
        worker->thread_maxVolumeBuffer[buff_idx] =
            worker->volumeBuffer[buff_idx];
      }
    }

    // Always fill stereo buffers (unified approach)
    if (g_sp3ctra_config.stereo_mode_enabled) {
      // Stereo mode: Apply per-oscillator panning with per-buffer ramp (zipper-noise mitigation)
      const float start_left  = worker->last_left_gain[local_note_idx];
      const float start_right = worker->last_right_gain[local_note_idx];
      const float end_left    = worker->precomputed_left_gain[local_note_idx];
      const float end_right   = worker->precomputed_right_gain[local_note_idx];

      // Use persistent temporary buffers for L/R channels (no VLA on stack)
      float *waveBuffer_L = worker->temp_waveBuffer_L;
      float *waveBuffer_R = worker->temp_waveBuffer_R;

      // Linear interpolation across this audio buffer to avoid abrupt pan jumps
      const float step = 1.0f / (float)g_sp3ctra_config.audio_buffer_size;
      float t = 0.0f;
      for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        t += step; // ramp from start -> end across the buffer
        float gl = start_left  + (end_left  - start_left)  * t;
        float gr = start_right + (end_right - start_right) * t;

        // Apply interpolated panning gains
        waveBuffer_L[buff_idx] = worker->waveBuffer[buff_idx] * gl;
        waveBuffer_R[buff_idx] = worker->waveBuffer[buff_idx] * gr;
      }

      // Persist end-gains for next buffer ramp
      worker->last_left_gain[local_note_idx]  = end_left;
      worker->last_right_gain[local_note_idx] = end_right;

      // Accumulate to stereo buffers
      add_float(waveBuffer_L, worker->thread_additiveBuffer_L,
                worker->thread_additiveBuffer_L, g_sp3ctra_config.audio_buffer_size);
      add_float(waveBuffer_R, worker->thread_additiveBuffer_R,
                worker->thread_additiveBuffer_R, g_sp3ctra_config.audio_buffer_size);
    } else {
      // Mono mode: Duplicate mono signal to both L/R channels (center panning)
      // This creates a unified architecture where stereo buffers are always filled
      add_float(worker->waveBuffer, worker->thread_additiveBuffer_L,
                worker->thread_additiveBuffer_L, g_sp3ctra_config.audio_buffer_size);
      add_float(worker->waveBuffer, worker->thread_additiveBuffer_R,
                worker->thread_additiveBuffer_R, g_sp3ctra_config.audio_buffer_size);
    }

    // Additive summation for mono or combined processing
    add_float(worker->waveBuffer, worker->thread_additiveBuffer,
              worker->thread_additiveBuffer, g_sp3ctra_config.audio_buffer_size);
    // Intelligent volume weighting: strong oscillators dominate over weak background noise
    for (int buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        float current_volume = worker->volumeBuffer[buff_idx];
        float volume_normalized = current_volume / (float)VOLUME_AMP_RESOLUTION;
        float weighted_volume = pow_unit_fast(volume_normalized, g_sp3ctra_config.volume_weighting_exponent) * (float)VOLUME_AMP_RESOLUTION;
        worker->thread_sumVolumeBuffer[buff_idx] += weighted_volume;
    }

    // Commit phase continuity: set waves[note].current_idx to the last precomputed index for this buffer
    waves[note].current_idx = *(worker->precomputed_new_idx + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size + (g_sp3ctra_config.audio_buffer_size - 1));
  }

  // NOTE: RT-safe buffer writing removed - causes audio corruption
  // Workers only write to their local buffers, main thread combines them
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
      int32_t* pre_idx_base = worker->precomputed_new_idx + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size;
      float* pre_wave_base = worker->precomputed_wave_data + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size;

      // Pre-compute waveform data
      // Preserve phase continuity: compute indices locally, do not write back to waves[].current_idx here
      uint32_t cur_idx = waves[note].current_idx;
      for (int buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        int32_t new_idx = (cur_idx + waves[note].octave_coeff);
        if ((uint32_t)new_idx >= waves[note].area_size) {
          new_idx -= waves[note].area_size;
        }

        pre_idx_base[buff_idx] = new_idx;
        pre_wave_base[buff_idx] =
            (*(waves[note].start_ptr + new_idx));
        cur_idx = (uint32_t)new_idx;
      }
      // Workers will commit the last index per note after processing using the precomputed indices

#ifdef GAP_LIMITER
      // ✅ GAP_LIMITER: Don't pre-compute volume - threads access it directly
      // Volume parameters are now handled by tau_up_base_ms/tau_down_base_ms system
#endif

      if (g_sp3ctra_config.stereo_mode_enabled) {
        // Use lock-free pan system to get current gains
        float left_gain, right_gain, pan_position;
        lock_free_pan_read(note, &left_gain, &right_gain, &pan_position);
        
        worker->precomputed_pan_position[local_note_idx] = pan_position;
        worker->precomputed_left_gain[local_note_idx] = left_gain;
        worker->precomputed_right_gain[local_note_idx] = right_gain;
      }
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

    // Free dynamically allocated worker buffers
    free(thread_pool[i].thread_additiveBuffer);    thread_pool[i].thread_additiveBuffer = NULL;
    free(thread_pool[i].thread_sumVolumeBuffer);   thread_pool[i].thread_sumVolumeBuffer = NULL;
    free(thread_pool[i].thread_maxVolumeBuffer);   thread_pool[i].thread_maxVolumeBuffer = NULL;
    free(thread_pool[i].thread_additiveBuffer_L);  thread_pool[i].thread_additiveBuffer_L = NULL;
    free(thread_pool[i].thread_additiveBuffer_R);  thread_pool[i].thread_additiveBuffer_R = NULL;
    free(thread_pool[i].waveBuffer);               thread_pool[i].waveBuffer = NULL;
    free(thread_pool[i].volumeBuffer);             thread_pool[i].volumeBuffer = NULL;
    free(thread_pool[i].imageBuffer_q31);          thread_pool[i].imageBuffer_q31 = NULL;
    free(thread_pool[i].imageBuffer_f32);          thread_pool[i].imageBuffer_f32 = NULL;
    free(thread_pool[i].precomputed_new_idx);      thread_pool[i].precomputed_new_idx = NULL;
    free(thread_pool[i].precomputed_wave_data);    thread_pool[i].precomputed_wave_data = NULL;
    free(thread_pool[i].precomputed_volume);       thread_pool[i].precomputed_volume = NULL;
    free(thread_pool[i].precomputed_pan_position); thread_pool[i].precomputed_pan_position = NULL;
    free(thread_pool[i].precomputed_left_gain);    thread_pool[i].precomputed_left_gain = NULL;
    free(thread_pool[i].precomputed_right_gain);   thread_pool[i].precomputed_right_gain = NULL;
    free(thread_pool[i].last_left_gain);           thread_pool[i].last_left_gain = NULL;
    free(thread_pool[i].last_right_gain);          thread_pool[i].last_right_gain = NULL;
    free(thread_pool[i].captured_current_volume);  thread_pool[i].captured_current_volume = NULL;
    free(thread_pool[i].captured_target_volume);   thread_pool[i].captured_target_volume = NULL;
    free(thread_pool[i].temp_waveBuffer_L);        thread_pool[i].temp_waveBuffer_L = NULL;
    free(thread_pool[i].temp_waveBuffer_R);        thread_pool[i].temp_waveBuffer_R = NULL;

    pthread_mutex_destroy(&thread_pool[i].work_mutex);
    pthread_cond_destroy(&thread_pool[i].work_cond);
  }

  if (g_sp3ctra_config.stereo_mode_enabled) {
    // Cleanup lock-free pan gains system
    lock_free_pan_cleanup();
    printf("🔧 LOCK_FREE_PAN: System cleaned up\n");
  }

  synth_pool_initialized = 0;
}

/**
 * @brief  Initialize RT-safe double buffering system
 * @retval 0 on success, -1 on error
 */
int init_rt_safe_buffers(void) {
  int buffer_size = g_sp3ctra_config.audio_buffer_size;
  
  // Initialize additive buffer
  g_rt_additive_buffer.buffers[0] = (float*)calloc(buffer_size, sizeof(float));
  g_rt_additive_buffer.buffers[1] = (float*)calloc(buffer_size, sizeof(float));
  if (!g_rt_additive_buffer.buffers[0] || !g_rt_additive_buffer.buffers[1]) {
    printf("ERROR: Failed to allocate RT additive buffers\n");
    return -1;
  }
  g_rt_additive_buffer.ready_buffer = 0;  // RT reads from buffer 0 initially
  g_rt_additive_buffer.worker_buffer = 1; // Workers write to buffer 1 initially
  pthread_mutex_init(&g_rt_additive_buffer.swap_mutex, NULL);

  // Initialize stereo L buffer  
  g_rt_stereo_L_buffer.buffers[0] = (float*)calloc(buffer_size, sizeof(float));
  g_rt_stereo_L_buffer.buffers[1] = (float*)calloc(buffer_size, sizeof(float));
  if (!g_rt_stereo_L_buffer.buffers[0] || !g_rt_stereo_L_buffer.buffers[1]) {
    printf("ERROR: Failed to allocate RT stereo L buffers\n");
    return -1;
  }
  g_rt_stereo_L_buffer.ready_buffer = 0;
  g_rt_stereo_L_buffer.worker_buffer = 1;
  pthread_mutex_init(&g_rt_stereo_L_buffer.swap_mutex, NULL);

  // Initialize stereo R buffer
  g_rt_stereo_R_buffer.buffers[0] = (float*)calloc(buffer_size, sizeof(float));
  g_rt_stereo_R_buffer.buffers[1] = (float*)calloc(buffer_size, sizeof(float));
  if (!g_rt_stereo_R_buffer.buffers[0] || !g_rt_stereo_R_buffer.buffers[1]) {
    printf("ERROR: Failed to allocate RT stereo R buffers\n");
    return -1;
  }
  g_rt_stereo_R_buffer.ready_buffer = 0;
  g_rt_stereo_R_buffer.worker_buffer = 1;
  pthread_mutex_init(&g_rt_stereo_R_buffer.swap_mutex, NULL);

  printf("[RT-SAFE] Double buffering system initialized\n");
  return 0;
}

/**
 * @brief  Cleanup RT-safe double buffering system
 * @retval None
 */
void cleanup_rt_safe_buffers(void) {
  // Cleanup additive buffer
  if (g_rt_additive_buffer.buffers[0]) { free(g_rt_additive_buffer.buffers[0]); g_rt_additive_buffer.buffers[0] = NULL; }
  if (g_rt_additive_buffer.buffers[1]) { free(g_rt_additive_buffer.buffers[1]); g_rt_additive_buffer.buffers[1] = NULL; }
  pthread_mutex_destroy(&g_rt_additive_buffer.swap_mutex);

  // Cleanup stereo L buffer
  if (g_rt_stereo_L_buffer.buffers[0]) { free(g_rt_stereo_L_buffer.buffers[0]); g_rt_stereo_L_buffer.buffers[0] = NULL; }
  if (g_rt_stereo_L_buffer.buffers[1]) { free(g_rt_stereo_L_buffer.buffers[1]); g_rt_stereo_L_buffer.buffers[1] = NULL; }
  pthread_mutex_destroy(&g_rt_stereo_L_buffer.swap_mutex);

  // Cleanup stereo R buffer
  if (g_rt_stereo_R_buffer.buffers[0]) { free(g_rt_stereo_R_buffer.buffers[0]); g_rt_stereo_R_buffer.buffers[0] = NULL; }
  if (g_rt_stereo_R_buffer.buffers[1]) { free(g_rt_stereo_R_buffer.buffers[1]); g_rt_stereo_R_buffer.buffers[1] = NULL; }
  pthread_mutex_destroy(&g_rt_stereo_R_buffer.swap_mutex);

  printf("[RT-SAFE] Double buffering system cleaned up\n");
}

/**
 * @brief  Swap RT-safe buffers when workers are done (called from non-RT thread)
 * @retval None
 */
void rt_safe_swap_buffers(void) {
  // Swap additive buffer (non-blocking for non-RT thread)
  pthread_mutex_lock(&g_rt_additive_buffer.swap_mutex);
  int old_ready = g_rt_additive_buffer.ready_buffer;
  g_rt_additive_buffer.ready_buffer = g_rt_additive_buffer.worker_buffer;
  g_rt_additive_buffer.worker_buffer = old_ready;
  pthread_mutex_unlock(&g_rt_additive_buffer.swap_mutex);

  // Swap stereo L buffer  
  pthread_mutex_lock(&g_rt_stereo_L_buffer.swap_mutex);
  old_ready = g_rt_stereo_L_buffer.ready_buffer;
  g_rt_stereo_L_buffer.ready_buffer = g_rt_stereo_L_buffer.worker_buffer;
  g_rt_stereo_L_buffer.worker_buffer = old_ready;
  pthread_mutex_unlock(&g_rt_stereo_L_buffer.swap_mutex);

  // Swap stereo R buffer
  pthread_mutex_lock(&g_rt_stereo_R_buffer.swap_mutex);
  old_ready = g_rt_stereo_R_buffer.ready_buffer;
  g_rt_stereo_R_buffer.ready_buffer = g_rt_stereo_R_buffer.worker_buffer;
  g_rt_stereo_R_buffer.worker_buffer = old_ready;
  pthread_mutex_unlock(&g_rt_stereo_R_buffer.swap_mutex);
}
