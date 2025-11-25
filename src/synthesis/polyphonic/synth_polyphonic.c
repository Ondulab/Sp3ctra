/*
 * synth_polyphonic.c
 */

#include "synth_polyphonic.h"
#include "config.h"
#include "context.h"
#include "doublebuffer.h"
#include "error.h"
#include "../../config/config_loader.h"
#include "../../config/config_instrument.h"
#include "../../utils/logger.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/mach_time.h>
#endif

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#define TWO_PI (2.0 * M_PI)

// --- Static Forward Declarations ---
static void adsr_init_envelope(AdsrEnvelope *env, float attack_s, float decay_s,
                               float sustain_level, float release_s,
                               float sample_rate);
static void adsr_update_settings_and_recalculate_rates(
    AdsrEnvelope *env, float attack_s, float decay_s, float sustain_level,
    float release_s,
    float sample_rate); // New forward declaration
static void adsr_trigger_attack(AdsrEnvelope *env);
static void adsr_trigger_release(AdsrEnvelope *env);
static float adsr_get_output(AdsrEnvelope *env);
static void filter_init_spectral_params(SpectralFilterParams *fp,
                                        float base_cutoff_hz,
                                        float filter_env_depth);
static void lfo_init(LfoState *lfo, float rate_hz, float depth_semitones,
                     float sample_rate);
static float lfo_process(LfoState *lfo);

// --- Synth Parameters & Globals ---
// Runtime-configurable polyphonic synthesis parameters
int g_num_poly_voices = 8;           // Default: 8 voices (will be loaded from config)
int g_max_mapped_oscillators = 128;  // Default: 128 oscillators (will be loaded from config)

// Polyphony related globals
unsigned long long g_current_trigger_order =
    0; // Global trigger order counter, starts at 0
SynthVoice poly_voices[MAX_POLY_VOICES];
float global_smoothed_magnitudes[MAX_MAPPED_OSCILLATORS];
float global_stereo_left_gains[MAX_MAPPED_OSCILLATORS];   // Per-harmonic left gains (spectral panning)
float global_stereo_right_gains[MAX_MAPPED_OSCILLATORS];  // Per-harmonic right gains (spectral panning)
float global_harmonicity[MAX_MAPPED_OSCILLATORS];         // Per-harmonic harmonicity [0,1] from color temperature
float global_detune_cents[MAX_MAPPED_OSCILLATORS];        // Per-harmonic detune in cents for semi-harmonic sounds
float global_inharmonic_ratios[MAX_MAPPED_OSCILLATORS];   // Per-harmonic frequency ratios for inharmonic sounds
SpectralFilterParams global_spectral_filter_params;
LfoState global_vibrato_lfo; // Definition for the global LFO

// Polyphonic synthesis related globals
FftAudioDataBuffer polyphonic_audio_buffers[2];
volatile int polyphonic_current_buffer_index = 0;
pthread_mutex_t polyphonic_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

extern volatile int keepRunning;

// --- Initialization ---
void synth_polyphonicMode_init(void) {
  int i, j;
  
  // Load runtime configuration values
  g_num_poly_voices = g_sp3ctra_config.poly_num_voices;
  g_max_mapped_oscillators = g_sp3ctra_config.poly_max_oscillators;
  
  // Validate configuration
  if (g_num_poly_voices < 1 || g_num_poly_voices > MAX_POLY_VOICES) {
    log_warning("SYNTH", "Invalid poly_num_voices (%d), clamping to [1, %d]", 
                g_num_poly_voices, MAX_POLY_VOICES);
    g_num_poly_voices = (g_num_poly_voices < 1) ? 1 : MAX_POLY_VOICES;
  }
  
  if (g_max_mapped_oscillators < 1 || g_max_mapped_oscillators > MAX_MAPPED_OSCILLATORS) {
    log_warning("SYNTH", "Invalid poly_max_oscillators (%d), clamping to [1, %d]", 
                g_max_mapped_oscillators, MAX_MAPPED_OSCILLATORS);
    g_max_mapped_oscillators = (g_max_mapped_oscillators < 1) ? 1 : MAX_MAPPED_OSCILLATORS;
  }
  
  log_info("SYNTH", "Initializing polyphonic synthesis mode with LFO");
  log_info("SYNTH", "Configuration: %d voices, %d oscillators per voice (total: %d oscillators)",
           g_num_poly_voices, g_max_mapped_oscillators, 
           g_num_poly_voices * g_max_mapped_oscillators);

  for (i = 0; i < 2; ++i) {
    if (pthread_mutex_init(&polyphonic_audio_buffers[i].mutex, NULL) != 0) {
      die("Failed to initialize polyphonic audio buffer mutex");
    }
    if (pthread_cond_init(&polyphonic_audio_buffers[i].cond, NULL) != 0) {
      die("Failed to initialize polyphonic audio buffer condition variable");
    }
    polyphonic_audio_buffers[i].ready = 0;
    
    // Allocate separate L/R buffers for true stereo
    if (!polyphonic_audio_buffers[i].data_left) {
      polyphonic_audio_buffers[i].data_left = (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
    } else {
      memset(polyphonic_audio_buffers[i].data_left, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
    
    if (!polyphonic_audio_buffers[i].data_right) {
      polyphonic_audio_buffers[i].data_right = (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
    } else {
      memset(polyphonic_audio_buffers[i].data_right, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
  }
  if (pthread_mutex_init(&polyphonic_buffer_index_mutex, NULL) != 0) {
    die("Failed to initialize polyphonic buffer index mutex");
  }
  polyphonic_current_buffer_index = 0;

  memset(global_smoothed_magnitudes, 0, sizeof(global_smoothed_magnitudes));
  
  /* Initialize stereo gains to center (0.707 for constant power) */
  for (i = 0; i < MAX_MAPPED_OSCILLATORS; ++i) {
    global_stereo_left_gains[i] = 0.707f;
    global_stereo_right_gains[i] = 0.707f;
  }
  
  filter_init_spectral_params(&global_spectral_filter_params, 
                              g_sp3ctra_config.poly_filter_cutoff_hz,
                              g_sp3ctra_config.poly_filter_env_depth_hz);
  log_info("SYNTH", "Global Spectral Filter Params: BaseCutoff=%.0fHz, EnvDepth=%.0fHz",
           global_spectral_filter_params.base_cutoff_hz,
           global_spectral_filter_params.filter_env_depth);

  lfo_init(&global_vibrato_lfo, g_sp3ctra_config.poly_lfo_rate_hz, 
           g_sp3ctra_config.poly_lfo_depth_semitones,
           (float)g_sp3ctra_config.sampling_frequency);
  log_info("SYNTH", "Global Vibrato LFO initialized: Rate=%.2f Hz, Depth=%.2f semitones",
           global_vibrato_lfo.rate_hz, global_vibrato_lfo.depth_semitones);

  for (i = 0; i < g_num_poly_voices; ++i) {
    poly_voices[i].fundamental_frequency = 0.0f;
    poly_voices[i].voice_state = ADSR_STATE_IDLE;
    poly_voices[i].midi_note_number = -1;
    poly_voices[i].last_velocity = 1.0f;
    poly_voices[i].last_triggered_order = 0; // Initialize trigger order
    for (j = 0; j < MAX_MAPPED_OSCILLATORS; ++j) {
      poly_voices[i].oscillators[j].phase = 0.0f;
    }
    adsr_init_envelope(&poly_voices[i].volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
                       g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
                       g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
    adsr_init_envelope(&poly_voices[i].filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
                       g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
                       g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
  log_info("SYNTH", "%d polyphonic voices initialized", g_num_poly_voices);
  log_info("SYNTH", "Polyphonic mode initialized (FFT computed in UDP thread)");
}

// --- Audio Processing ---

// Counter for rate-limiting polyphonic debug prints
// Print roughly once per second (assuming SAMPLING_FREQUENCY=44100,
// AUDIO_BUFFER_SIZE=512 -> ~86 calls/sec)
#define POLYPHONIC_PRINT_INTERVAL 86

void synth_polyphonicMode_process(float *audio_buffer_left,
                                  float *audio_buffer_right,
                                  unsigned int buffer_size) {
  if (audio_buffer_left == NULL || audio_buffer_right == NULL) {
    fprintf(stderr, "synth_polyphonicMode_process: audio_buffer is NULL\n");
    return;
  }
  memset(audio_buffer_left, 0, buffer_size * sizeof(float));
  memset(audio_buffer_right, 0, buffer_size * sizeof(float));

  // NOTE: global_smoothed_magnitudes is now pre-computed in udpThread
  // via read_preprocessed_fft_magnitudes() which reads from preprocessed_data.fft.magnitudes
  // No need to calculate FFT here anymore - just use the pre-computed values!

  for (unsigned int sample_idx = 0; sample_idx < buffer_size; ++sample_idx) {
    float master_sample_left = 0.0f;
    float master_sample_right = 0.0f;
    float lfo_modulation_value =
        lfo_process(&global_vibrato_lfo); // Process LFO per sample

    for (int v_idx = 0; v_idx < g_num_poly_voices; ++v_idx) {
      SynthVoice *current_voice = &poly_voices[v_idx];
      float volume_adsr_val = adsr_get_output(&current_voice->volume_adsr);
      float filter_adsr_val = adsr_get_output(&current_voice->filter_adsr);

      // Update voice state to IDLE when ADSR completes, but DON'T clear midi_note_number yet
      // This prevents race condition where Note Off arrives after ADSR reaches IDLE
      // The midi_note_number will be cleared when the voice is stolen by a new Note On
      if (current_voice->volume_adsr.state == ADSR_STATE_IDLE &&
          current_voice->voice_state != ADSR_STATE_IDLE) {
        current_voice->voice_state = ADSR_STATE_IDLE;
        // NOTE: midi_note_number is intentionally NOT cleared here to allow late Note Off messages
      }

      if (volume_adsr_val < 0.00001f &&
          current_voice->voice_state == ADSR_STATE_IDLE) {
        continue;
      }

      float modulated_cutoff_hz =
          global_spectral_filter_params.base_cutoff_hz +
          filter_adsr_val * global_spectral_filter_params.filter_env_depth;
      modulated_cutoff_hz =
          fmaxf(20.0f, fminf(modulated_cutoff_hz,
                             (float)g_sp3ctra_config.sampling_frequency / 2.0f - 1.0f));

      // Apply LFO to fundamental frequency
      float base_freq = current_voice->fundamental_frequency;
      float freq_mod_factor = powf(
          2.0f,
          (lfo_modulation_value * global_vibrato_lfo.depth_semitones) / 12.0f);
      float actual_fundamental_freq = base_freq * freq_mod_factor;

      float voice_sample_left = 0.0f;
      float voice_sample_right = 0.0f;
      // CPU Optimized harmonic processing loop
      // Calculate adaptive harmonic limits based on frequency and sample rate
      int max_harmonics = g_max_mapped_oscillators;

      // Reduce harmonics for high frequencies to save CPU
      if (actual_fundamental_freq > g_sp3ctra_config.poly_high_freq_harmonic_limit_hz) {
        max_harmonics =
            fminf(g_max_mapped_oscillators / 2, MAX_MAPPED_OSCILLATORS);
      } else if (actual_fundamental_freq > g_sp3ctra_config.poly_high_freq_harmonic_limit_hz / 2) {
        max_harmonics = fminf(g_max_mapped_oscillators, MAX_MAPPED_OSCILLATORS);
      }

      for (int osc_idx = 0; osc_idx < max_harmonics; ++osc_idx) {
        float harmonic_multiple;
        if (osc_idx == 0) {
          harmonic_multiple = 1.0f; // Fundamental frequency for osc_idx 0
        } else {
          // COLOR-BASED HARMONICITY: Use temperature to control harmonic/inharmonic behavior
          float h = global_harmonicity[osc_idx];  // [0,1]: 0=inharmonic, 1=harmonic
          
          if (h > 0.7f) {
            // Highly harmonic (warm colors: red, orange)
            // Use standard harmonic series with optional slight detune
            float detune_factor = global_detune_cents[osc_idx] / 1200.0f;  // Convert cents to ratio
            harmonic_multiple = (float)(osc_idx + 1) + detune_factor;
          } else if (h > 0.3f) {
            // Semi-harmonic (neutral colors: yellow, green)
            // Use harmonic series with stronger detune for "piano/guitar" effect
            float detune_factor = global_detune_cents[osc_idx] / 1200.0f;
            harmonic_multiple = (float)(osc_idx + 1) + detune_factor;
          } else {
            // Inharmonic (cold colors: blue, cyan)
            // Use inharmonic ratios for "bell/percussion" effect
            harmonic_multiple = global_inharmonic_ratios[osc_idx];
          }
        }
        float osc_freq = actual_fundamental_freq * harmonic_multiple;

        // Nyquist check: if harmonic frequency is too high, stop adding
        // harmonics
        if (osc_freq >= (float)g_sp3ctra_config.sampling_frequency / 2.0f) {
          break;
        }

        float smoothed_amplitude = global_smoothed_magnitudes[osc_idx];

        // CPU optimization: Skip harmonics with very low amplitude
        if (smoothed_amplitude < g_sp3ctra_config.poly_min_audible_amplitude) {
          // Still update phase to maintain continuity
          float phase_increment = TWO_PI * osc_freq / (float)g_sp3ctra_config.sampling_frequency;
          current_voice->oscillators[osc_idx].phase += phase_increment;
          if (current_voice->oscillators[osc_idx].phase >= TWO_PI) {
            current_voice->oscillators[osc_idx].phase -= TWO_PI;
          }
          continue;
        }

        float phase_increment = TWO_PI * osc_freq / (float)g_sp3ctra_config.sampling_frequency;

        float amplitude_after_gamma = powf(smoothed_amplitude, g_sp3ctra_config.poly_amplitude_gamma);
        if (smoothed_amplitude < 0.0f &&
            (g_sp3ctra_config.poly_amplitude_gamma != floorf(g_sp3ctra_config.poly_amplitude_gamma))) {
          amplitude_after_gamma = 0.0f;
        }

        float attenuation = 1.0f;
        if (modulated_cutoff_hz > 1.0f) {
          if (osc_freq > 0.001f) {
            float ratio = osc_freq / modulated_cutoff_hz;
            attenuation = 1.0f / sqrtf(1.0f + ratio * ratio);
          }
        } else {
          attenuation = (osc_freq < 1.0f) ? 1.0f : 0.00001f;
        }

        float final_amplitude = amplitude_after_gamma * attenuation;

        // Only calculate sine if amplitude is significant enough
        if (final_amplitude > g_sp3ctra_config.poly_min_audible_amplitude) {
          float osc_sample =
              final_amplitude * sinf(current_voice->oscillators[osc_idx].phase);
          
          // Apply spectral panning: each harmonic gets its own stereo position
          voice_sample_left += osc_sample * global_stereo_left_gains[osc_idx];
          voice_sample_right += osc_sample * global_stereo_right_gains[osc_idx];
        }

        current_voice->oscillators[osc_idx].phase += phase_increment;
        if (current_voice->oscillators[osc_idx].phase >= TWO_PI) {
          current_voice->oscillators[osc_idx].phase -= TWO_PI;
        }
      }

      // Apply voice-level modulations (ADSR, velocity) to both channels
      voice_sample_left *= volume_adsr_val;
      voice_sample_left *= current_voice->last_velocity;
      voice_sample_right *= volume_adsr_val;
      voice_sample_right *= current_voice->last_velocity;
      
      master_sample_left += voice_sample_left;
      master_sample_right += voice_sample_right;
    }

    // Apply master volume to both channels
    master_sample_left *= g_sp3ctra_config.poly_master_volume;
    master_sample_right *= g_sp3ctra_config.poly_master_volume;

    // Clipping for left channel
    if (master_sample_left > 1.0f)
      master_sample_left = 1.0f;
    else if (master_sample_left < -1.0f)
      master_sample_left = -1.0f;
    
    // Clipping for right channel
    if (master_sample_right > 1.0f)
      master_sample_right = 1.0f;
    else if (master_sample_right < -1.0f)
      master_sample_right = -1.0f;

    // Output true stereo with spectral panning
    audio_buffer_left[sample_idx] = master_sample_left;
    audio_buffer_right[sample_idx] = master_sample_right;
  }
  
  /* DEBUG: Log generated stereo output periodically */
  static int output_debug_counter = 0;
  if (++output_debug_counter >= 100) { // Every 100 buffers (~100ms at 1kHz)
    output_debug_counter = 0;
    float sum_left = 0.0f, sum_right = 0.0f;
    for (unsigned int i = 0; i < buffer_size; i++) {
      sum_left += fabsf(audio_buffer_left[i]);
      sum_right += fabsf(audio_buffer_right[i]);
    }
    float avg_left = sum_left / buffer_size;
    float avg_right = sum_right / buffer_size;
    printf("[POLY_OUTPUT] Generated L=%.6f R=%.6f (diff=%.6f, ratio=%.3f)\n",
           avg_left, avg_right, avg_left - avg_right,
           (avg_right > 0.000001f) ? (avg_left / avg_right) : 0.0f);
  }
}

// --- Image & FFT Processing ---
/**
 * @brief Read pre-computed FFT magnitudes from preprocessed data
 * This replaces the old process_image_data_for_fft() function
 * FFT is now computed in UDP thread for better performance
 */
static void read_preprocessed_fft_magnitudes(DoubleBuffer *image_db) {
  if (image_db == NULL) {
    log_error("SYNTH", "read_preprocessed_fft_magnitudes: image_db is NULL");
    return;
  }
  
  pthread_mutex_lock(&image_db->mutex);
  
  /* Check if FFT data is valid */
  if (image_db->preprocessed_data.polyphonic.valid) {
    /* Copy pre-computed magnitudes to global array */
    memcpy(global_smoothed_magnitudes, 
           image_db->preprocessed_data.polyphonic.magnitudes,
           sizeof(global_smoothed_magnitudes));
    
    /* Copy pre-computed stereo gains for spectral panning */
    memcpy(global_stereo_left_gains,
           image_db->preprocessed_data.polyphonic.left_gains,
           sizeof(global_stereo_left_gains));
    memcpy(global_stereo_right_gains,
           image_db->preprocessed_data.polyphonic.right_gains,
           sizeof(global_stereo_right_gains));
    
    /* Copy pre-computed harmonicity data for color-based timbre control */
    memcpy(global_harmonicity,
           image_db->preprocessed_data.polyphonic.harmonicity,
           sizeof(global_harmonicity));
    memcpy(global_detune_cents,
           image_db->preprocessed_data.polyphonic.detune_cents,
           sizeof(global_detune_cents));
    memcpy(global_inharmonic_ratios,
           image_db->preprocessed_data.polyphonic.inharmonic_ratios,
           sizeof(global_inharmonic_ratios));
    
    /* DEBUG: Log stereo gains periodically */
    static int debug_counter = 0;
    if (++debug_counter >= 100) { // Every 100 calls (~100ms at 1kHz)
      debug_counter = 0;
      printf("[POLY_STEREO] Gains copied - First 8 harmonics:\n");
      for (int i = 0; i < 8; i++) {
        printf("  H%d: L=%.3f R=%.3f (diff=%.3f)\n", 
               i, global_stereo_left_gains[i], global_stereo_right_gains[i],
               global_stereo_left_gains[i] - global_stereo_right_gains[i]);
      }
    }
  } else {
    /* FFT data not valid - use silence (all zeros) and center panning */
    memset(global_smoothed_magnitudes, 0, sizeof(global_smoothed_magnitudes));
    
    /* Reset to center panning (0.707 for constant power) */
    for (int i = 0; i < MAX_MAPPED_OSCILLATORS; ++i) {
      global_stereo_left_gains[i] = 0.707f;
      global_stereo_right_gains[i] = 0.707f;
    }
  }
  
  pthread_mutex_unlock(&image_db->mutex);
}

// --- Main Thread Function ---
void *synth_polyphonicMode_thread_func(void *arg) {
  DoubleBuffer *image_db = NULL;
  if (arg != NULL) {
    Context *ctx = (Context *)arg;
    image_db = ctx->doubleBuffer;
  } else {
    log_warning("SYNTH", "Polyphonic thread: No context provided, no DoubleBuffer available");
  }
  
  // Set RT priority for polyphonic synthesis thread (priority 75, between callback at 70 and additive workers at 80)
  // Use the unified synth_set_rt_priority() function with macOS support
  extern int synth_set_rt_priority(pthread_t thread, int priority);
  if (synth_set_rt_priority(pthread_self(), 75) != 0) {
    log_warning("SYNTH", "Polyphonic thread: Failed to set RT priority (continuing without RT)");
  }
  
  log_info("SYNTH", "Polyphonic synthesis thread started");
  srand(time(NULL));

  while (keepRunning) {
    // Read pre-computed FFT magnitudes from UDP thread preprocessing
    // NEW ARCHITECTURE: FFT is now computed in UDP thread for better RT performance
    if (image_db != NULL) {
      read_preprocessed_fft_magnitudes(image_db);
    } else {
      log_warning("SYNTH", "Polyphonic thread: No DoubleBuffer, using silence");
      memset(global_smoothed_magnitudes, 0, sizeof(global_smoothed_magnitudes));
    }

    int local_producer_idx;
    pthread_mutex_lock(&polyphonic_buffer_index_mutex);
    local_producer_idx = polyphonic_current_buffer_index;
    pthread_mutex_unlock(&polyphonic_buffer_index_mutex);

    // RT-SAFE: Wait for buffer to be consumed with timeout and exponential backoff
    int wait_iterations = 0;
    const int MAX_WAIT_ITERATIONS = 500; // ~50ms max wait (increased from 100)
    
    while (__atomic_load_n(&polyphonic_audio_buffers[local_producer_idx].ready, __ATOMIC_ACQUIRE) == 1 && 
           keepRunning && wait_iterations < MAX_WAIT_ITERATIONS) {
      // Optimized exponential backoff: more aggressive at start, then backs off
      int sleep_us = (wait_iterations < 5) ? 5 :      // 5µs for first 5 iterations
                     (wait_iterations < 20) ? 20 :     // 20µs for next 15 iterations
                     (wait_iterations < 100) ? 50 :    // 50µs for next 80 iterations
                     100;                              // 100µs for remaining iterations
      struct timespec sleep_time = {0, sleep_us * 1000}; // Convert µs to ns
      nanosleep(&sleep_time, NULL);
      wait_iterations++;
    }
    
    if (!keepRunning) {
      goto cleanup_thread;
    }
    
    // If timeout, log warning but continue (graceful degradation)
    if (wait_iterations >= MAX_WAIT_ITERATIONS) {
      log_warning("SYNTH", "Polyphonic: Buffer wait timeout (callback too slow)");
    }
    
    pthread_mutex_lock(&polyphonic_audio_buffers[local_producer_idx].mutex);
    
    // TRUE STEREO: Pass separate L/R buffers for spectral panning
    synth_polyphonicMode_process(
        polyphonic_audio_buffers[local_producer_idx].data_left,
        polyphonic_audio_buffers[local_producer_idx].data_right,
        g_sp3ctra_config.audio_buffer_size);
    
    // Record timestamp when buffer is written
    struct timeval tv;
    gettimeofday(&tv, NULL);
    polyphonic_audio_buffers[local_producer_idx].write_timestamp_us = 
        (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    
    polyphonic_audio_buffers[local_producer_idx].ready = 1;
    pthread_cond_signal(&polyphonic_audio_buffers[local_producer_idx].cond);
    pthread_mutex_unlock(&polyphonic_audio_buffers[local_producer_idx].mutex);

    pthread_mutex_lock(&polyphonic_buffer_index_mutex);
    polyphonic_current_buffer_index = 1 - local_producer_idx;
    pthread_mutex_unlock(&polyphonic_buffer_index_mutex);
  }

cleanup_thread:
  log_info("SYNTH", "Polyphonic synthesis thread stopping");
  return NULL;
}

// --- ADSR Envelope Implementation ---
static void adsr_init_envelope(AdsrEnvelope *env, float attack_s, float decay_s,
                               float sustain_level, float release_s,
                               float sample_rate) {
  env->attack_s = attack_s;
  env->decay_s = decay_s;
  env->sustain_level = sustain_level;
  env->release_s = release_s;
  env->attack_time_samples =
      (attack_s > 0.0f) ? fmaxf(1.0f, attack_s * sample_rate) : 0.0f;
  env->decay_time_samples =
      (decay_s > 0.0f) ? fmaxf(1.0f, decay_s * sample_rate) : 0.0f;
  env->release_time_samples =
      (release_s > 0.0f) ? fmaxf(1.0f, release_s * sample_rate) : 0.0f;
  env->attack_increment = (env->attack_time_samples > 0.0f)
                              ? (1.0f / env->attack_time_samples)
                              : 1.0f;
  env->decay_decrement =
      (env->decay_time_samples > 0.0f && (1.0f - sustain_level) > 0.0f)
          ? ((1.0f - sustain_level) / env->decay_time_samples)
          : (1.0f - sustain_level);
  env->state = ADSR_STATE_IDLE;
  env->current_output = 0.0f;
  env->current_samples = 0;
}

// New function to update ADSR settings for an already active envelope without
// resetting its state
static void
adsr_update_settings_and_recalculate_rates(AdsrEnvelope *env, float attack_s,
                                           float decay_s, float sustain_level,
                                           float release_s, float sample_rate) {
  env->attack_s = attack_s;
  env->decay_s = decay_s;
  env->sustain_level = sustain_level;
  env->release_s = release_s;

  env->attack_time_samples =
      (attack_s > 0.0f) ? fmaxf(1.0f, attack_s * sample_rate) : 0.0f;
  env->decay_time_samples =
      (decay_s > 0.0f) ? fmaxf(1.0f, decay_s * sample_rate) : 0.0f;
  env->release_time_samples =
      (release_s > 0.0f) ? fmaxf(1.0f, release_s * sample_rate) : 0.0f;

  // Recalculate increments/decrements
  // For attack_increment, it's generally set when attack is triggered or
  // re-triggered. If an attack is in progress and its time changes,
  // adsr_get_output would need more complex logic to adjust smoothly. For now,
  // we primarily focus on decay and release adjustments.
  env->attack_increment =
      (env->attack_time_samples > 0.0f)
          ? (1.0f / env->attack_time_samples)
          : 1.0f; // Default for re-calc, trigger_attack refines

  // Decay decrement:
  // If currently in DECAY state and current_output is above the new
  // sustain_level
  if (env->state == ADSR_STATE_DECAY &&
      env->current_output > env->sustain_level) {
    float time_remaining_decay = env->decay_time_samples - env->current_samples;
    if (time_remaining_decay > 0.0f) {
      env->decay_decrement =
          (env->current_output - env->sustain_level) / time_remaining_decay;
    } else { // Time is up or negative, should go to sustain level instantly or
             // use a minimal decrement
      env->decay_decrement =
          (env->current_output -
           env->sustain_level); // Effectively instant if current_samples >=
                                // decay_time_samples
    }
  } else { // Standard calculation (e.g. for init or if not in decay, or if
           // current_output <= sustain_level)
    env->decay_decrement =
        (env->decay_time_samples > 0.0f &&
         (1.0f - env->sustain_level) > 0.00001f)
            ? ((1.0f - env->sustain_level) / env->decay_time_samples)
            : (1.0f - env->sustain_level); // Default or if sustain is 1.0 or
                                           // decay time is 0
    if (env->decay_decrement < 0.0f)
      env->decay_decrement = 0.0f; // Ensure non-negative
  }

  // Release decrement:
  // If currently in RELEASE state and current_output is above 0
  if (env->state == ADSR_STATE_RELEASE && env->current_output > 0.0f) {
    float time_remaining_release =
        env->release_time_samples - env->current_samples;
    if (time_remaining_release > 0.0f) {
      env->release_decrement = env->current_output / time_remaining_release;
    } else { // Time is up or negative, should go to 0 instantly
      env->release_decrement = env->current_output; // Effectively instant
    }
  } else { // Standard calculation (e.g. for init or if not in release)
    env->release_decrement =
        (env->release_time_samples > 0.0f &&
         env->current_output > 0.00001f) // Check current_output for release
            ? (env->current_output /
               env->release_time_samples) // This assumes release starts from
                                          // current_output
            : env->current_output;        // If release time is 0, instant drop
    if (env->release_decrement < 0.0f)
      env->release_decrement = 0.0f; // Ensure non-negative
  }
  // Note: The original adsr_init_envelope's release_decrement was calculated
  // based on sustain_level if current_output was 0. Here, for an active
  // envelope, it should always be based on current_output. If
  // adsr_trigger_release is called, it will set its own release_decrement.
}

static void adsr_trigger_attack(AdsrEnvelope *env) {
  env->state = ADSR_STATE_ATTACK;
  env->current_samples = 0;
  // Always reset current_output to 0 for a new attack phase, ensuring full
  // attack from zero.
  env->current_output = 0.0f;

  if (env->attack_time_samples > 0.0f) {
    env->attack_increment = 1.0f / env->attack_time_samples;
  } else {                        // Attack time is zero or negative
    env->current_output = 1.0f;   // Instantly go to peak
    env->attack_increment = 0.0f; // No increment needed
    if (env->sustain_level < 1.0f && env->decay_time_samples > 0.0f) {
      env->state = ADSR_STATE_DECAY;
      if (env->decay_time_samples > 0.0f) {
        env->decay_decrement =
            (1.0f - env->sustain_level) / env->decay_time_samples;
      } else {
        env->decay_decrement = (1.0f - env->sustain_level);
        env->current_output = env->sustain_level;
        env->state = ADSR_STATE_SUSTAIN;
      }
    } else {
      env->state = ADSR_STATE_SUSTAIN;
    }
  }
}

static void adsr_trigger_release(AdsrEnvelope *env) {
  env->state = ADSR_STATE_RELEASE;
  env->current_samples = 0;
  if (env->release_time_samples > 0.0f && env->current_output > 0.0f) {
    env->release_decrement = env->current_output / env->release_time_samples;
  } else {
    env->release_decrement = env->current_output;
    env->current_output = 0.0f;
    env->state = ADSR_STATE_IDLE;
  }
}

static float adsr_get_output(AdsrEnvelope *env) {
  switch (env->state) {
  case ADSR_STATE_IDLE:
    break;
  case ADSR_STATE_ATTACK:
    env->current_output += env->attack_increment;
    env->current_samples++;
    if (env->current_output >= 1.0f ||
        (env->attack_time_samples > 0.0f &&
         env->current_samples >= env->attack_time_samples)) {
      env->current_output = 1.0f;
      env->state = ADSR_STATE_DECAY;
      env->current_samples = 0;
      if (env->decay_time_samples > 0.0f) {
        env->decay_decrement =
            (1.0f - env->sustain_level) / env->decay_time_samples;
      } else {
        env->current_output = env->sustain_level;
        env->state = ADSR_STATE_SUSTAIN;
      }
    }
    break;
  case ADSR_STATE_DECAY:
    env->current_output -= env->decay_decrement;
    env->current_samples++;
    if (env->current_output <= env->sustain_level ||
        (env->decay_time_samples > 0.0f &&
         env->current_samples >= env->decay_time_samples)) {
      env->current_output = env->sustain_level;
      env->state = ADSR_STATE_SUSTAIN;
    }
    break;
  case ADSR_STATE_SUSTAIN:
    break;
  case ADSR_STATE_RELEASE:
    env->current_output -= env->release_decrement;
    env->current_samples++;
    if (env->current_output <= 0.0f ||
        (env->release_time_samples > 0.0f &&
         env->current_samples >= env->release_time_samples)) {
      env->current_output = 0.0f;
      env->state = ADSR_STATE_IDLE;
    }
    break;
  }
  if (env->current_output > 1.0f)
    env->current_output = 1.0f;
  if (env->current_output < 0.0f)
    env->current_output = 0.0f;
  return env->current_output;
}

// --- MIDI Note Handling ---
static float midi_note_to_frequency(int noteNumber) {
  if (noteNumber < 0 || noteNumber > 127) {
    fprintf(stderr, "Invalid MIDI note number: %d\n", noteNumber);
    return 0.0f;
  }
  return 440.0f * powf(2.0f, (float)(noteNumber - 69) / 12.0f);
}

void synth_polyphonic_note_on(int noteNumber, int velocity) {
  if (velocity <= 0) {
    synth_polyphonic_note_off(noteNumber);
    return;
  }

  g_current_trigger_order++; // Increment global trigger order

  int voice_idx = -1;

  // Priority 1: Find an IDLE voice
  for (int i = 0; i < g_num_poly_voices; ++i) {
    if (poly_voices[i].voice_state == ADSR_STATE_IDLE) {
      voice_idx = i;
      break;
    }
  }

  // Priority 2: Steal the voice in RELEASE with the lowest envelope output
  // This allows notes to finish their release phase naturally before being stolen
  if (voice_idx == -1) {
    float lowest_env_output = 2.0f; // Greater than max envelope output (1.0)
    int candidate_idx = -1;
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].voice_state == ADSR_STATE_RELEASE) {
        if (poly_voices[i].volume_adsr.current_output < lowest_env_output) {
          lowest_env_output = poly_voices[i].volume_adsr.current_output;
          candidate_idx = i;
        }
      }
    }
    if (candidate_idx != -1) {
      voice_idx = candidate_idx;
      printf("POLYPHONIC: Stealing quietest release voice %d for note %d (Env: %.2f)\n",
             voice_idx, noteNumber, lowest_env_output);
    }
  }

  // Priority 3: As last resort, find the oldest active voice (ATTACK, DECAY, SUSTAIN)
  // This should rarely happen if release times are reasonable
  if (voice_idx == -1) {
    unsigned long long oldest_order =
        g_current_trigger_order +
        1; // Initialize with a value guaranteed to be newer
    int candidate_idx = -1;
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].voice_state != ADSR_STATE_RELEASE &&
          poly_voices[i].voice_state !=
              ADSR_STATE_IDLE) { // i.e., ATTACK, DECAY, SUSTAIN
        if (poly_voices[i].last_triggered_order < oldest_order) {
          oldest_order = poly_voices[i].last_triggered_order;
          candidate_idx = i;
        }
      }
    }
    if (candidate_idx != -1) {
      voice_idx = candidate_idx;
      printf("POLYPHONIC: Last resort - stealing oldest active voice %d for note %d (Order: %llu)\n",
             voice_idx, noteNumber, oldest_order);
    }
  }

  // Fallback: If absolutely no voice found (e.g., all voices are in very recent
  // ATTACK and g_num_poly_voices is small) or if all voices are in RELEASE but
  // none were selected by prio 3 (should not happen if there's at least one
  // release voice) For now, we'll just steal voice 0 as a last resort if
  // voice_idx is still -1. A more robust system might refuse the note or have a
  // more complex fallback.
  if (voice_idx == -1) {
    voice_idx = 0; // Default to stealing voice 0 if no other candidate found
    printf("POLYPHONIC: Critical fallback. Stealing voice 0 for note %d\n",
           noteNumber);
  }

  SynthVoice *voice = &poly_voices[voice_idx];

  // Initialize the chosen voice's envelopes with current global ADSR settings
  // This ensures it starts fresh, respecting the latest global parameters.
  adsr_init_envelope(&voice->volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
                     g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
                     g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  adsr_init_envelope(&voice->filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
                     g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
                     g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);

  voice->fundamental_frequency = midi_note_to_frequency(noteNumber);
  voice->midi_note_number = noteNumber;
  voice->voice_state = ADSR_STATE_ATTACK; // Will be set by adsr_trigger_attack,
                                          // but good to be explicit
  voice->last_velocity = (float)velocity / 127.0f;
  voice->last_triggered_order = g_current_trigger_order;

  for (int i = 0; i < MAX_MAPPED_OSCILLATORS; ++i) {
    voice->oscillators[i].phase = 0.0f;
  }

  adsr_trigger_attack(
      &voice->volume_adsr); // This will now use the freshly initialized params
  adsr_trigger_attack(
      &voice->filter_adsr); // and set state to ATTACK, current_output to 0

  printf("POLYPHONIC: Voice %d Note On: %d, Vel: %d (Norm: %.2f), Freq: %.2f "
         "Hz, Order: %llu -> ADSR Attack\n",
         voice_idx, noteNumber, velocity, voice->last_velocity,
         voice->fundamental_frequency, voice->last_triggered_order);
}

void synth_polyphonic_note_off(int noteNumber) {
  // Priority 1: Find the OLDEST voice with this note number that is ACTIVE (not in RELEASE or IDLE)
  int oldest_voice_idx = -1;
  unsigned long long oldest_order = g_current_trigger_order + 1;
  
  for (int i = 0; i < g_num_poly_voices; ++i) {
    if (poly_voices[i].midi_note_number == noteNumber &&
        poly_voices[i].voice_state != ADSR_STATE_IDLE &&
        poly_voices[i].voice_state != ADSR_STATE_RELEASE) {
      // Find the oldest (lowest trigger order) voice with this note
      if (poly_voices[i].last_triggered_order < oldest_order) {
        oldest_order = poly_voices[i].last_triggered_order;
        oldest_voice_idx = i;
      }
    }
  }
  
  // Priority 2: If no active voice found, search in RELEASE voices (duplicate/late Note Off)
  // This handles duplicate Note Off messages or Note Offs that arrive while voice is already releasing
  if (oldest_voice_idx == -1) {
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].midi_note_number == noteNumber &&
          poly_voices[i].voice_state == ADSR_STATE_RELEASE) {
        oldest_voice_idx = i;
        log_debug("SYNTH_POLY", "Duplicate Note Off %d handled via RELEASE voice %d (already releasing)", 
                  noteNumber, i);
        break; // Take the first RELEASE voice found with this note
      }
    }
  }
  
  // Priority 3: If still not found, search in IDLE voices (grace period for very late Note Off)
  // This handles the race condition where ADSR reached IDLE before Note Off arrived
  if (oldest_voice_idx == -1) {
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].midi_note_number == noteNumber &&
          poly_voices[i].voice_state == ADSR_STATE_IDLE) {
        oldest_voice_idx = i;
        log_debug("SYNTH_POLY", "Late Note Off %d handled via IDLE voice %d (grace period)", 
                  noteNumber, i);
        break; // Take the first IDLE voice found with this note
      }
    }
  }
  
  // Release the voice if found
  if (oldest_voice_idx != -1) {
    // Only trigger release if not already IDLE (avoid re-triggering IDLE voices)
    if (poly_voices[oldest_voice_idx].voice_state != ADSR_STATE_IDLE) {
      adsr_trigger_release(&poly_voices[oldest_voice_idx].volume_adsr);
      adsr_trigger_release(&poly_voices[oldest_voice_idx].filter_adsr);
      poly_voices[oldest_voice_idx].voice_state = ADSR_STATE_RELEASE;
      printf("POLYPHONIC: Voice %d Note Off: %d -> ADSR Release\n", 
             oldest_voice_idx, noteNumber);
    }
    // Clear midi_note_number now that Note Off has been processed
    poly_voices[oldest_voice_idx].midi_note_number = -1;
  } else {
    // DEBUG: Log when no voice is found for Note Off (should be very rare now)
    printf("POLYPHONIC: WARNING - Note Off %d: No voice found (neither active nor idle)!\n", noteNumber);
    printf("POLYPHONIC: Voice states: ");
    for (int i = 0; i < g_num_poly_voices; ++i) {
      printf("[%d:note=%d,state=%d] ", i, poly_voices[i].midi_note_number, poly_voices[i].voice_state);
    }
    printf("\n");
  }
}

// --- Filter Implementation (Simplified for Spectral Params) ---
static void filter_init_spectral_params(SpectralFilterParams *fp,
                                        float base_cutoff_hz,
                                        float filter_env_depth) {
  fp->base_cutoff_hz = base_cutoff_hz;
  fp->filter_env_depth = filter_env_depth;
}

// --- LFO Implementation ---
static void lfo_init(LfoState *lfo, float rate_hz, float depth_semitones,
                     float sample_rate) {
  lfo->rate_hz = rate_hz;
  lfo->depth_semitones = depth_semitones;
  lfo->phase = 0.0f;
  lfo->phase_increment = TWO_PI * rate_hz / sample_rate;
  lfo->current_output = 0.0f;
}

static float lfo_process(LfoState *lfo) {
  lfo->current_output = sinf(lfo->phase);
  lfo->phase += lfo->phase_increment;
  if (lfo->phase >= TWO_PI) {
    lfo->phase -= TWO_PI;
  }
  return lfo->current_output;
}

// --- ADSR Parameter Setters ---
void synth_polyphonic_set_volume_adsr_attack(float attack_s) {
  if (attack_s < 0.0f)
    attack_s = 0.0f;
  g_sp3ctra_config.poly_volume_adsr_attack_s = attack_s;
  // printf("SYNTH_FFT: Global Volume ADSR Attack set to: %.3f s\n", attack_s);
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
        g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
        g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_volume_adsr_decay(float decay_s) {
  if (decay_s < 0.0f)
    decay_s = 0.0f;
  g_sp3ctra_config.poly_volume_adsr_decay_s = decay_s;
  // printf("SYNTH_FFT: Global Volume ADSR Decay set to: %.3f s\n", decay_s);
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
        g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
        g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_volume_adsr_sustain(float sustain_level) {
  if (sustain_level < 0.0f)
    sustain_level = 0.0f;
  if (sustain_level > 1.0f)
    sustain_level = 1.0f;
  g_sp3ctra_config.poly_volume_adsr_sustain_level = sustain_level;
  // printf("SYNTH_FFT: Global Volume ADSR Sustain set to: %.2f\n",
  // sustain_level);
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
        g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
        g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_volume_adsr_release(float release_s) {
  if (release_s < 0.0f)
    release_s = 0.0f;
  g_sp3ctra_config.poly_volume_adsr_release_s = release_s;
  // printf("SYNTH_FFT: Global Volume ADSR Release set to: %.3f s\n",
  // release_s);
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, g_sp3ctra_config.poly_volume_adsr_attack_s,
        g_sp3ctra_config.poly_volume_adsr_decay_s, g_sp3ctra_config.poly_volume_adsr_sustain_level,
        g_sp3ctra_config.poly_volume_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

// --- LFO Parameter Setters ---
void synth_polyphonic_set_vibrato_rate(float rate_hz) {
  if (rate_hz < 0.0f)
    rate_hz = 0.0f;
  // Potentially add a max rate limit, e.g., 20Hz or 30Hz
  global_vibrato_lfo.rate_hz = rate_hz;
  global_vibrato_lfo.phase_increment =
      TWO_PI * rate_hz / (float)g_sp3ctra_config.sampling_frequency;
  // printf("SYNTH_FFT: Global Vibrato LFO Rate set to: %.2f Hz\n", rate_hz);
}

void synth_polyphonic_set_vibrato_depth(float depth_semitones) {
  // Depth can be positive or negative, typically small values like -2 to 2
  // semitones
  global_vibrato_lfo.depth_semitones = depth_semitones;
  // printf("SYNTH_FFT: Global Vibrato LFO Depth set to: %.2f semitones\n",
  //        depth_semitones);
}

// --- Filter Parameter Setters ---
void synth_polyphonic_set_filter_cutoff(float cutoff_hz) {
  if (cutoff_hz < 20.0f)
    cutoff_hz = 20.0f;
  if (cutoff_hz > (float)g_sp3ctra_config.sampling_frequency / 2.0f)
    cutoff_hz = (float)g_sp3ctra_config.sampling_frequency / 2.0f;
  
  global_spectral_filter_params.base_cutoff_hz = cutoff_hz;
  // printf("SYNTH_FFT: Global Filter Cutoff set to: %.0f Hz\n", cutoff_hz);
}

void synth_polyphonic_set_filter_env_depth(float depth_hz) {
  // Depth can be positive or negative
  // Typical range: -10000 to +10000 Hz
  global_spectral_filter_params.filter_env_depth = depth_hz;
  // printf("SYNTH_FFT: Global Filter Env Depth set to: %.0f Hz\n", depth_hz);
}

// --- Filter ADSR Parameter Setters ---
void synth_polyphonic_set_filter_adsr_attack(float attack_s) {
  if (attack_s < 0.0f)
    attack_s = 0.0f;
  g_sp3ctra_config.poly_filter_adsr_attack_s = attack_s;
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
        g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
        g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_filter_adsr_decay(float decay_s) {
  if (decay_s < 0.0f)
    decay_s = 0.0f;
  g_sp3ctra_config.poly_filter_adsr_decay_s = decay_s;
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
        g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
        g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_filter_adsr_sustain(float sustain_level) {
  if (sustain_level < 0.0f)
    sustain_level = 0.0f;
  if (sustain_level > 1.0f)
    sustain_level = 1.0f;
  g_sp3ctra_config.poly_filter_adsr_sustain_level = sustain_level;
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
        g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
        g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}

void synth_polyphonic_set_filter_adsr_release(float release_s) {
  if (release_s < 0.0f)
    release_s = 0.0f;
  g_sp3ctra_config.poly_filter_adsr_release_s = release_s;
  for (int i = 0; i < g_num_poly_voices; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].filter_adsr, g_sp3ctra_config.poly_filter_adsr_attack_s,
        g_sp3ctra_config.poly_filter_adsr_decay_s, g_sp3ctra_config.poly_filter_adsr_sustain_level,
        g_sp3ctra_config.poly_filter_adsr_release_s, (float)g_sp3ctra_config.sampling_frequency);
  }
}
