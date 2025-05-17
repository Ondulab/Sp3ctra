/*
 * synth_fft.c
 *
 *  Created on: 16 May 2025
 *      Author: Cline
 */

#include "synth_fft.h"
#include "config.h"       // For SAMPLING_FREQUENCY, AUDIO_BUFFER_SIZE
#include "context.h"      // Pour accéder à la structure Context
#include "doublebuffer.h" // Pour accéder au DoubleBuffer des images
#include "error.h"        // For die() or other error handling
#include <errno.h>        // For ETIMEDOUT
#include <math.h> // For sinf, M_PI, powf, sqrtf, fmaxf, fminf, floorf, expf
#include <pthread.h>
#include <stdio.h>  // For printf (debugging)
#include <string.h> // For memset
#include <time.h>   // Pour time() (random seed)
#include <unistd.h> // Pour usleep

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#define TWO_PI (2.0 * M_PI)

// Forward declarations for static ADSR functions
static void adsr_init_envelope(AdsrEnvelope *env, float attack_s, float decay_s,
                               float sustain_level, float release_s,
                               float sample_rate);
static void adsr_trigger_attack(AdsrEnvelope *env);
static void adsr_trigger_release(AdsrEnvelope *env);
static float adsr_get_output(AdsrEnvelope *env);

// Forward declarations for static Filter functions (now only init for spectral
// params)
static void filter_init_spectral_params(SpectralFilterParams *fp,
                                        float base_cutoff_hz,
                                        float filter_env_depth);

// Normalization factors for FFT magnitudes
#define NORM_FACTOR_BIN0 881280.0f
#define NORM_FACTOR_HARMONICS 220320.0f
#define MASTER_VOLUME 0.125f
#define AMPLITUDE_SMOOTHING_ALPHA 0.005f
#define AMPLITUDE_GAMMA 2.5f

// Global variables defined in synth_fft.h
FftAudioDataBuffer fft_audio_buffers[2];
volatile int fft_current_buffer_index = 0;
pthread_mutex_t fft_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

GrayscaleLine image_line_history[MOVING_AVERAGE_WINDOW_SIZE];
int history_write_index = 0;
int history_fill_count = 0;
pthread_mutex_t image_history_mutex = PTHREAD_MUTEX_INITIALIZER;
FftContext fft_context;
MonophonicVoice g_mono_voice;

static float sine_phase = 0.0f;           // Legacy, unused by current synth
static float sine_phase_increment = 0.0f; // Legacy

extern volatile int keepRunning;

void synth_fftMode_init(void) {
  printf("Initializing synth_fftMode...\n");

  sine_phase_increment = TWO_PI * 440.0f / (float)SAMPLING_FREQUENCY; // Legacy
  sine_phase = 0.0f;                                                  // Legacy

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
  history_fill_count = 0;
  memset(image_line_history, 0, sizeof(image_line_history));

  fft_context.fft_cfg = kiss_fftr_alloc(CIS_MAX_PIXELS_NB, 0, NULL, NULL);
  if (fft_context.fft_cfg == NULL) {
    die("Failed to initialize FFT configuration");
  }
  memset(fft_context.fft_input, 0, sizeof(fft_context.fft_input));
  memset(fft_context.fft_output, 0, sizeof(fft_context.fft_output));

  g_mono_voice.fundamental_frequency = DEFAULT_FUNDAMENTAL_FREQUENCY;
  g_mono_voice.active = 0;
  g_mono_voice.last_velocity = 1.0f;
  for (int i = 0; i < NUM_OSCILLATORS; ++i) {
    g_mono_voice.oscillators[i].phase = 0.0f;
    g_mono_voice.smoothed_normalized_magnitudes[i] = 0.0f;
  }
  adsr_init_envelope(&g_mono_voice.volume_adsr, 0.01f, 0.1f, 0.8f, 0.2f,
                     (float)SAMPLING_FREQUENCY);
  printf("Monophonic voice initialized. Volume ADSR: A=%.2fs, D=%.2fs, S=%.1f, "
         "R=%.2fs\n",
         g_mono_voice.volume_adsr.attack_s, g_mono_voice.volume_adsr.decay_s,
         g_mono_voice.volume_adsr.sustain_level,
         g_mono_voice.volume_adsr.release_s);

  adsr_init_envelope(&g_mono_voice.filter_adsr, 0.02f, 0.2f, 0.1f, 0.3f,
                     (float)SAMPLING_FREQUENCY);
  printf("Monophonic voice initialized. Filter ADSR: A=%.2fs, D=%.2fs, S=%.1f, "
         "R=%.2fs\n",
         g_mono_voice.filter_adsr.attack_s, g_mono_voice.filter_adsr.decay_s,
         g_mono_voice.filter_adsr.sustain_level,
         g_mono_voice.filter_adsr.release_s);

  filter_init_spectral_params(&g_mono_voice.spectral_filter_params, 8000.0f,
                              -7800.0f);
  printf("Monophonic voice initialized. Spectral Filter Params: "
         "BaseCutoff=%.0fHz, EnvDepth=%.0fHz\n",
         g_mono_voice.spectral_filter_params.base_cutoff_hz,
         g_mono_voice.spectral_filter_params.filter_env_depth);

  printf("synth_fftMode initialized with moving average window of %d frames.\n",
         MOVING_AVERAGE_WINDOW_SIZE);
}

void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size) {
  if (audio_buffer == NULL) {
    fprintf(stderr, "synth_fftMode_process: audio_buffer is NULL\n");
    return;
  }

  float target_normalized_magnitudes[NUM_OSCILLATORS];

  target_normalized_magnitudes[0] =
      fft_context.fft_output[0].r / NORM_FACTOR_BIN0;
  if (target_normalized_magnitudes[0] < 0.0f) {
    target_normalized_magnitudes[0] = 0.0f;
  }

  for (int i = 1; i < NUM_OSCILLATORS; ++i) {
    float real = fft_context.fft_output[i].r;
    float imag = fft_context.fft_output[i].i;
    float magnitude = sqrtf(real * real + imag * imag);
    target_normalized_magnitudes[i] =
        fminf(1.0f, magnitude / NORM_FACTOR_HARMONICS);
    if (target_normalized_magnitudes[i] < 0.0f) {
      target_normalized_magnitudes[i] = 0.0f;
    }
  }

  for (int i = 0; i < NUM_OSCILLATORS; ++i) {
    g_mono_voice.smoothed_normalized_magnitudes[i] =
        AMPLITUDE_SMOOTHING_ALPHA * target_normalized_magnitudes[i] +
        (1.0f - AMPLITUDE_SMOOTHING_ALPHA) *
            g_mono_voice.smoothed_normalized_magnitudes[i];
  }

  for (unsigned int sample_idx = 0; sample_idx < buffer_size; ++sample_idx) {
    float volume_adsr_val = adsr_get_output(&g_mono_voice.volume_adsr);
    float filter_adsr_val = adsr_get_output(&g_mono_voice.filter_adsr);

    if (volume_adsr_val < 0.00001f &&
        g_mono_voice.volume_adsr.state == ADSR_STATE_IDLE) {
      for (unsigned int j = sample_idx; j < buffer_size; ++j) {
        audio_buffer[j] = 0.0f;
      }
      break;
    }

    float modulated_cutoff_hz =
        g_mono_voice.spectral_filter_params.base_cutoff_hz +
        filter_adsr_val * g_mono_voice.spectral_filter_params.filter_env_depth;
    modulated_cutoff_hz =
        fmaxf(20.0f, fminf(modulated_cutoff_hz,
                           (float)SAMPLING_FREQUENCY / 2.0f - 1.0f));

    float current_sample_sum = 0.0f;
    for (int osc_idx = 0; osc_idx < NUM_OSCILLATORS; ++osc_idx) {
      float harmonic_multiple = (float)(osc_idx + 1);
      float osc_freq = g_mono_voice.fundamental_frequency * harmonic_multiple;
      float phase_increment = TWO_PI * osc_freq / (float)SAMPLING_FREQUENCY;

      float smoothed_amplitude =
          g_mono_voice.smoothed_normalized_magnitudes[osc_idx];
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
          final_amplitude * sinf(g_mono_voice.oscillators[osc_idx].phase);
      current_sample_sum += osc_sample;

      g_mono_voice.oscillators[osc_idx].phase += phase_increment;
      if (g_mono_voice.oscillators[osc_idx].phase >= TWO_PI) {
        g_mono_voice.oscillators[osc_idx].phase -= TWO_PI;
      }
    }

    current_sample_sum *= volume_adsr_val;
    current_sample_sum *= g_mono_voice.last_velocity;
    // Time-domain filter processing is removed. Spectral filtering is
    // per-oscillator.
    current_sample_sum *= MASTER_VOLUME;

    if (current_sample_sum > 1.0f)
      current_sample_sum = 1.0f;
    else if (current_sample_sum < -1.0f)
      current_sample_sum = -1.0f;

    audio_buffer[sample_idx] = current_sample_sum;
  }
}

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
                  MOVING_AVERAGE_WINDOW_SIZE; // Corrected history indexing
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
    printf("Historique rempli à %d/%d\n", history_fill_count,
           MOVING_AVERAGE_WINDOW_SIZE);
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

static void adsr_trigger_attack(AdsrEnvelope *env) {
  env->state = ADSR_STATE_ATTACK;
  env->current_samples = 0;
  if (env->current_output > 0.0f && env->current_output < 1.0f &&
      env->attack_time_samples > 0.0f) {
    env->attack_increment =
        (1.0f - env->current_output) / env->attack_time_samples;
  } else if (env->attack_time_samples > 0.0f) {
    env->current_output = 0.0f;
    env->attack_increment = 1.0f / env->attack_time_samples;
  } else {
    env->current_output = 1.0f;
    env->attack_increment = 0.0f;
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
  if (velocity > 0) {
    g_mono_voice.fundamental_frequency = midi_note_to_frequency(noteNumber);
    g_mono_voice.active = 1;
    for (int i = 0; i < NUM_OSCILLATORS; ++i) {
      g_mono_voice.oscillators[i].phase = 0.0f;
    }
    g_mono_voice.last_velocity = (float)velocity / 127.0f;
    adsr_trigger_attack(&g_mono_voice.volume_adsr);
    adsr_trigger_attack(&g_mono_voice.filter_adsr);
    printf("SYNTH_FFT: Note On: %d, Vel: %d (Norm: %.2f), Freq: %f Hz -> ADSR "
           "Attack (Vol & Filter)\n",
           noteNumber, velocity, g_mono_voice.last_velocity,
           g_mono_voice.fundamental_frequency);
  } else {
    synth_fft_note_off(noteNumber);
  }
}

void synth_fft_note_off(int noteNumber) {
  float released_note_freq = midi_note_to_frequency(noteNumber);
  if (g_mono_voice.active &&
      fabsf(g_mono_voice.fundamental_frequency - released_note_freq) < 0.01f) {
    adsr_trigger_release(&g_mono_voice.volume_adsr);
    adsr_trigger_release(&g_mono_voice.filter_adsr);
    g_mono_voice.active = 0;
    printf(
        "SYNTH_FFT: Note Off: %d, Freq: %f Hz -> ADSR Release (Vol & Filter)\n",
        noteNumber, released_note_freq);
  }
}

// --- Filter Implementation (Simplified for Spectral Params) ---
static void filter_init_spectral_params(SpectralFilterParams *fp,
                                        float base_cutoff_hz,
                                        float filter_env_depth) {
  fp->base_cutoff_hz = base_cutoff_hz;
  fp->filter_env_depth = filter_env_depth;
  // No prev_output or alpha to init for this spectral approach
}

// --- ADSR Parameter Setters ---
void synth_fft_set_volume_adsr_attack(float attack_s) {
  if (attack_s < 0.0f)
    attack_s = 0.0f;
  g_mono_voice.volume_adsr.attack_s = attack_s;
  adsr_init_envelope(
      &g_mono_voice.volume_adsr, g_mono_voice.volume_adsr.attack_s,
      g_mono_voice.volume_adsr.decay_s, g_mono_voice.volume_adsr.sustain_level,
      g_mono_voice.volume_adsr.release_s, (float)SAMPLING_FREQUENCY);
  printf("SYNTH_FFT: Volume ADSR Attack set to: %.3f s\n", attack_s);
}

void synth_fft_set_volume_adsr_decay(float decay_s) {
  if (decay_s < 0.0f)
    decay_s = 0.0f;
  g_mono_voice.volume_adsr.decay_s = decay_s;
  adsr_init_envelope(
      &g_mono_voice.volume_adsr, g_mono_voice.volume_adsr.attack_s,
      g_mono_voice.volume_adsr.decay_s, g_mono_voice.volume_adsr.sustain_level,
      g_mono_voice.volume_adsr.release_s, (float)SAMPLING_FREQUENCY);
  printf("SYNTH_FFT: Volume ADSR Decay set to: %.3f s\n", decay_s);
}

void synth_fft_set_volume_adsr_sustain(float sustain_level) {
  if (sustain_level < 0.0f)
    sustain_level = 0.0f;
  if (sustain_level > 1.0f)
    sustain_level = 1.0f;
  g_mono_voice.volume_adsr.sustain_level = sustain_level;
  adsr_init_envelope(
      &g_mono_voice.volume_adsr, g_mono_voice.volume_adsr.attack_s,
      g_mono_voice.volume_adsr.decay_s, g_mono_voice.volume_adsr.sustain_level,
      g_mono_voice.volume_adsr.release_s, (float)SAMPLING_FREQUENCY);
  printf("SYNTH_FFT: Volume ADSR Sustain set to: %.2f\n", sustain_level);
}

void synth_fft_set_volume_adsr_release(float release_s) {
  if (release_s < 0.0f)
    release_s = 0.0f;
  g_mono_voice.volume_adsr.release_s = release_s;
  adsr_init_envelope(
      &g_mono_voice.volume_adsr, g_mono_voice.volume_adsr.attack_s,
      g_mono_voice.volume_adsr.decay_s, g_mono_voice.volume_adsr.sustain_level,
      g_mono_voice.volume_adsr.release_s, (float)SAMPLING_FREQUENCY);
  printf("SYNTH_FFT: Volume ADSR Release set to: %.3f s\n", release_s);
}
