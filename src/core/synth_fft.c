/*
 * synth_fft.c
 */

#include "synth_fft.h"
#include "config.h"
#include "context.h"
#include "doublebuffer.h"
#include "error.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
static void process_image_data_for_fft(DoubleBuffer *image_db);
static void generate_test_data_for_fft(void);

// --- Synth Parameters & Globals ---
#define NORM_FACTOR_BIN0 881280.0f * 1.1f
#define NORM_FACTOR_HARMONICS 220320.0f * 2.0f
#define MASTER_VOLUME 0.10f
#define AMPLITUDE_SMOOTHING_ALPHA 0.1f // Increased for more responsiveness
#define AMPLITUDE_GAMMA 2.0f           // Increased to enhance variations

// Polyphony related globals
unsigned long long g_current_trigger_order =
    0; // Global trigger order counter, starts at 0
SynthVoice poly_voices[NUM_POLY_VOICES];
float global_smoothed_magnitudes[MAX_MAPPED_OSCILLATORS];
SpectralFilterParams global_spectral_filter_params;
LfoState global_vibrato_lfo; // Definition for the global LFO

// Global default ADSR parameters
static float G_VOLUME_ADSR_ATTACK_S = 0.01f;
static float G_VOLUME_ADSR_DECAY_S = 0.1f;
static float G_VOLUME_ADSR_SUSTAIN_LEVEL = 0.8f;
static float G_VOLUME_ADSR_RELEASE_S = 0.2f;

static float G_FILTER_ADSR_ATTACK_S = 0.02f;
static float G_FILTER_ADSR_DECAY_S = 0.2f;
static float G_FILTER_ADSR_SUSTAIN_LEVEL = 0.1f;
static float G_FILTER_ADSR_RELEASE_S = 0.3f;

// Default LFO parameters
static float G_LFO_RATE_HZ = 5.0f;
static float G_LFO_DEPTH_SEMITONES = 0.25f;

// FFT related globals
FftAudioDataBuffer fft_audio_buffers[2];
volatile int fft_current_buffer_index = 0;
pthread_mutex_t fft_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

GrayscaleLine image_line_history[MOVING_AVERAGE_WINDOW_SIZE];
int history_write_index = 0;
int history_fill_count = 0;
pthread_mutex_t image_history_mutex = PTHREAD_MUTEX_INITIALIZER;
FftContext fft_context;

extern volatile int keepRunning;

// --- Initialization ---
void synth_fftMode_init(void) {
  printf("Initializing synth_fftMode (Polyphonic with LFO)...\n");

  for (int i = 0; i < 2; ++i) {
    if (pthread_mutex_init(&fft_audio_buffers[i].mutex, NULL) != 0) {
      die("Failed to initialize FFT audio buffer mutex");
    }
    if (pthread_cond_init(&fft_audio_buffers[i].cond, NULL) != 0) {
      die("Failed to initialize FFT audio buffer condition variable");
    }
    fft_audio_buffers[i].ready = 0;
    memset(fft_audio_buffers[i].data, 0, AUDIO_BUFFER_SIZE * sizeof(float));
  }
  if (pthread_mutex_init(&fft_buffer_index_mutex, NULL) != 0) {
    die("Failed to initialize FFT buffer index mutex");
  }
  fft_current_buffer_index = 0;
  if (pthread_mutex_init(&image_history_mutex, NULL) != 0) {
    die("Failed to initialize image history mutex");
  }
  history_write_index = 0;
  // Pre-fill image_line_history with a default white line
  // This ensures that the FFT has valid, non-zero data from the start.
  float default_white_line[CIS_MAX_PIXELS_NB];
  for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
    default_white_line[i] = 255.0f; // Max brightness for a white line
  }
  for (int i = 0; i < MOVING_AVERAGE_WINDOW_SIZE; ++i) {
    memcpy(image_line_history[i].line_data, default_white_line,
           CIS_MAX_PIXELS_NB * sizeof(float));
  }
  history_fill_count =
      MOVING_AVERAGE_WINDOW_SIZE; // History is now full with default data
  printf("synth_fftMode: Image history pre-filled with default white lines. "
         "Fill count: %d\n",
         history_fill_count);

  fft_context.fft_cfg = kiss_fftr_alloc(CIS_MAX_PIXELS_NB, 0, NULL, NULL);
  if (fft_context.fft_cfg == NULL) {
    die("Failed to initialize FFT configuration");
  }
  memset(fft_context.fft_input, 0, sizeof(fft_context.fft_input));
  memset(fft_context.fft_output, 0, sizeof(fft_context.fft_output));

  memset(global_smoothed_magnitudes, 0, sizeof(global_smoothed_magnitudes));
  filter_init_spectral_params(&global_spectral_filter_params, 8000.0f,
                              -7800.0f);
  printf("Global Spectral Filter Params: BaseCutoff=%.0fHz, EnvDepth=%.0fHz\n",
         global_spectral_filter_params.base_cutoff_hz,
         global_spectral_filter_params.filter_env_depth);

  lfo_init(&global_vibrato_lfo, G_LFO_RATE_HZ, G_LFO_DEPTH_SEMITONES,
           (float)SAMPLING_FREQUENCY);
  printf("Global Vibrato LFO initialized: Rate=%.2f Hz, Depth=%.2f semitones\n",
         global_vibrato_lfo.rate_hz, global_vibrato_lfo.depth_semitones);

  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    poly_voices[i].fundamental_frequency = 0.0f;
    poly_voices[i].voice_state = ADSR_STATE_IDLE;
    poly_voices[i].midi_note_number = -1;
    poly_voices[i].last_velocity = 1.0f;
    poly_voices[i].last_triggered_order = 0; // Initialize trigger order
    for (int j = 0; j < MAX_MAPPED_OSCILLATORS; ++j) {
      poly_voices[i].oscillators[j].phase = 0.0f;
    }
    adsr_init_envelope(&poly_voices[i].volume_adsr, G_VOLUME_ADSR_ATTACK_S,
                       G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
                       G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
    adsr_init_envelope(&poly_voices[i].filter_adsr, G_FILTER_ADSR_ATTACK_S,
                       G_FILTER_ADSR_DECAY_S, G_FILTER_ADSR_SUSTAIN_LEVEL,
                       G_FILTER_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
  }
  printf("%d polyphonic voices initialized.\n", NUM_POLY_VOICES);
  printf("synth_fftMode initialized with moving average window of %d frames.\n",
         MOVING_AVERAGE_WINDOW_SIZE);
}

// --- Audio Processing ---

// Counter for rate-limiting FFT debug prints
static int g_fft_print_counter = 0;
// Print roughly once per second (assuming SAMPLING_FREQUENCY=44100,
// AUDIO_BUFFER_SIZE=512 -> ~86 calls/sec)
#define FFT_PRINT_INTERVAL 86

void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size) {
  if (audio_buffer == NULL) {
    fprintf(stderr, "synth_fftMode_process: audio_buffer is NULL\n");
    return;
  }
  memset(audio_buffer, 0, buffer_size * sizeof(float));

  // Calculate smoothed magnitudes (original logic)
  global_smoothed_magnitudes[0] =
      fft_context.fft_output[0].r / NORM_FACTOR_BIN0;
  if (global_smoothed_magnitudes[0] < 0.0f) {
    global_smoothed_magnitudes[0] = 0.0f;
  }

  for (int i = 1; i < MAX_MAPPED_OSCILLATORS; ++i) {
    float real = fft_context.fft_output[i].r;
    float imag = fft_context.fft_output[i].i;
    float magnitude = sqrtf(real * real + imag * imag);
    float target_mag = fminf(1.0f, magnitude / NORM_FACTOR_HARMONICS);
    if (target_mag < 0.0f) {
      target_mag = 0.0f;
    }
    global_smoothed_magnitudes[i] =
        AMPLITUDE_SMOOTHING_ALPHA * target_mag +
        (1.0f - AMPLITUDE_SMOOTHING_ALPHA) * global_smoothed_magnitudes[i];
  }

  // --- BEGIN FFT DEBUG TRACE ---
  if ((++g_fft_print_counter % FFT_PRINT_INTERVAL) == 0) {
    int print_limit =
        (MAX_MAPPED_OSCILLATORS < 10) ? MAX_MAPPED_OSCILLATORS : 10;
    printf("\n--- FFT DEBUG TRACE (Cycle: %d) ---\n",
           g_fft_print_counter / FFT_PRINT_INTERVAL);

    printf(
        "Raw FFT Output (fft_context.fft_output) - First %d bins (approx):\n",
        print_limit);
    // DC component (Bin 0)
    printf("Bin 0 (DC): Real = %.4e\n", fft_context.fft_output[0].r);
    // Note: For kiss_fftr, fft_context.fft_output[0].i may store Nyquist
    // component if N is even. We are typically interested in the magnitudes
    // derived for the oscillators.

    for (int k = 1; k < print_limit && k < (CIS_MAX_PIXELS_NB / 2 + 1); ++k) {
      // Iterate up to print_limit or half the FFT size (number of complex bins)
      float r_raw = fft_context.fft_output[k].r;
      float im_raw = fft_context.fft_output[k].i;
      float mag_raw = sqrtf(r_raw * r_raw + im_raw * im_raw);
      float phase_raw =
          atan2f(im_raw, r_raw) * 180.0f / M_PI; // Phase in degrees
      printf("Bin %2d: Real=%.2e, Imag=%.2e, Mag=%.2e, Phase=%.1f deg\n", k,
             r_raw, im_raw, mag_raw, phase_raw);
    }

    printf("Processed Magnitudes (global_smoothed_magnitudes) - First %d "
           "oscillators:\n",
           print_limit);
    for (int k = 0; k < print_limit && k < MAX_MAPPED_OSCILLATORS; ++k) {
      printf("Osc %2d (maps to FFT bin %d): SmoothedMag=%.4f\n", k, k,
             global_smoothed_magnitudes[k]);
    }
    // Add debug print for active harmonics count for the first active voice
    for (int v_idx_debug = 0; v_idx_debug < NUM_POLY_VOICES; ++v_idx_debug) {
      if (poly_voices[v_idx_debug].voice_state != ADSR_STATE_IDLE) {
        int active_harmonics_count = 0;
        float base_freq_debug = poly_voices[v_idx_debug].fundamental_frequency;
        // Recalculate how many harmonics would be used based on Nyquist for
        // this voice
        for (int osc_idx_debug = 0; osc_idx_debug < MAX_MAPPED_OSCILLATORS;
             ++osc_idx_debug) {
          float harmonic_multiple_debug;
          if (osc_idx_debug == 0)
            harmonic_multiple_debug = 1.0f;
          else
            harmonic_multiple_debug = (float)(osc_idx_debug + 1);
          float osc_freq_debug = base_freq_debug * harmonic_multiple_debug;
          if (osc_freq_debug >= (float)SAMPLING_FREQUENCY / 2.0f) {
            break;
          }
          active_harmonics_count++;
        }
        printf("Voice %d (Note %d, Freq %.0fHz): Active Harmonics (due to "
               "Nyquist) = %d / %d\n",
               v_idx_debug, poly_voices[v_idx_debug].midi_note_number,
               base_freq_debug, active_harmonics_count, MAX_MAPPED_OSCILLATORS);
        break; // Print for the first active voice only to avoid flooding
      }
    }
    printf("---------------------------------------\n");
    fflush(stdout); // Ensure it prints immediately
  }
  // --- END FFT DEBUG TRACE ---

  for (unsigned int sample_idx = 0; sample_idx < buffer_size; ++sample_idx) {
    float master_sample_sum = 0.0f;
    float lfo_modulation_value =
        lfo_process(&global_vibrato_lfo); // Process LFO per sample

    for (int v_idx = 0; v_idx < NUM_POLY_VOICES; ++v_idx) {
      SynthVoice *current_voice = &poly_voices[v_idx];
      float volume_adsr_val = adsr_get_output(&current_voice->volume_adsr);
      float filter_adsr_val = adsr_get_output(&current_voice->filter_adsr);

      if (current_voice->volume_adsr.state == ADSR_STATE_IDLE &&
          current_voice->voice_state != ADSR_STATE_IDLE) {
        current_voice->voice_state = ADSR_STATE_IDLE;
        current_voice->midi_note_number = -1;
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
                             (float)SAMPLING_FREQUENCY / 2.0f - 1.0f));

      // Apply LFO to fundamental frequency
      float base_freq = current_voice->fundamental_frequency;
      float freq_mod_factor = powf(
          2.0f,
          (lfo_modulation_value * global_vibrato_lfo.depth_semitones) / 12.0f);
      float actual_fundamental_freq = base_freq * freq_mod_factor;

      float voice_sample_sum = 0.0f;
      // Loop now starts from osc_idx = 0 to include the fundamental frequency
      // modulated by global_smoothed_magnitudes[0] (DC component)
      // It iterates up to MAX_MAPPED_OSCILLATORS and includes Nyquist check.
      for (int osc_idx = 0; osc_idx < MAX_MAPPED_OSCILLATORS; ++osc_idx) {
        float harmonic_multiple;
        if (osc_idx == 0) {
          harmonic_multiple = 1.0f; // Fundamental frequency for osc_idx 0
        } else {
          harmonic_multiple = (float)(osc_idx + 1); // Harmonics for osc_idx > 0
        }
        float osc_freq = actual_fundamental_freq *
                         harmonic_multiple; // Use LFO modulated fundamental

        // Nyquist check: if harmonic frequency is too high, stop adding
        // harmonics for this voice
        if (osc_freq >= (float)SAMPLING_FREQUENCY / 2.0f) {
          break;
        }

        float phase_increment = TWO_PI * osc_freq / (float)SAMPLING_FREQUENCY;

        float smoothed_amplitude = global_smoothed_magnitudes[osc_idx];
        float amplitude_after_gamma = powf(smoothed_amplitude, AMPLITUDE_GAMMA);
        if (smoothed_amplitude < 0.0f &&
            (AMPLITUDE_GAMMA != floorf(AMPLITUDE_GAMMA))) {
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
        float osc_sample =
            final_amplitude * sinf(current_voice->oscillators[osc_idx].phase);
        voice_sample_sum += osc_sample;

        current_voice->oscillators[osc_idx].phase += phase_increment;
        if (current_voice->oscillators[osc_idx].phase >= TWO_PI) {
          current_voice->oscillators[osc_idx].phase -= TWO_PI;
        }
      }

      voice_sample_sum *= volume_adsr_val;
      voice_sample_sum *= current_voice->last_velocity;
      master_sample_sum += voice_sample_sum;
    }

    master_sample_sum *= MASTER_VOLUME;

    if (master_sample_sum > 1.0f)
      master_sample_sum = 1.0f;
    else if (master_sample_sum < -1.0f)
      master_sample_sum = -1.0f;

    audio_buffer[sample_idx] = master_sample_sum;
  }
}

// --- Image & FFT Processing ---
static void process_image_data_for_fft(DoubleBuffer *image_db) {
  if (image_db == NULL) {
    fprintf(stderr, "process_image_data_for_fft: image_db is NULL\n");
    return;
  }
  pthread_mutex_lock(&image_db->mutex);
  while (!image_db->dataReady && keepRunning) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    int wait_ret =
        pthread_cond_timedwait(&image_db->cond, &image_db->mutex, &ts);
    if (wait_ret == ETIMEDOUT && !keepRunning) {
      pthread_mutex_unlock(&image_db->mutex);
      return;
    }
  }
  if (!keepRunning) {
    pthread_mutex_unlock(&image_db->mutex);
    return;
  }

  float current_grayscale_line[CIS_MAX_PIXELS_NB];
  for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
    current_grayscale_line[i] = 0.299f * image_db->activeBuffer_R[i] +
                                0.587f * image_db->activeBuffer_G[i] +
                                0.114f * image_db->activeBuffer_B[i];
  }
  pthread_mutex_unlock(&image_db->mutex);

  pthread_mutex_lock(&image_history_mutex);
  memcpy(image_line_history[history_write_index].line_data,
         current_grayscale_line, CIS_MAX_PIXELS_NB * sizeof(float));
  history_write_index = (history_write_index + 1) % MOVING_AVERAGE_WINDOW_SIZE;
  if (history_fill_count < MOVING_AVERAGE_WINDOW_SIZE) {
    history_fill_count++;
  }

  if (history_fill_count > 0) {
    memset(fft_context.fft_input, 0,
           CIS_MAX_PIXELS_NB * sizeof(kiss_fft_scalar));
    for (int j = 0; j < CIS_MAX_PIXELS_NB; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < history_fill_count; ++k) {
        int idx = (history_write_index - 1 - k + MOVING_AVERAGE_WINDOW_SIZE) %
                  MOVING_AVERAGE_WINDOW_SIZE;
        sum += image_line_history[idx].line_data[j];
      }
      fft_context.fft_input[j] = sum / history_fill_count;
    }
    kiss_fftr(fft_context.fft_cfg, fft_context.fft_input,
              fft_context.fft_output);
  }
  pthread_mutex_unlock(&image_history_mutex);
}

static void generate_test_data_for_fft(void) {
  static int call_count = 0;
  printf("Génération de données de test pour la FFT (%d)...\n", call_count++);
  pthread_mutex_lock(&image_history_mutex);
  float test_line[CIS_MAX_PIXELS_NB];
  for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
    float phase = 10.0f * 2.0f * M_PI * (float)i / (float)CIS_MAX_PIXELS_NB;
    test_line[i] = sinf(phase) * 100.0f;
    test_line[i] += sinf(5.0f * phase) * 50.0f;
    test_line[i] += (rand() % 100) / 100.0f * 20.0f;
  }
  memcpy(image_line_history[history_write_index].line_data, test_line,
         CIS_MAX_PIXELS_NB * sizeof(float));
  history_write_index = (history_write_index + 1) % MOVING_AVERAGE_WINDOW_SIZE;
  if (history_fill_count < MOVING_AVERAGE_WINDOW_SIZE) {
    history_fill_count++;
  }
  if (history_fill_count > 0) {
    memset(fft_context.fft_input, 0,
           CIS_MAX_PIXELS_NB * sizeof(kiss_fft_scalar));
    for (int j = 0; j < CIS_MAX_PIXELS_NB; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < history_fill_count; ++k) {
        int idx = (history_write_index - 1 - k + MOVING_AVERAGE_WINDOW_SIZE) %
                  MOVING_AVERAGE_WINDOW_SIZE;
        sum += image_line_history[idx].line_data[j];
      }
      fft_context.fft_input[j] = sum / history_fill_count;
    }
    kiss_fftr(fft_context.fft_cfg, fft_context.fft_input,
              fft_context.fft_output);
  }
  pthread_mutex_unlock(&image_history_mutex);
}

// --- Main Thread Function ---
void *synth_fftMode_thread_func(void *arg) {
  DoubleBuffer *image_db = NULL;
  if (arg != NULL) {
    Context *ctx = (Context *)arg;
    image_db = ctx->doubleBuffer;
    printf(
        "synth_fftMode_thread_func: DoubleBuffer obtenu depuis le contexte.\n");
  } else {
    printf("synth_fftMode_thread_func: Aucun contexte fourni, pas de "
           "DoubleBuffer disponible.\n");
  }
  printf("synth_fftMode_thread_func started.\n");
  fflush(stdout);
  srand(time(NULL));

  while (keepRunning) {
    if (image_db != NULL) {
      process_image_data_for_fft(image_db);
    } else {
      printf("synth_fftMode_thread_func: Aucun DoubleBuffer. Utilisation des "
             "données de test.\n");
      generate_test_data_for_fft();
    }
    fflush(stdout);

    int local_producer_idx;
    pthread_mutex_lock(&fft_buffer_index_mutex);
    local_producer_idx = fft_current_buffer_index;
    pthread_mutex_unlock(&fft_buffer_index_mutex);

    pthread_mutex_lock(&fft_audio_buffers[local_producer_idx].mutex);
    while (fft_audio_buffers[local_producer_idx].ready == 1 && keepRunning) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 1;
      int wait_ret = pthread_cond_timedwait(
          &fft_audio_buffers[local_producer_idx].cond,
          &fft_audio_buffers[local_producer_idx].mutex, &ts);
      if (wait_ret == ETIMEDOUT && !keepRunning) {
        pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);
        goto cleanup_thread;
      }
    }
    synth_fftMode_process(fft_audio_buffers[local_producer_idx].data,
                          AUDIO_BUFFER_SIZE);
    fft_audio_buffers[local_producer_idx].ready = 1;
    pthread_cond_signal(&fft_audio_buffers[local_producer_idx].cond);
    pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);

    pthread_mutex_lock(&fft_buffer_index_mutex);
    fft_current_buffer_index = 1 - local_producer_idx;
    pthread_mutex_unlock(&fft_buffer_index_mutex);
  }

cleanup_thread:
  printf("synth_fftMode_thread_func stopping.\n");
  if (fft_context.fft_cfg != NULL) {
    kiss_fftr_free(fft_context.fft_cfg);
    fft_context.fft_cfg = NULL;
  }
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

void synth_fft_note_on(int noteNumber, int velocity) {
  if (velocity <= 0) {
    synth_fft_note_off(noteNumber);
    return;
  }

  g_current_trigger_order++; // Increment global trigger order

  int voice_idx = -1;

  // Priority 1: Find an IDLE voice
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    if (poly_voices[i].voice_state == ADSR_STATE_IDLE) {
      voice_idx = i;
      break;
    }
  }

  // Priority 2: Find the oldest voice not in RELEASE or RECENT ATTACK
  // "Recent attack" could be defined by comparing its trigger order to
  // g_current_trigger_order For simplicity here, we'll consider any ATTACK,
  // DECAY, SUSTAIN voice. We'll pick the one with the smallest
  // last_triggered_order.
  if (voice_idx == -1) {
    unsigned long long oldest_order =
        g_current_trigger_order +
        1; // Initialize with a value guaranteed to be newer
    int candidate_idx = -1;
    for (int i = 0; i < NUM_POLY_VOICES; ++i) {
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
      // printf("SYNTH_FFT: No idle voice. Stealing oldest active (non-release)
      // "
      //        "voice %d for note %d (Order: %llu)\n",
      //        voice_idx, noteNumber, oldest_order);
    }
  }

  // Priority 3: If still no voice, steal the voice in RELEASE with the lowest
  // envelope output
  if (voice_idx == -1) {
    float lowest_env_output = 2.0f; // Greater than max envelope output (1.0)
    int candidate_idx = -1;
    for (int i = 0; i < NUM_POLY_VOICES; ++i) {
      if (poly_voices[i].voice_state == ADSR_STATE_RELEASE) {
        if (poly_voices[i].volume_adsr.current_output < lowest_env_output) {
          lowest_env_output = poly_voices[i].volume_adsr.current_output;
          candidate_idx = i;
        }
      }
    }
    if (candidate_idx != -1) {
      voice_idx = candidate_idx;
      // printf("SYNTH_FFT: No idle or active non-release voice. Stealing "
      //        "quietest release voice %d for note %d (Env: %.2f)\n",
      //        voice_idx, noteNumber, lowest_env_output);
    }
  }

  // Fallback: If absolutely no voice found (e.g., all voices are in very recent
  // ATTACK and NUM_POLY_VOICES is small) or if all voices are in RELEASE but
  // none were selected by prio 3 (should not happen if there's at least one
  // release voice) For now, we'll just steal voice 0 as a last resort if
  // voice_idx is still -1. A more robust system might refuse the note or have a
  // more complex fallback.
  if (voice_idx == -1) {
    voice_idx = 0; // Default to stealing voice 0 if no other candidate found
    // printf("SYNTH_FFT: Critical fallback. Stealing voice 0 for note %d\n",
    //        noteNumber);
  }

  SynthVoice *voice = &poly_voices[voice_idx];

  // Initialize the chosen voice's envelopes with current global ADSR settings
  // This ensures it starts fresh, respecting the latest global parameters.
  adsr_init_envelope(&voice->volume_adsr, G_VOLUME_ADSR_ATTACK_S,
                     G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
                     G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
  adsr_init_envelope(&voice->filter_adsr, G_FILTER_ADSR_ATTACK_S,
                     G_FILTER_ADSR_DECAY_S, G_FILTER_ADSR_SUSTAIN_LEVEL,
                     G_FILTER_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);

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

  // printf("SYNTH_FFT: Voice %d Note On: %d, Vel: %d (Norm: %.2f), Freq: %.2f "
  //        "Hz, Order: %llu -> ADSR Attack\n",
  //        voice_idx, noteNumber, velocity, voice->last_velocity,
  //        voice->fundamental_frequency, voice->last_triggered_order);
}

void synth_fft_note_off(int noteNumber) {
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    if (poly_voices[i].midi_note_number == noteNumber &&
        poly_voices[i].voice_state != ADSR_STATE_IDLE &&
        poly_voices[i].voice_state != ADSR_STATE_RELEASE) {
      adsr_trigger_release(&poly_voices[i].volume_adsr);
      adsr_trigger_release(&poly_voices[i].filter_adsr);
      // voice_state will be set to IDLE by adsr_get_output when release
      // finishes midi_note_number will be set to -1 when voice_state becomes
      // IDLE in synth_fftMode_process
      // printf("SYNTH_FFT: Voice %d Note Off: %d -> ADSR Release\n", i,
      //        noteNumber);
    }
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
void synth_fft_set_volume_adsr_attack(float attack_s) {
  if (attack_s < 0.0f)
    attack_s = 0.0f;
  G_VOLUME_ADSR_ATTACK_S = attack_s;
  // printf("SYNTH_FFT: Global Volume ADSR Attack set to: %.3f s\n", attack_s);
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, G_VOLUME_ADSR_ATTACK_S,
        G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
        G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
    // Update filter ADSR similarly if desired
    // adsr_update_settings_and_recalculate_rates(&poly_voices[i].filter_adsr,
    // G_FILTER_ADSR_ATTACK_S, ...);
  }
}

void synth_fft_set_volume_adsr_decay(float decay_s) {
  if (decay_s < 0.0f)
    decay_s = 0.0f;
  G_VOLUME_ADSR_DECAY_S = decay_s;
  // printf("SYNTH_FFT: Global Volume ADSR Decay set to: %.3f s\n", decay_s);
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, G_VOLUME_ADSR_ATTACK_S,
        G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
        G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
  }
}

void synth_fft_set_volume_adsr_sustain(float sustain_level) {
  if (sustain_level < 0.0f)
    sustain_level = 0.0f;
  if (sustain_level > 1.0f)
    sustain_level = 1.0f;
  G_VOLUME_ADSR_SUSTAIN_LEVEL = sustain_level;
  // printf("SYNTH_FFT: Global Volume ADSR Sustain set to: %.2f\n",
  // sustain_level);
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, G_VOLUME_ADSR_ATTACK_S,
        G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
        G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
  }
}

void synth_fft_set_volume_adsr_release(float release_s) {
  if (release_s < 0.0f)
    release_s = 0.0f;
  G_VOLUME_ADSR_RELEASE_S = release_s;
  // printf("SYNTH_FFT: Global Volume ADSR Release set to: %.3f s\n",
  // release_s);
  for (int i = 0; i < NUM_POLY_VOICES; ++i) {
    adsr_update_settings_and_recalculate_rates(
        &poly_voices[i].volume_adsr, G_VOLUME_ADSR_ATTACK_S,
        G_VOLUME_ADSR_DECAY_S, G_VOLUME_ADSR_SUSTAIN_LEVEL,
        G_VOLUME_ADSR_RELEASE_S, (float)SAMPLING_FREQUENCY);
  }
}

// --- LFO Parameter Setters ---
void synth_fft_set_vibrato_rate(float rate_hz) {
  if (rate_hz < 0.0f)
    rate_hz = 0.0f;
  // Potentially add a max rate limit, e.g., 20Hz or 30Hz
  global_vibrato_lfo.rate_hz = rate_hz;
  global_vibrato_lfo.phase_increment =
      TWO_PI * rate_hz / (float)SAMPLING_FREQUENCY;
  // printf("SYNTH_FFT: Global Vibrato LFO Rate set to: %.2f Hz\n", rate_hz);
}

void synth_fft_set_vibrato_depth(float depth_semitones) {
  // Depth can be positive or negative, typically small values like -2 to 2
  // semitones
  global_vibrato_lfo.depth_semitones = depth_semitones;
  // printf("SYNTH_FFT: Global Vibrato LFO Depth set to: %.2f semitones\n",
  //        depth_semitones);
}
