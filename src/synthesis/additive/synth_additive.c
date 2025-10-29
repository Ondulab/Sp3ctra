/*
 * synth_additive.c - Main additive synthesis module (refactored)
 *
 * This file serves as the main entry point for the additive synthesis system.
 * The actual implementation has been split into specialized modules:
 * - synth_additive_algorithms.c: Centralized core algorithms
 * - synth_additive_math.c: Mathematical operations and utilities
 * - synth_additive_stereo.c: Stereo processing and panning
 * - synth_additive_state.c: State management and data freeze functionality
 * - synth_additive_threading.c: Multi-threading and worker management
 *
 * Created on: 24 avr. 2019
 * Author: zhonx
 * Refactored: 21 sep. 2025
 */

#include "config.h"

// Include all the specialized modules
#include "synth_additive_algorithms.h"
#include "synth_additive_math.h"
#include "pow_approx.h"
#include "synth_additive_stereo.h"
#include "synth_additive_state.h"
#include "synth_additive_threading.h"
#include "synth_additive_runtime.h"

// Main header
#include "synth_additive.h"

// Runtime configuration
#include "../../config/config_loader.h"

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// Project includes
#include "audio_c_api.h"
#include "error.h"
#include "wave_generation.h"
#include "../../core/context.h"
#include "image_debug.h"
#include "lock_free_pan.h"
#include "../../config/config_debug.h"
#include "../../config/config_synth_additive.h"
#include "../../audio/buffers/audio_image_buffers.h"
#include "../../audio/buffers/doublebuffer.h"
#include "../../audio/rtaudio/audio_c_api.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../utils/error.h"
#include "../../utils/image_debug.h"

#ifdef __APPLE__
#include <stdlib.h>
#else
#include <stdlib.h>
#endif


/* Private variables ---------------------------------------------------------*/
// Mutex to ensure thread-safe synthesis processing for stereo channels
static pthread_mutex_t g_synth_process_mutex;

// Variables pour la limitation des logs (affichage périodique)
static uint32_t log_counter = 0;

static int32_t *imageRef = NULL; // Dynamically allocated

// Last calculated contrast factor (atomic for thread-safe access by auto-volume)
static _Atomic float g_last_contrast_factor = 0.0f;

/* Global context variables (moved from shared.c) */
struct shared_var shared_var;
volatile int32_t audioBuff[1]; // legacy placeholder, unused

// Persistent dynamically-sized buffers (allocated on first use; freed in synth_additive_cleanup)
static float *additiveBuffer   = NULL;
static float *sumVolumeBuffer  = NULL;
static float *maxVolumeBuffer  = NULL;
static float *tmp_audioData    = NULL;
// Stereo temp accumulation buffers (persistently allocated to avoid per-call alloc)
static float *stereoBuffer_L   = NULL;
static float *stereoBuffer_R   = NULL;

// Cleanup function to release persistent buffers (registered via atexit)
void synth_additive_cleanup(void) {
  if (additiveBuffer)  { free(additiveBuffer);  additiveBuffer = NULL; }
  if (sumVolumeBuffer) { free(sumVolumeBuffer); sumVolumeBuffer = NULL; }
  if (maxVolumeBuffer) { free(maxVolumeBuffer); maxVolumeBuffer = NULL; }
  if (tmp_audioData)   { free(tmp_audioData);   tmp_audioData = NULL; }
  if (stereoBuffer_L)  { free(stereoBuffer_L);  stereoBuffer_L = NULL; }
  if (stereoBuffer_R)  { free(stereoBuffer_R);  stereoBuffer_R = NULL; }
  if (imageRef)        { free(imageRef);        imageRef = NULL; }
}

/* Public functions ----------------------------------------------------------*/

int32_t synth_IfftInit(void) {
  int32_t buffer_len = 0;

  printf("---------- SYNTH INIT ---------\n");
  printf("-------------------------------\n");

  // Initialize runtime configuration
  if (synth_runtime_init(CIS_MAX_PIXELS_NB, g_sp3ctra_config.pixels_per_note) != 0) {
    fprintf(stderr, "Failed to initialize runtime configuration\n");
    return -1;
  }

  // Allocate dynamic buffers
  if (synth_runtime_allocate_buffers() != 0) {
    fprintf(stderr, "Failed to allocate dynamic buffers\n");
    return -1;
  }

  // Set global pointers to dynamically allocated arrays
  waves = synth_runtime_get_waves();
  unitary_waveform = synth_runtime_get_unitary_waveform();

  // Register cleanup functions
  atexit(synth_runtime_free_buffers);
  atexit(synth_shutdown_thread_pool);
  atexit(synth_additive_cleanup);

  // Initialize default parameters
  wavesGeneratorParams.commaPerSemitone = g_sp3ctra_config.comma_per_semitone;
  wavesGeneratorParams.startFrequency = (uint32_t)g_sp3ctra_config.start_frequency; // Cast to uint32_t
  wavesGeneratorParams.harmonization = MAJOR;
  wavesGeneratorParams.harmonizationLevel = 100;
  wavesGeneratorParams.waveformOrder = 1;

  buffer_len = init_waves(unitary_waveform, waves, &wavesGeneratorParams);

  // Precompute GAP_LIMITER envelope coefficients for all oscillators
  update_gap_limiter_coefficients();

  // Start with random index
  for (int i = 0; i < get_current_number_of_notes(); i++) {
#ifdef __APPLE__
    uint32_t aRandom32bit = arc4random();
#else
    // Use standard random function on Linux
    uint32_t aRandom32bit = rand();
#endif
    waves[i].current_idx = aRandom32bit % waves[i].area_size;
    waves[i].current_volume = 0;
    
    }

  if (buffer_len > (2400000 - 1)) {
    printf("RAM overflow");
    die("synth init failed");
    return -1;
  }

  printf("Note number  = %d\n", (int)get_current_number_of_notes());
  printf("[INFO] using Float32 path\n");
  printf("Buffer length = %d uint16\n", (int)buffer_len);

  uint8_t FreqStr[256] = {0};
  sprintf((char *)FreqStr, " %d -> %dHz      Octave:%d",
          (int)waves[0].frequency, (int)waves[get_current_number_of_notes() - 1].frequency,
          (int)sqrt(waves[get_current_number_of_notes() - 1].octave_coeff));

  printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency,
         (int)waves[0].area_size);
  printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n",
         (int)waves[get_current_number_of_notes() - 1].frequency,
         (int)waves[get_current_number_of_notes() - 1].area_size /
             (int)sqrt(waves[get_current_number_of_notes() - 1].octave_coeff),
         (int)sqrt(waves[get_current_number_of_notes() - 1].octave_coeff));

  printf("-------------------------------\n");

#ifdef PRINT_IFFT_FREQUENCY
  for (uint32_t pix = 0; pix < NUMBER_OF_NOTES; pix++) {
    printf("FREQ = %0.2f, SIZE = %d, OCTAVE = %d\n", waves[pix].frequency,
           (int)waves[pix].area_size, (int)waves[pix].octave_coeff);
#ifdef PRINT_IFFT_FREQUENCY_FULL
    int32_t output = 0;
    for (uint32_t idx = 0;
         idx < (waves[pix].area_size / waves[pix].octave_coeff); idx++) {
      output = *(waves[pix].start_ptr + (idx * waves[pix].octave_coeff));
      printf("%d\n", output);
    }
#endif
  }
  printf("-------------------------------\n");
  printf("Buffer length = %d uint16\n", (int)buffer_len);

  printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency,
         (int)waves[0].area_size);
  printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n",
         (int)waves[NUMBER_OF_NOTES - 1].frequency,
         (int)waves[NUMBER_OF_NOTES - 1].area_size /
             (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff),
         (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

  printf("-------------------------------\n");
#endif

  printf("Note number  = %d\n", (int)get_current_number_of_notes());

  // Allocate imageRef dynamically
  imageRef = (int32_t*)calloc(get_current_number_of_notes(), sizeof(int32_t));
  if (!imageRef) {
    fprintf(stderr, "ERROR: Failed to allocate imageRef\n");
    return -1;
  }
  // REFACTORED: Initialize with 1.0 in micros scale (normalized amplitude)
  // This matches the new preprocessing that stores values as (normalized * 1000000)
  fill_int32(1000000, imageRef, get_current_number_of_notes());

  // Initialize image debug system
  image_debug_init();

  // Initialize the global synthesis mutex
  if (pthread_mutex_init(&g_synth_process_mutex, NULL) != 0) {
      perror("Failed to initialize synth process mutex");
      die("synth init failed");
      return -1;
  }

  if (g_sp3ctra_config.stereo_mode_enabled) {
    // Initialize lock-free pan gains system
    lock_free_pan_init();
    printf("🔧 LOCK_FREE_PAN: System initialized for stereo mode\n");
  }

  return 0;
}

/**
 * @brief  Optimized version of the Additive synthesis with a persistent thread pool
 * @param  imageData Grayscale input data
 * @param  audioDataLeft Left channel audio output buffer (stereo mode)
 * @param  audioDataRight Right channel audio output buffer (stereo mode)
 * @param  contrast_factor Contrast factor for volume modulation
 * @retval None
 */
void synth_IfftMode(float *imageData, float *audioDataLeft, float *audioDataRight, float contrast_factor) {

  // Additive mode (limited logs)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Additive Mode called (optimized) =====\n");
  }

  static int buff_idx;
  static int first_call = 1;

  // Persistent dynamically-sized buffers are declared at file scope

  // Initialize thread pool and RT-safe buffers on first call only
  if (first_call) {
    if (synth_init_thread_pool() == 0) {
      if (init_rt_safe_buffers() == 0) {
        if (synth_start_worker_threads() == 0) {
          printf("RT-safe synthesis system initialized successfully\n");
        } else {
          printf("Error starting threads, synthesis will fail\n");
          synth_pool_initialized = 0;
        }
      } else {
        printf("Error initializing RT-safe buffers, synthesis will fail\n");
        synth_pool_initialized = 0;
      }
    } else {
      printf("Error initializing pool, synthesis will fail\n");
      synth_pool_initialized = 0;
    }
    first_call = 0;
  }

  // Allocate persistent buffers once based on runtime audio buffer size
  if (!additiveBuffer) {
    int bs = g_sp3ctra_config.audio_buffer_size;
    additiveBuffer   = (float*)calloc(bs, sizeof(float));
    sumVolumeBuffer  = (float*)calloc(bs, sizeof(float));
    maxVolumeBuffer  = (float*)calloc(bs, sizeof(float));
    tmp_audioData    = (float*)calloc(bs, sizeof(float));
    if (!additiveBuffer || !sumVolumeBuffer || !maxVolumeBuffer || !tmp_audioData) {
      printf("ERROR: Failed to allocate additive persistent buffers\n");
      return;
    }
  }

  // Debug marker: start of new image (yellow line)
  image_debug_mark_new_image_boundary();

  // Final buffers for combined results
  // buffers allocated at first call above

  // Reset final buffers
  fill_float(0, additiveBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, sumVolumeBuffer, g_sp3ctra_config.audio_buffer_size);
  fill_float(0, maxVolumeBuffer, g_sp3ctra_config.audio_buffer_size);


  if (synth_pool_initialized && !synth_pool_shutdown) {
    // === OPTIMIZED VERSION WITH THREAD POOL ===

    // Phase 1: Pre-compute data in single-thread (avoids contention)
    synth_precompute_wave_data(imageData);

    // Phase 2: Start workers in parallel
    for (int i = 0; i < 3; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      thread_pool[i].work_ready = 1;
      thread_pool[i].work_done = 0;
      pthread_cond_signal(&thread_pool[i].work_cond);
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    // Phase 3: Wait for all workers to finish (optimized latency)
    for (int i = 0; i < 3; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      while (!thread_pool[i].work_done) {
        // Very short sleep for better responsiveness
        struct timespec sleep_time = {0, 5000}; // 5 microseconds (reduced from 100µs)
        pthread_mutex_unlock(&thread_pool[i].work_mutex);
        nanosleep(&sleep_time, NULL);
        pthread_mutex_lock(&thread_pool[i].work_mutex);
      }
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    // Capture per-sample (per buffer) volumes across all notes to ensure 1 image line = 1 audio sample
  if (image_debug_is_oscillator_capture_enabled()) {
    // Iterate over each sample inside this audio buffer
    for (int s = 0; s < g_sp3ctra_config.audio_buffer_size; s++) {
      // Visit notes in ascending order across workers to keep strict note order
      for (int wi = 0; wi < 3; wi++) {
        synth_thread_worker_t *w = &thread_pool[wi];
        // Safety: ensure captured buffers are allocated for this worker
        if (!w->captured_current_volume || !w->captured_target_volume) {
          continue;
        }
        int notes_this = w->end_note - w->start_note;
        if (notes_this <= 0) continue;

        for (int note = w->start_note; note < w->end_note; note++) {
          int local_note_idx = note - w->start_note;
          if (local_note_idx < 0 || local_note_idx >= notes_this) continue;

          size_t base = (size_t)local_note_idx * (size_t)g_sp3ctra_config.audio_buffer_size;
          float cur = w->captured_current_volume[base + (size_t)s];
          float tgt = w->captured_target_volume[base + (size_t)s];
          image_debug_capture_volume_sample_fast(note, cur, tgt);
        }
      }
    }
  }

    // Thread buffers combination completed

    // Float32 version: combine float buffers directly
    for (int i = 0; i < 3; i++) {
      synth_thread_worker_t *w = &thread_pool[i];
      if (w->thread_additiveBuffer) {
        add_float(w->thread_additiveBuffer, additiveBuffer,
                  additiveBuffer, g_sp3ctra_config.audio_buffer_size);
      }
      if (w->thread_sumVolumeBuffer) {
        add_float(w->thread_sumVolumeBuffer, sumVolumeBuffer,
                  sumVolumeBuffer, g_sp3ctra_config.audio_buffer_size);
      }

      // For maxVolumeBuffer, take the maximum
      if (w->thread_maxVolumeBuffer) {
        for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
          if (w->thread_maxVolumeBuffer[buff_idx] >
              maxVolumeBuffer[buff_idx]) {
            maxVolumeBuffer[buff_idx] =
                w->thread_maxVolumeBuffer[buff_idx];
          }
        }
      }
    }

    // SATURATION PREVENTION: Apply moderate pre-scaling to prevent overflow
    // Fixed conservative factor that maintains good volume while preventing saturation
    const float safety_scale = 0.35f;  // Conservative but not excessive
    scale_float(additiveBuffer, safety_scale, g_sp3ctra_config.audio_buffer_size);

    // CORRECTION: Conditional normalization by platform
#ifdef __linux__
    // Pi/Linux: Divide by 3 (BossDAC/ALSA amplifies naturally)
    //scale_float(additiveBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
    //scale_float(sumVolumeBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
    //scale_float(maxVolumeBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
#else
    //scale_float(additiveBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
    //scale_float(sumVolumeBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
    //scale_float(maxVolumeBuffer, 1.0f / 3.0f, g_sp3ctra_config.audio_buffer_size);
#endif

  } else {
    // === ERROR: Thread pool not available ===
    printf("ERROR: Thread pool not available\n");
    // Fill buffers with silence
    fill_float(0, audioDataLeft, g_sp3ctra_config.audio_buffer_size);
    fill_float(0, audioDataRight, g_sp3ctra_config.audio_buffer_size);
    return;
  }

  // === FINAL PHASE (common to both modes) ===
  // CRITICAL FIX: Remove problematic mult_float that creates explosion with normalized waveforms
  // mult_float(additiveBuffer, maxVolumeBuffer, additiveBuffer, AUDIO_BUFFER_SIZE);
  // CRITICAL FIX: Remove the problematic scaling that was creating massive compression
  // scale_float(sumVolumeBuffer, (float)VOLUME_AMP_RESOLUTION / 2.0f, AUDIO_BUFFER_SIZE);

    // Final processing phase

    // Intelligent normalization with exponential response curve (REACTIVATED)
    // ANTI-TAC PROTECTION: Fade-in over first few callbacks to eliminate startup "tac"
    const float SUM_EPS_FLOAT = 1.0f;   // after scaling (Float path)
    
    static int startup_callback_count = 0;
    static const int FADE_IN_CALLBACKS = 10;  // Fade-in over first 10 callbacks (~2-3ms)
    float fade_in_factor = 1.0f;
    
    if (startup_callback_count < FADE_IN_CALLBACKS) {
      // Smooth exponential fade-in curve (0 -> 1 over FADE_IN_CALLBACKS)
      fade_in_factor = (float)startup_callback_count / (float)FADE_IN_CALLBACKS;
      fade_in_factor = fade_in_factor * fade_in_factor; // Quadratic curve for smooth start
      startup_callback_count++;
    }
    
    // NOISE GATE: Suppress weak signals (dust, background noise) BEFORE compression
    const float noise_gate_threshold_absolute = g_sp3ctra_config.noise_gate_threshold * (float)VOLUME_AMP_RESOLUTION;
    
    for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        // Apply noise gate to suppress weak signals
        if (sumVolumeBuffer[buff_idx] < noise_gate_threshold_absolute) {
          sumVolumeBuffer[buff_idx] = 0.0f;  // Complete suppression below threshold
          tmp_audioData[buff_idx] = 0.0f;
          continue;
        }
        
        // Compression ONLY applied to signals above gate threshold
        if (sumVolumeBuffer[buff_idx] > SUM_EPS_FLOAT) {
          // Apply exponential response curve to reduce compression effects
          float sum_normalized = sumVolumeBuffer[buff_idx] / (float)VOLUME_AMP_RESOLUTION;
          
          // Pre-normalization removed - was compressing dynamics too much
          
          float base_level = (float)SUMMATION_BASE_LEVEL / (float)VOLUME_AMP_RESOLUTION; // Use configured base level (normalized)
          // CORRECTED: Proper exponent logic for compression reduction with normalized waveforms
          float expo = 1.0f / g_sp3ctra_config.summation_response_exponent;
          float x = sum_normalized + base_level;
          float response_curve = (fabsf(expo - 0.5f) <= 1e-3f) ? sqrtf(x < 0.0f ? 0.0f : x)
                                  : pow_shifted_fast(x, base_level, expo);
          float ratio = additiveBuffer[buff_idx] / (response_curve * (float)VOLUME_AMP_RESOLUTION);
          tmp_audioData[buff_idx] = ratio * fade_in_factor; // Apply anti-tac fade-in
        } else {
          tmp_audioData[buff_idx] = 0.0f;
        }
    }
    
    // SOFT LIMITER: Prevent hard clipping while preserving dynamics (applied AFTER normalization)
    for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        float abs_signal = fabsf(tmp_audioData[buff_idx]);
        if (abs_signal > g_sp3ctra_config.soft_limit_threshold) {
          // Soft compression using tanh for smooth saturation
          float excess = abs_signal - g_sp3ctra_config.soft_limit_threshold;
          float compressed = tanhf(excess / g_sp3ctra_config.soft_limit_knee) * g_sp3ctra_config.soft_limit_knee;
          tmp_audioData[buff_idx] = copysignf(g_sp3ctra_config.soft_limit_threshold + compressed, tmp_audioData[buff_idx]);
        }
    }

  // The contrast factor is now passed as parameter from synth_AudioProcess

  // Apply contrast modulation and unified stereo output
  if (synth_pool_initialized && !synth_pool_shutdown) {
    if (g_sp3ctra_config.stereo_mode_enabled) {
    // STEREO MODE: Use actual stereo buffers from threads
    // Combine stereo buffers from all threads (declared at file scope)
    
    // Initialize stereo buffers (allocate once)
    if (!stereoBuffer_L) {
      stereoBuffer_L = (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      stereoBuffer_R = (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      if (!stereoBuffer_L || !stereoBuffer_R) {
        printf("ERROR: Failed to allocate stereo buffers\n");
      }
    }
    fill_float(0, stereoBuffer_L, g_sp3ctra_config.audio_buffer_size);
    fill_float(0, stereoBuffer_R, g_sp3ctra_config.audio_buffer_size);
    
    // Float32 version: combine float stereo buffers directly
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_additiveBuffer_L, stereoBuffer_L,
                stereoBuffer_L, g_sp3ctra_config.audio_buffer_size);
      add_float(thread_pool[i].thread_additiveBuffer_R, stereoBuffer_R,
                stereoBuffer_R, g_sp3ctra_config.audio_buffer_size);
    }
    
    // SATURATION PREVENTION: Apply same safety scaling to stereo buffers
    const float safety_scale_stereo = 0.35f;  // Same as mono for consistency
    scale_float(stereoBuffer_L, safety_scale_stereo, g_sp3ctra_config.audio_buffer_size);
    scale_float(stereoBuffer_R, safety_scale_stereo, g_sp3ctra_config.audio_buffer_size);
    
    // CRITICAL FIX: Remove problematic mult_float that creates explosion in stereo mode
    // mult_float(stereoBuffer_L, maxVolumeBuffer, stereoBuffer_L, AUDIO_BUFFER_SIZE);
    // mult_float(stereoBuffer_R, maxVolumeBuffer, stereoBuffer_R, AUDIO_BUFFER_SIZE);
    
    // Apply final processing and contrast
    // Pre-limit clipping telemetry (once per second, low overhead)
    float peakPreL = 0.0f, peakPreR = 0.0f;

    for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
      float left_signal, right_signal;

      {
        const float SUM_EPS_FLOAT = 1.0f;
        if (sumVolumeBuffer[buff_idx] > SUM_EPS_FLOAT) {
          // Apply exponential response curve to reduce compression effects (stereo mode)
          float sum_normalized = sumVolumeBuffer[buff_idx] / (float)VOLUME_AMP_RESOLUTION;
          
          // Pre-normalization removed - was compressing dynamics too much  
          
          float base_level = (float)SUMMATION_BASE_LEVEL / (float)VOLUME_AMP_RESOLUTION; // Use configured base level (normalized)
          // CORRECTED: Proper exponent logic for compression reduction with normalized waveforms
          float expo = 1.0f / g_sp3ctra_config.summation_response_exponent;
          float x = sum_normalized + base_level;
          float response_curve = (fabsf(expo - 0.5f) <= 1e-3f) ? sqrtf(x < 0.0f ? 0.0f : x)
                                  : pow_shifted_fast(x, base_level, expo);
          left_signal  = stereoBuffer_L[buff_idx] / (response_curve * (float)VOLUME_AMP_RESOLUTION);
          right_signal = stereoBuffer_R[buff_idx] / (response_curve * (float)VOLUME_AMP_RESOLUTION);
          
          // Apply same anti-tac fade-in as mono mode
          left_signal *= fade_in_factor;
          right_signal *= fade_in_factor;
        } else {
          left_signal = 0.0f;
          right_signal = 0.0f;
        }
      }

      // Track pre-limit peaks
      float aL = fabsf(left_signal), aR = fabsf(right_signal);
      if (aL > peakPreL) peakPreL = aL;
      if (aR > peakPreR) peakPreR = aR;

      // Apply contrast factor
      audioDataLeft[buff_idx] = left_signal * contrast_factor;
      audioDataRight[buff_idx] = right_signal * contrast_factor;

      // Apply final hard limiting
      if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
      if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
      if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
      if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
    }

    // Clipping telemetry disabled in production
    // if (log_counter % LOG_FREQUENCY == 0) {
    //   if (preClipCount > 0) {
    //     printf("[CLIP] pre=%d/%d (%.1f%%) peakPreL=%.3f peakPreR=%.3f\n",
    //            preClipCount, AUDIO_BUFFER_SIZE,
    //            (preClipCount * 100.0f) / (float)AUDIO_BUFFER_SIZE,
    //            peakPreL, peakPreR);
    //   } else {
    //     printf("[CLIP] pre=0 peakPreL=%.3f peakPreR=%.3f\n", peakPreL, peakPreR);
    //   }
    // }
    } else {
      // MONO MODE: Use original simple processing and duplicate output
      float peakPre = 0.0f;

      for (buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        float mono_pre = tmp_audioData[buff_idx];
        float a = fabsf(mono_pre);
        if (a > peakPre) peakPre = a;

        float mono_sample = mono_pre * contrast_factor;

        // Duplicate mono sample to both channels
        audioDataLeft[buff_idx] = mono_sample;
        audioDataRight[buff_idx] = mono_sample;

        // Apply final hard limiting
        if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
        if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
        if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
        if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
      }

      // Clipping telemetry disabled in production (mono mode)
      // if (log_counter % LOG_FREQUENCY == 0) {
      //   if (preClipCount > 0) {
      //     printf("[CLIP] pre=%d/%d (%.1f%%) peakPre=%.3f (mono)\n",
      //            preClipCount, g_sp3ctra_config.audio_buffer_size,
      //            (preClipCount * 100.0f) / (float)g_sp3ctra_config.audio_buffer_size,
      //            peakPre);
      //   } else {
      //     printf("[CLIP] pre=0 peakPre=%.3f (mono)\n", peakPre);
      //   }
      // }
    }
  } else {
    // Error case: fill with silence
    fill_float(0, audioDataLeft, g_sp3ctra_config.audio_buffer_size);
    fill_float(0, audioDataRight, g_sp3ctra_config.audio_buffer_size);
  }

  // Increment global counter for log frequency limitation
  log_counter++;

  shared_var.synth_process_cnt += g_sp3ctra_config.audio_buffer_size;
}

// Synth process function
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G,
                        uint8_t *buffer_B, DoubleBuffer *db) {
  // Audio processing (limited logs)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Audio Process called =====\n"); // Removed or commented
  }


  // Check that input buffers are not NULL
  if (!buffer_R || !buffer_G || !buffer_B) {
    printf("ERROR: One of the input buffers is NULL!\n");
    return;
  }
  int index = __atomic_load_n(&current_buffer_index, __ATOMIC_RELAXED);
  static float
      g_grayScale_live[CIS_MAX_PIXELS_NB]; // Buffer for live grayscale data (normalized float [0, 1])
  float processed_grayScale[CIS_MAX_PIXELS_NB]; // Buffer for data to be
                                                  // passed to synth_IfftMode (normalized float [0, 1])

  // RT-SAFE: Poll buffer ready state atomically (no mutex/cond needed)
  // Increased sleep time to reduce CPU contention and improve startup stability
  while (__atomic_load_n(&buffers_R[index].ready, __ATOMIC_ACQUIRE) != 0) {
    struct timespec sleep_time = {0, 10000}; // 10 microseconds (more stable)
    nanosleep(&sleep_time, NULL);
  }
  // Also wait for L buffer to be ready
  while (__atomic_load_n(&buffers_L[index].ready, __ATOMIC_ACQUIRE) != 0) {
    struct timespec sleep_time = {0, 10000}; // 10 microseconds (more stable)
    nanosleep(&sleep_time, NULL);
  }

  // 🎯 USE PREPROCESSED DATA: Get all preprocessed data in single mutex lock (optimized)
  float contrast_factor;
  pthread_mutex_lock(&db->mutex);
  memcpy(g_grayScale_live, db->preprocessed_data.grayscale, CIS_MAX_PIXELS_NB * sizeof(float));
  contrast_factor = db->preprocessed_data.contrast_factor;
  pthread_mutex_unlock(&db->mutex);

  // Debug auto-freeze after N images: keep reception active but freeze synth data
#if ADDITIVE_DEBUG_AUTOFREEZE_ENABLE
  {
    static uint32_t g_image_count = 0;
    g_image_count++;
    if (g_image_count == (uint32_t)ADDITIVE_DEBUG_AUTOFREEZE_AFTER_IMAGES) {
      pthread_mutex_lock(&g_synth_data_freeze_mutex);
      // Hard freeze (no fade) - synth_additive.c logic will snapshot current g_grayScale_live
      g_is_synth_data_frozen = 1;
      g_is_synth_data_fading_out = 0;
      pthread_mutex_unlock(&g_synth_data_freeze_mutex);
    }
  }
#endif

  // 🎯 REMOVED: Color temperature calculation - now done in preprocessing (image_preprocessor.c)
  // The stereo pan positions and gains are already calculated and stored in preprocessed data
  // TODO: Use db->preprocessed_active.stereo.pan_positions[] and gains[] when implementing preprocessed data usage

  // Capture raw scanner line for debug visualization
  image_debug_capture_raw_scanner_line(buffer_R, buffer_G, buffer_B);

  // --- Synth Data Freeze/Fade Logic ---
  pthread_mutex_lock(&g_synth_data_freeze_mutex);
  int local_is_frozen = g_is_synth_data_frozen;
  int local_is_fading = g_is_synth_data_fading_out;

  static int prev_frozen_state_synth = 0;
  if (local_is_frozen && !prev_frozen_state_synth && !local_is_fading) {
    memcpy(g_frozen_grayscale_buffer, g_grayScale_live,
           sizeof(g_grayScale_live));
  }
  prev_frozen_state_synth = local_is_frozen;

  static int prev_fading_state_synth = 0;
  if (local_is_fading && !prev_fading_state_synth) {
    g_synth_data_fade_start_time = synth_getCurrentTimeInSeconds();
  }
  prev_fading_state_synth = local_is_fading;
  pthread_mutex_unlock(&g_synth_data_freeze_mutex);

  float alpha_blend = 1.0f; // For cross-fade

  if (local_is_fading) {
    double elapsed_time =
        synth_getCurrentTimeInSeconds() - g_synth_data_fade_start_time;
    if (elapsed_time >= G_SYNTH_DATA_FADE_DURATION_SECONDS) {
      pthread_mutex_lock(&g_synth_data_freeze_mutex);
      g_is_synth_data_fading_out = 0;
      g_is_synth_data_frozen = 0;
      pthread_mutex_unlock(&g_synth_data_freeze_mutex);
      memcpy(processed_grayScale, g_grayScale_live,
             sizeof(g_grayScale_live)); // Use live data
    } else {
      alpha_blend =
          (float)(elapsed_time /
                  G_SYNTH_DATA_FADE_DURATION_SECONDS); // Alpha from 0
                                                       // (frozen) to 1 (live)
      alpha_blend = (alpha_blend < 0.0f)
                        ? 0.0f
                        : ((alpha_blend > 1.0f) ? 1.0f : alpha_blend);
      for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
        processed_grayScale[i] =
            g_frozen_grayscale_buffer[i] * (1.0f - alpha_blend) +
            g_grayScale_live[i] * alpha_blend;
      }
    }
  } else if (local_is_frozen) {
    memcpy(processed_grayScale, g_frozen_grayscale_buffer,
           sizeof(g_frozen_grayscale_buffer)); // Use frozen data
  } else {
    memcpy(processed_grayScale, g_grayScale_live,
           sizeof(g_grayScale_live)); // Use live data
  }
  // --- End Synth Data Freeze/Fade Logic ---

  // Store contrast factor atomically for auto-volume system (using memcpy for float)
  // Note: Single float write is atomic on most platforms, but we use explicit atomic for clarity
  g_last_contrast_factor = contrast_factor;

  // Launch synthesis with potentially frozen/faded data
  // Unified mode: always pass both left and right buffers
  synth_IfftMode(processed_grayScale,
                 buffers_L[index].data,
                 buffers_R[index].data,
                 contrast_factor);

  // Update global display buffers with original color data
  pthread_mutex_lock(&g_displayable_synth_mutex);
  memcpy(g_displayable_synth_R, buffer_R, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_G, buffer_G, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_B, buffer_B, CIS_MAX_PIXELS_NB);
  pthread_mutex_unlock(&g_displayable_synth_mutex);
  // Additive synthesis finished

  // RT-SAFE: Mark buffers as ready using atomic stores (no mutex needed)
  __atomic_store_n(&buffers_L[index].ready, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&buffers_R[index].ready, 1, __ATOMIC_RELEASE);
  // pthread_cond_signal removed - RT callback polls atomically

  // Change index so callback reads the filled buffer and next write goes to other buffer
  __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}

/**
 * @brief Get the last calculated contrast factor (thread-safe)
 * @return Last contrast factor value (0.0-1.0 range typically)
 * @note Used by auto-volume system to detect audio intensity for adaptive thresholding
 */
float synth_get_last_contrast_factor(void) {
  return g_last_contrast_factor;
}
