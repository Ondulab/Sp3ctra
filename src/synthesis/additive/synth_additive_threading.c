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
#include "pow_approx.h"
#include "wave_generation.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../audio/buffers/doublebuffer.h"
#include "../../config/config_debug.h"
#include "../../config/config_loader.h"
#include "../../utils/image_debug.h"
#include "../../utils/logger.h"
#include "../../utils/rt_profiler.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>

#ifdef __linux__
#include <sched.h>
#endif

/* External RT Profiler */
extern RTProfiler g_rt_profiler;

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

// Pool of persistent threads (dynamically allocated)
synth_thread_worker_t *thread_pool = NULL;
pthread_t *worker_threads = NULL;
int num_workers = 0;  // Actual number of workers from config
volatile int synth_pool_initialized = 0;
volatile int synth_pool_shutdown = 0;

// Barrier synchronization for deterministic execution
#ifdef __linux__
pthread_barrier_t g_worker_start_barrier;
pthread_barrier_t g_worker_end_barrier;
#else
barrier_t g_worker_start_barrier;
barrier_t g_worker_end_barrier;
#endif
volatile int g_use_barriers = 1;  // Enable barriers by default for deterministic execution

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

  // Get number of workers from config (with validation)
  num_workers = g_sp3ctra_config.num_workers;
  if (num_workers < 1 || num_workers > MAX_WORKERS) {
    log_warning("SYNTH", "Invalid num_workers=%d, clamping to range [1, %d]", num_workers, MAX_WORKERS);
    num_workers = (num_workers < 1) ? 1 : MAX_WORKERS;
  }
  
  log_info("SYNTH", "Initializing thread pool with %d workers", num_workers);
  
  // Allocate thread pool and worker threads arrays
  thread_pool = (synth_thread_worker_t*)calloc(num_workers, sizeof(synth_thread_worker_t));
  worker_threads = (pthread_t*)calloc(num_workers, sizeof(pthread_t));
  
  if (!thread_pool || !worker_threads) {
    log_error("SYNTH", "Failed to allocate thread pool arrays");
    if (thread_pool) { free(thread_pool); thread_pool = NULL; }
    if (worker_threads) { free(worker_threads); worker_threads = NULL; }
    return -1;
  }

  // Initialize barrier synchronization (Phase 2: Deterministic execution)
  if (g_use_barriers) {
    // num_workers + 1 for main thread
    if (synth_init_barriers(num_workers + 1) != 0) {
      log_warning("SYNTH", "Failed to initialize barriers, falling back to condition variables");
      g_use_barriers = 0;
    }
  }

  int current_notes = get_current_number_of_notes();
  int notes_per_thread = current_notes / num_workers;

  for (int i = 0; i < num_workers; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    // Worker configuration
    worker->thread_id = i;
    worker->start_note = i * notes_per_thread;
    // Last worker handles all remaining notes (handles rounding)
    worker->end_note = (i == num_workers - 1) ? current_notes : (i + 1) * notes_per_thread;

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
        log_error("SYNTH", "Error allocating worker buffers for thread %d", i);
        return -1;
      }
      
      // Initialize last pan gains for per-buffer ramping (center equal-power)
      for (int idx = 0; idx < notes_this; idx++) {
        worker->last_left_gain[idx] = 0.707f;
        worker->last_right_gain[idx] = 0.707f;
      }
    }

    // Initialize timing statistics
    worker->worker_time_sum_us = 0;
    worker->worker_time_max_us = 0;
    worker->worker_timing_sample_count = 0;

    // Initialize synchronization
    if (pthread_mutex_init(&worker->work_mutex, NULL) != 0) {
      log_error("SYNTH", "Error initializing mutex for thread %d", i);
      return -1;
    }
    if (pthread_cond_init(&worker->work_cond, NULL) != 0) {
      log_error("SYNTH", "Error initializing condition for thread %d", i);
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
    // Deterministic execution with barriers
    // Wait at start barrier for all workers + main thread
    synth_barrier_wait(&g_worker_start_barrier);
    
    if (synth_pool_shutdown)
      break;
    
    // Perform the work (Float32 path)
    synth_process_worker_range(worker);
    
    // Wait at end barrier for all workers to complete
    synth_barrier_wait(&g_worker_end_barrier);
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
    log_info("SYNTH", "Float32 WORKER: Using Float32 path in workers");
    f32_logged = 1;
  }

  // Record start time for this worker
  gettimeofday(&worker->worker_start_time, NULL);

  // Release capture buffers if capture was disabled since last buffer
  synth_release_capture_buffers_if_disabled(worker);

  // Initialize output buffers to zero
  fill_float(0, worker->thread_additiveBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_sumVolumeBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_maxVolumeBuffer, g_sp3ctra_config.audio_buffer_size);

  // Initialize stereo buffers - CRITICAL FIX: must zero these buffers! (always present)
  fill_float(0, worker->thread_additiveBuffer_L, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, worker->thread_additiveBuffer_R, g_sp3ctra_config.audio_buffer_size);

  // DEPRECATED: Old preprocessing removed - now using preprocessed_data.additive.notes[]
  // The preprocessing is done centrally in image_preprocessor.c
  // Data is already: RGB → Grayscale → Inversion → Gamma → Averaging → Contrast

  // ✅ OPTIMIZATION: Hoist invariant calculations and improve cache locality
  const int audio_buffer_size = g_sp3ctra_config.audio_buffer_size;
  const int stereo_enabled = g_sp3ctra_config.stereo_mode_enabled;
  const float volume_weighting_exp = g_sp3ctra_config.volume_weighting_exponent;
  const int capture_enabled = image_debug_is_oscillator_capture_enabled();
  
  // Main note processing loop - optimized for cache efficiency
  for (note = worker->start_note; note < worker->end_note; note++) {
    local_note_idx = note - worker->start_note;
    
    // ✅ OPTIMIZATION: Prefetch next iteration data (improves cache hit rate)
    if (note + 1 < worker->end_note) {
      __builtin_prefetch(&worker->precomputed_volume[local_note_idx + 1], 0, 3);
      __builtin_prefetch(&worker->precomputed_wave_data[(size_t)(local_note_idx + 1) * audio_buffer_size], 0, 3);
    }
    
    // Use preprocessed volume data (already has: RGB → Grayscale → Inversion → Gamma → Averaging)
    float target_volume = worker->precomputed_volume[local_note_idx];

    // ✅ OPTIMIZATION: Compute pointers once (avoid repeated address calculations)
    const float* pre_wave = worker->precomputed_wave_data + (size_t)local_note_idx * audio_buffer_size;
    float* wave_buf = worker->waveBuffer;
    float* vol_buf = worker->volumeBuffer;
    
    // Generate waveform samples
    generate_waveform_samples(note, wave_buf, pre_wave);

    // Apply GAP_LIMITER envelope
    apply_gap_limiter_ramp(note, target_volume, pre_wave, vol_buf);

    // Debug capture (fast path when disabled)
    if (capture_enabled) {
      if (synth_ensure_capture_buffers(worker) == 0) {
        memcpy(worker->captured_current_volume + (size_t)local_note_idx * audio_buffer_size,
               vol_buf,
               sizeof(float) * (size_t)audio_buffer_size);
        fill_float(waves[note].target_volume,
                   worker->captured_target_volume + (size_t)local_note_idx * audio_buffer_size,
                   (size_t)audio_buffer_size);
      }
    }

    // Apply volume scaling to the current note waveform
    mult_float(wave_buf, vol_buf, wave_buf, audio_buffer_size);

    // ✅ OPTIMIZATION: Update max volume buffer inline (better cache locality)
    for (buff_idx = audio_buffer_size; --buff_idx >= 0;) {
      if (vol_buf[buff_idx] > worker->thread_maxVolumeBuffer[buff_idx]) {
        worker->thread_maxVolumeBuffer[buff_idx] = vol_buf[buff_idx];
      }
    }

    // ✅ OPTIMIZATION: Conditional stereo/mono processing (hoisted check)
    if (stereo_enabled) {
      // Stereo mode: Apply per-oscillator panning with per-buffer ramp
      const float start_left  = worker->last_left_gain[local_note_idx];
      const float start_right = worker->last_right_gain[local_note_idx];
      const float end_left    = worker->precomputed_left_gain[local_note_idx];
      const float end_right   = worker->precomputed_right_gain[local_note_idx];

      // Use optimized stereo panning function (NEON-accelerated on ARM)
      apply_stereo_pan_ramp(wave_buf, 
                           worker->temp_waveBuffer_L, 
                           worker->temp_waveBuffer_R,
                           start_left, start_right, end_left, end_right,
                           audio_buffer_size);

      // Persist end-gains for next buffer ramp
      worker->last_left_gain[local_note_idx]  = end_left;
      worker->last_right_gain[local_note_idx] = end_right;
      
      // ✅ OPTIMIZATION: Direct pointer usage (avoid temp variables)
      add_float(worker->temp_waveBuffer_L, worker->thread_additiveBuffer_L,
                worker->thread_additiveBuffer_L, audio_buffer_size);
      add_float(worker->temp_waveBuffer_R, worker->thread_additiveBuffer_R,
                worker->thread_additiveBuffer_R, audio_buffer_size);
    } else {
      // Mono mode: Duplicate mono signal to both L/R channels (center panning)
      add_float(wave_buf, worker->thread_additiveBuffer_L,
                worker->thread_additiveBuffer_L, audio_buffer_size);
      add_float(wave_buf, worker->thread_additiveBuffer_R,
                worker->thread_additiveBuffer_R, audio_buffer_size);
    }

    // Additive summation for mono or combined processing
    add_float(wave_buf, worker->thread_additiveBuffer,
              worker->thread_additiveBuffer, audio_buffer_size);
    
    // Intelligent volume weighting: strong oscillators dominate over weak background noise
    // ✅ OPTIMIZATION: Use hoisted constant for weighting exponent
    apply_volume_weighting(worker->thread_sumVolumeBuffer, vol_buf,
                          volume_weighting_exp, audio_buffer_size);

    // Commit phase continuity: set waves[note].current_idx to the last precomputed index for this buffer
    waves[note].current_idx = *(worker->precomputed_new_idx + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size + (g_sp3ctra_config.audio_buffer_size - 1));
  }

  // Record end time for this worker
  gettimeofday(&worker->worker_end_time, NULL);
  
  // Calculate execution time
  int64_t sec_diff = (int64_t)(worker->worker_end_time.tv_sec - worker->worker_start_time.tv_sec);
  int64_t usec_diff = (int64_t)(worker->worker_end_time.tv_usec - worker->worker_start_time.tv_usec);
  uint64_t worker_us = (uint64_t)(sec_diff * 1000000LL + usec_diff);
  
  // Accumulate statistics
  worker->worker_time_sum_us += worker_us;
  if (worker_us > worker->worker_time_max_us) {
    worker->worker_time_max_us = worker_us;
  }
  worker->worker_timing_sample_count++;

  // NOTE: RT-safe buffer writing removed - causes audio corruption
  // Workers only write to their local buffers, main thread combines them
}

/**
 * @brief  Pre-compute waves[] data in parallel to avoid contention
 * @param  imageData Input image data
 * @param  db DoubleBuffer for accessing preprocessed stereo data
 * @retval None
 */
void synth_precompute_wave_data(float *imageData, DoubleBuffer *db) {
  // ✅ CRITICAL OPTIMIZATION: Batch read all preprocessed data in ONE mutex lock
  // BEFORE: 6912 mutex locks per buffer (2 locks × 3456 notes) = massive contention!
  // AFTER: 1 mutex lock per buffer = 6912x reduction in lock overhead
  
  // Phase 1: Image data assignment (thread-safe, read-only)
  for (int i = 0; i < num_workers; i++) {
    thread_pool[i].imageData = imageData;
  }

  // Phase 2: Batch copy ALL preprocessed data with a SINGLE mutex lock
  // RT PROFILER: Measure mutex contention
  struct timeval mutex_start, mutex_end;
  gettimeofday(&mutex_start, NULL);
  rt_profiler_mutex_lock_start(&g_rt_profiler);
  
  pthread_mutex_lock(&db->mutex);
  
  gettimeofday(&mutex_end, NULL);
  int64_t sec_diff = (int64_t)(mutex_end.tv_sec - mutex_start.tv_sec);
  int64_t usec_diff = (int64_t)(mutex_end.tv_usec - mutex_start.tv_usec);
  uint64_t wait_us = (uint64_t)(sec_diff * 1000000LL + usec_diff);
  rt_profiler_mutex_lock_end(&g_rt_profiler, wait_us);
  
  // Copy all preprocessed data for all workers in one shot
  for (int i = 0; i < num_workers; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];
    int notes_this_worker = worker->end_note - worker->start_note;
    
    // Batch copy volume data
    memcpy(worker->precomputed_volume,
           &db->preprocessed_data.additive.notes[worker->start_note],
           notes_this_worker * sizeof(float));
    
    // Batch copy stereo data if enabled
    if (g_sp3ctra_config.stereo_mode_enabled) {
      memcpy(worker->precomputed_pan_position,
             &db->preprocessed_data.stereo.pan_positions[worker->start_note],
             notes_this_worker * sizeof(float));
      memcpy(worker->precomputed_left_gain,
             &db->preprocessed_data.stereo.left_gains[worker->start_note],
             notes_this_worker * sizeof(float));
      memcpy(worker->precomputed_right_gain,
             &db->preprocessed_data.stereo.right_gains[worker->start_note],
             notes_this_worker * sizeof(float));
    }
  }
  
  pthread_mutex_unlock(&db->mutex);

  // Phase 3: Lock-free parallel pre-computation of waves[] data by ranges
  // Each worker computes independently without mutex contention
  // THREAD-SAFETY ANALYSIS:
  // - Each worker processes a disjoint range of notes (no overlap)
  // - waves[note] reads are thread-safe (read-only access during precomputation)
  // - waves[note].current_idx writes are deferred until after worker completion
  // - Preprocessed data already copied, no more mutex needed
  
  for (int i = 0; i < num_workers; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    for (int note = worker->start_note; note < worker->end_note; note++) {
      int local_note_idx = note - worker->start_note;
      int32_t* pre_idx_base = worker->precomputed_new_idx + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size;
      float* pre_wave_base = worker->precomputed_wave_data + (size_t)local_note_idx * g_sp3ctra_config.audio_buffer_size;

      // Pre-compute waveform data
      // Preserve phase continuity: compute indices locally, do not write back to waves[].current_idx here
      // ✅ LOCK-FREE: Read-only access to waves[note] fields (thread-safe)
      uint32_t cur_idx = waves[note].current_idx;
      const uint32_t octave_coeff = waves[note].octave_coeff;
      const uint32_t area_size = waves[note].area_size;
      volatile float* volatile ptr = waves[note].start_ptr;
      const float* start_ptr = (const float*)ptr;  // Safe cast: read-only access
      
      // Optimize loop: hoist invariant loads, enable better compiler optimizations
      for (int buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        int32_t new_idx = (cur_idx + octave_coeff);
        if ((uint32_t)new_idx >= area_size) {
          new_idx -= area_size;
        }

        pre_idx_base[buff_idx] = new_idx;
        pre_wave_base[buff_idx] = *(start_ptr + new_idx);
        cur_idx = (uint32_t)new_idx;
      }
      // Workers will commit the last index per note after processing using the precomputed indices
    }
  }
  
  // ✅ PERFORMANCE BOOST: Eliminated per-note mutex contention
  // BEFORE: 6912 mutex locks per buffer (catastrophic for RT performance)
  // AFTER: 1 mutex lock per buffer (6912x reduction!)
  // Expected speedup: 50-70% reduction in precomputation time
  // Expected spike reduction: Eliminates mutex-induced latency spikes
}

/**
 * @brief  Start worker threads with CPU affinity and RT priorities
 * @retval 0 on success, -1 on error
 */
int synth_start_worker_threads(void) {
  for (int i = 0; i < num_workers; i++) {
    if (pthread_create(&worker_threads[i], NULL, synth_persistent_worker_thread,
                       &thread_pool[i]) != 0) {
      log_error("SYNTH", "Error creating worker thread %d", i);
      return -1;
    }

    // ✅ PHASE 1: Set RT priority for deterministic execution
#if defined(__linux__) || defined(__APPLE__)
    if (synth_set_rt_priority(worker_threads[i], 80) != 0) {
      log_warning("SYNTH", "Failed to set RT priority for worker %d (continuing without RT)", i);
    }
#endif

    // ✅ OPTIMIZATION: CPU affinity to balance load on Pi5
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Distribute threads across available CPUs (leave CPU 0 for system if possible)
    // For num_workers <= 7, use CPUs 1-7; for num_workers == 8, use CPUs 0-7
    int cpu_id = (num_workers <= 7) ? (i + 1) : i;
    CPU_SET(cpu_id, &cpuset);

    int result =
        pthread_setaffinity_np(worker_threads[i], sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
      log_info("SYNTH", "Worker thread %d assigned to CPU %d", i, cpu_id);
    } else {
      log_warning("SYNTH", "Cannot assign thread %d to CPU %d (error: %d)", i, cpu_id, result);
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
  for (int i = 0; i < num_workers; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    pthread_cond_signal(&thread_pool[i].work_cond);
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }

  // Wait for all threads to terminate
  for (int i = 0; i < num_workers; i++) {
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

  // Free the dynamically allocated arrays
  if (thread_pool) {
    free(thread_pool);
    thread_pool = NULL;
  }
  if (worker_threads) {
    free(worker_threads);
    worker_threads = NULL;
  }
  num_workers = 0;

  if (g_sp3ctra_config.stereo_mode_enabled) {
    // Cleanup lock-free pan gains system
    lock_free_pan_cleanup();
    log_info("SYNTH", "Lock-free pan system cleaned up");
  }

  // Cleanup barrier synchronization
  if (g_use_barriers) {
    synth_cleanup_barriers();
    log_info("SYNTH", "Barrier synchronization cleaned up");
  }

  synth_pool_initialized = 0;
  log_info("SYNTH", "Thread pool shutdown complete");
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
    log_error("SYNTH", "Failed to allocate RT additive buffers");
    return -1;
  }
  g_rt_additive_buffer.ready_buffer = 0;  // RT reads from buffer 0 initially
  g_rt_additive_buffer.worker_buffer = 1; // Workers write to buffer 1 initially
  pthread_mutex_init(&g_rt_additive_buffer.swap_mutex, NULL);

  // Initialize stereo L buffer  
  g_rt_stereo_L_buffer.buffers[0] = (float*)calloc(buffer_size, sizeof(float));
  g_rt_stereo_L_buffer.buffers[1] = (float*)calloc(buffer_size, sizeof(float));
  if (!g_rt_stereo_L_buffer.buffers[0] || !g_rt_stereo_L_buffer.buffers[1]) {
    log_error("SYNTH", "Failed to allocate RT stereo L buffers");
    return -1;
  }
  g_rt_stereo_L_buffer.ready_buffer = 0;
  g_rt_stereo_L_buffer.worker_buffer = 1;
  pthread_mutex_init(&g_rt_stereo_L_buffer.swap_mutex, NULL);

  // Initialize stereo R buffer
  g_rt_stereo_R_buffer.buffers[0] = (float*)calloc(buffer_size, sizeof(float));
  g_rt_stereo_R_buffer.buffers[1] = (float*)calloc(buffer_size, sizeof(float));
  if (!g_rt_stereo_R_buffer.buffers[0] || !g_rt_stereo_R_buffer.buffers[1]) {
    log_error("SYNTH", "Failed to allocate RT stereo R buffers");
    return -1;
  }
  g_rt_stereo_R_buffer.ready_buffer = 0;
  g_rt_stereo_R_buffer.worker_buffer = 1;
  pthread_mutex_init(&g_rt_stereo_R_buffer.swap_mutex, NULL);

  log_info("SYNTH", "RT-safe double buffering system initialized");
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

  log_info("SYNTH", "RT-safe double buffering system cleaned up");
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
