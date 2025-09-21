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
#include "synth_additive_stereo.h"
#include "synth_additive_state.h"
#include "synth_additive_threading.h"

// Main header
#include "synth_additive.h"

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// Project includes
#include "audio_c_api.h"
#include "error.h"
#include "shared.h"
#include "wave_generation.h"
#include "image_debug.h"
#include "lock_free_pan.h"
#include "../../config/config_debug.h"
#include "../../config/config_synth_additive.h"
#include "../../audio/buffers/audio_image_buffers.h"
#include "../../audio/buffers/doublebuffer.h"
#include "../../audio/rtaudio/audio_c_api.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../utils/error.h"
#include "../../utils/shared.h"
#include "../../utils/image_debug.h"

#ifdef __APPLE__
#include <stdlib.h>
#else
#include <stdlib.h>
#endif

// External declaration for debug configuration
#ifdef DEBUG_OSC
extern debug_additive_osc_config_t g_debug_osc_config;
#endif

/* Private variables ---------------------------------------------------------*/
// Mutex to ensure thread-safe synthesis processing for stereo channels
static pthread_mutex_t g_synth_process_mutex;

// Variables pour la limitation des logs (affichage périodique)
static uint32_t log_counter = 0;
#define LOG_FREQUENCY (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE) // Environ 1 seconde

static int32_t imageRef[NUMBER_OF_NOTES] = {0};

/* Public functions ----------------------------------------------------------*/

int32_t synth_IfftInit(void) {
  int32_t buffer_len = 0;

  printf("---------- SYNTH INIT ---------\n");
  printf("-------------------------------\n");

  // Register cleanup function for thread pool
  atexit(synth_shutdown_thread_pool);

  // Initialize default parameters
  wavesGeneratorParams.commaPerSemitone = COMMA_PER_SEMITONE;
  wavesGeneratorParams.startFrequency = (uint32_t)START_FREQUENCY; // Cast to uint32_t
  wavesGeneratorParams.harmonization = MAJOR;
  wavesGeneratorParams.harmonizationLevel = 100;
  wavesGeneratorParams.waveformOrder = 1;

  buffer_len = init_waves(unitary_waveform, waves, &wavesGeneratorParams);

  int32_t value = VOLUME_INCREMENT;

  if (value < 1)
    value = 1;
  if (value > 100000)
    value = 100000;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_increment =
        1.00 / (float)value * waves[note].max_volume_increment;
  }

  value = VOLUME_DECREMENT;

  if (value < 1)
    value = 1;
  if (value > 100000)
    value = 100000;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_decrement = 1.00 / (float)value * waves[note].max_volume_decrement;
  }

  // Start with random index
  for (uint32_t i = 0; i < NUMBER_OF_NOTES; i++) {
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

  printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);
  printf("Buffer length = %d uint16\n", (int)buffer_len);

  uint8_t FreqStr[256] = {0};
  sprintf((char *)FreqStr, " %d -> %dHz      Octave:%d",
          (int)waves[0].frequency, (int)waves[NUMBER_OF_NOTES - 1].frequency,
          (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

  printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency,
         (int)waves[0].area_size);
  printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n",
         (int)waves[NUMBER_OF_NOTES - 1].frequency,
         (int)waves[NUMBER_OF_NOTES - 1].area_size /
             (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff),
         (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

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

  printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);

  fill_int32(65535, (int32_t *)imageRef, NUMBER_OF_NOTES);

  // Initialize image debug system
  image_debug_init();

  // Initialize the global synthesis mutex
  if (pthread_mutex_init(&g_synth_process_mutex, NULL) != 0) {
      perror("Failed to initialize synth process mutex");
      die("synth init failed");
      return -1;
  }

#ifdef STEREO_MODE
  // Initialize lock-free pan gains system
  lock_free_pan_init();
  printf("🔧 LOCK_FREE_PAN: System initialized for stereo mode\n");
#endif

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
void synth_IfftMode(int32_t *imageData, float *audioDataLeft, float *audioDataRight, float contrast_factor) {

  // Additive mode (limited logs)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Additive Mode called (optimized) =====\n");
  }

  static int32_t signal_R;
  static int buff_idx;
  static int first_call = 1;

  // Initialize thread pool if first time
  if (first_call) {
    if (synth_init_thread_pool() == 0) {
      if (synth_start_worker_threads() == 0) {
        printf("Optimized thread pool initialized successfully\n");
      } else {
        printf("Error starting threads, synthesis will fail\n");
        synth_pool_initialized = 0;
      }
    } else {
      printf("Error initializing pool, synthesis will fail\n");
      synth_pool_initialized = 0;
    }
    first_call = 0;
  }

  // Final buffers for combined results
  static float additiveBuffer[AUDIO_BUFFER_SIZE];
  static float sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  static float maxVolumeBuffer[AUDIO_BUFFER_SIZE];

  // Reset final buffers
  fill_float(0, additiveBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, maxVolumeBuffer, AUDIO_BUFFER_SIZE);

  float tmp_audioData[AUDIO_BUFFER_SIZE];

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

    // Phase 3: Wait for all workers to finish (optimized for Pi5)
    for (int i = 0; i < 3; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      while (!thread_pool[i].work_done) {
        // ✅ OPTIMIZATION Pi5: Passive wait to reduce CPU load
        struct timespec sleep_time = {0, 100000}; // 100 microseconds
        pthread_mutex_unlock(&thread_pool[i].work_mutex);
        nanosleep(&sleep_time, NULL); // Sleep instead of busy wait
        pthread_mutex_lock(&thread_pool[i].work_mutex);
      }
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    // Phase 4: Combine results from threads with normalization
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_additiveBuffer, additiveBuffer,
                additiveBuffer, AUDIO_BUFFER_SIZE);
      add_float(thread_pool[i].thread_sumVolumeBuffer, sumVolumeBuffer,
                sumVolumeBuffer, AUDIO_BUFFER_SIZE);

      // For maxVolumeBuffer, take the maximum
      for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (thread_pool[i].thread_maxVolumeBuffer[buff_idx] >
            maxVolumeBuffer[buff_idx]) {
          maxVolumeBuffer[buff_idx] =
              thread_pool[i].thread_maxVolumeBuffer[buff_idx];
        }
      }
    }

    // CORRECTION: Conditional normalization by platform
#ifdef __linux__
    // Pi/Linux: Divide by 3 (BossDAC/ALSA amplifies naturally)
    scale_float(additiveBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(sumVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(maxVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
#else
    // Mac: No division (CoreAudio doesn't compensate automatically)
    // Signal kept at full amplitude for normal volume
#endif

  } else {
    // === ERROR: Thread pool not available ===
    printf("ERROR: Thread pool not available\n");
    // Fill buffers with silence
    fill_float(0, audioDataLeft, AUDIO_BUFFER_SIZE);
    fill_float(0, audioDataRight, AUDIO_BUFFER_SIZE);
    return;
  }

  // === FINAL PHASE (common to both modes) ===
  mult_float(additiveBuffer, maxVolumeBuffer, additiveBuffer,
             AUDIO_BUFFER_SIZE);
  scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);

    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (sumVolumeBuffer[buff_idx] != 0) {
            signal_R =
                (int32_t)(additiveBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
        } else {
            signal_R = 0;
        }
        tmp_audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;

        // Capture oscillator volumes for each audio sample (48kHz resolution)
        image_debug_capture_oscillator_sample();
    }

  // The contrast factor is now passed as parameter from synth_AudioProcess

  // Apply contrast modulation and unified stereo output
  if (synth_pool_initialized && !synth_pool_shutdown) {
#ifdef STEREO_MODE
    // STEREO MODE: Use actual stereo buffers from threads
    // Combine stereo buffers from all threads
    static float stereoBuffer_L[AUDIO_BUFFER_SIZE];
    static float stereoBuffer_R[AUDIO_BUFFER_SIZE];
    
    // Initialize stereo buffers
    fill_float(0, stereoBuffer_L, AUDIO_BUFFER_SIZE);
    fill_float(0, stereoBuffer_R, AUDIO_BUFFER_SIZE);
    
    // Accumulate stereo buffers from all threads
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_additiveBuffer_L, stereoBuffer_L,
                stereoBuffer_L, AUDIO_BUFFER_SIZE);
      add_float(thread_pool[i].thread_additiveBuffer_R, stereoBuffer_R,
                stereoBuffer_R, AUDIO_BUFFER_SIZE);
    }
    
    // Apply same normalization as mono signal
    mult_float(stereoBuffer_L, maxVolumeBuffer, stereoBuffer_L, AUDIO_BUFFER_SIZE);
    mult_float(stereoBuffer_R, maxVolumeBuffer, stereoBuffer_R, AUDIO_BUFFER_SIZE);
    
    // Apply final processing and contrast
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float left_signal, right_signal;
      
      if (sumVolumeBuffer[buff_idx] != 0) {
        left_signal = (stereoBuffer_L[buff_idx] / sumVolumeBuffer[buff_idx]) / (float)WAVE_AMP_RESOLUTION;
        right_signal = (stereoBuffer_R[buff_idx] / sumVolumeBuffer[buff_idx]) / (float)WAVE_AMP_RESOLUTION;
      } else {
        left_signal = 0;
        right_signal = 0;
      }
      
      // Apply contrast factor
      audioDataLeft[buff_idx] = left_signal * contrast_factor;
      audioDataRight[buff_idx] = right_signal * contrast_factor;

      // Apply final hard limiting
      if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
      if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
      if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
      if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
    }

#else
    // MONO MODE: Use original simple processing and duplicate output
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float mono_sample = tmp_audioData[buff_idx] * contrast_factor;
      
      // Duplicate mono sample to both channels
      audioDataLeft[buff_idx] = mono_sample;
      audioDataRight[buff_idx] = mono_sample;

      // Apply final hard limiting
      if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
      if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
      if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
      if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
    }
#endif
  } else {
    // Error case: fill with silence
    fill_float(0, audioDataLeft, AUDIO_BUFFER_SIZE);
    fill_float(0, audioDataRight, AUDIO_BUFFER_SIZE);
  }

  // Increment global counter for log frequency limitation
  log_counter++;

  shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}

// Synth process function
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G,
                        uint8_t *buffer_B) {
  // Audio processing (limited logs)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Audio Process called =====\n"); // Removed or commented
  }

#ifdef DEBUG_OSC
  // Debug: Print "NEW IMAGE LINE" when a new image is processed
  if (g_debug_osc_config.enabled) {
    printf("\n=== NEW IMAGE LINE ===\n");
    fflush(stdout);
  }
#endif

  // Check that input buffers are not NULL
  if (!buffer_R || !buffer_G || !buffer_B) {
    printf("ERROR: One of the input buffers is NULL!\n");
    return;
  }
  int index = __atomic_load_n(&current_buffer_index, __ATOMIC_RELAXED);
  static int32_t
      g_grayScale_live[CIS_MAX_PIXELS_NB]; // Buffer for live grayscale data
  int32_t processed_grayScale[CIS_MAX_PIXELS_NB]; // Buffer for data to be
                                                  // passed to synth_IfftMode

  // Wait for destination buffer to be free
  pthread_mutex_lock(&buffers_R[index].mutex);
  while (buffers_R[index].ready != 0) {
    pthread_cond_wait(&buffers_R[index].cond, &buffers_R[index].mutex);
  }
  pthread_mutex_unlock(&buffers_R[index].mutex);

#if 1
  // Launch grayscale conversion
  greyScale(buffer_R, buffer_G, buffer_B, g_grayScale_live, CIS_MAX_PIXELS_NB);

#ifdef STEREO_MODE
  // Calculate color temperature and pan positions for each oscillator
  // This is done once per image reception for efficiency
  for (uint32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    // Calculate average color for this note's pixels
    uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
    uint32_t pixel_count = 0;
    
    for (uint32_t pix = 0; pix < PIXELS_PER_NOTE; pix++) {
      uint32_t pixel_idx = note * PIXELS_PER_NOTE + pix;
      if (pixel_idx < CIS_MAX_PIXELS_NB) {
        r_sum += buffer_R[pixel_idx];
        g_sum += buffer_G[pixel_idx];
        b_sum += buffer_B[pixel_idx];
        pixel_count++;
      }
    }
    
    if (pixel_count > 0) {
      // Calculate average RGB values
      uint8_t r_avg = r_sum / pixel_count;
      uint8_t g_avg = g_sum / pixel_count;
      uint8_t b_avg = b_sum / pixel_count;
      
      // Calculate color temperature and pan position
      float temperature = calculate_color_temperature(r_avg, g_avg, b_avg);
      waves[note].pan_position = temperature;
      
      // Use temporary variables to avoid volatile qualifier warnings
      float temp_left_gain, temp_right_gain;
      calculate_pan_gains(temperature, &temp_left_gain, &temp_right_gain);
      waves[note].left_gain = temp_left_gain;
      waves[note].right_gain = temp_right_gain;
      
      // Debug output for first few notes (limited frequency)
      if (log_counter % (LOG_FREQUENCY * 10) == 0 && note < 5) {
#ifdef DEBUG_RGB_TEMPERATURE
        printf("Note %d: RGB(%d,%d,%d) -> Temp=%.2f L=%.2f R=%.2f\n",
               note, r_avg, g_avg, b_avg, temperature,
               waves[note].left_gain, waves[note].right_gain);
#endif
      }
    } else {
      // Default to center if no pixels
      waves[note].pan_position = 0.0f;
      waves[note].left_gain = 0.707f;
      waves[note].right_gain = 0.707f;
    }
  }
  
  // Update lock-free pan gains system with calculated values
  // Prepare arrays for batch update
  static float left_gains[NUMBER_OF_NOTES];
  static float right_gains[NUMBER_OF_NOTES];
  static float pan_positions[NUMBER_OF_NOTES];
  
  for (uint32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    left_gains[note] = waves[note].left_gain;
    right_gains[note] = waves[note].right_gain;
    pan_positions[note] = waves[note].pan_position;
  }
  
  // Atomic update of all pan gains
  lock_free_pan_update(left_gains, right_gains, pan_positions, NUMBER_OF_NOTES);
#endif // STEREO_MODE

#ifdef ENABLE_IMAGE_DEBUG
  // Capture mono pipeline for debug visualization
  static int debug_frame_counter_mono = 0;
  image_debug_capture_mono_pipeline(buffer_R, buffer_G, buffer_B,
                                   g_grayScale_live, processed_grayScale,
                                   debug_frame_counter_mono++);
#endif

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
            (int32_t)(g_frozen_grayscale_buffer[i] * (1.0f - alpha_blend) +
                      g_grayScale_live[i] * alpha_blend);
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

  // Calculate contrast factor based on the processed grayscale image
  // This optimization moves the contrast calculation from synth_IfftMode to here
  // for better performance (calculated once per image instead of per audio buffer)
  float contrast_factor = calculate_contrast(processed_grayScale, CIS_MAX_PIXELS_NB);

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
#endif

  // Mark buffers as ready
  pthread_mutex_lock(&buffers_L[index].mutex);
  buffers_L[index].ready = 1;
  pthread_cond_signal(&buffers_L[index].cond);
  pthread_mutex_unlock(&buffers_L[index].mutex);

  pthread_mutex_lock(&buffers_R[index].mutex);
  buffers_R[index].ready = 1;
  pthread_cond_signal(&buffers_R[index].cond);
  pthread_mutex_unlock(&buffers_R[index].mutex);

  // Change index so callback reads the filled buffer and next write goes to other buffer
  __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}
