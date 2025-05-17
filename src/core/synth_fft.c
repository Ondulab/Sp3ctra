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
#include <math.h>         // For sinf, M_PI
#include <pthread.h>
#include <stdio.h>  // For printf (debugging)
#include <string.h> // For memset
#include <time.h>   // Pour time() (random seed)
#include <unistd.h> // Pour usleep

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#define TWO_PI (2.0 * M_PI)

// Normalization factors for FFT magnitudes
#define NORM_FACTOR_BIN0 881280.0f
#define NORM_FACTOR_HARMONICS 220320.0f // Reverted from 50000.0f
#define MASTER_VOLUME                                                          \
  0.125f // Master volume control (0.0 to 1.0) - Reduced from 0.25f
#define AMPLITUDE_SMOOTHING_ALPHA 0.005f // Increased smoothing (was 0.05f)
#define AMPLITUDE_GAMMA 2.5f             // For non-linear amplitude mapping

// Global variables defined in synth_fft.h
FftAudioDataBuffer fft_audio_buffers[2];
volatile int fft_current_buffer_index = 0;
pthread_mutex_t fft_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

// Variables pour la moyenne glissante et la FFT
GrayscaleLine
    image_line_history[MOVING_AVERAGE_WINDOW_SIZE]; // Historique des lignes
int history_write_index =
    0; // Index pour l'écriture circulaire dans l'historique
int history_fill_count = 0; // Nombre d'éléments valides dans l'historique
pthread_mutex_t image_history_mutex =
    PTHREAD_MUTEX_INITIALIZER; // Mutex pour l'historique
FftContext fft_context;        // Contexte pour la FFT
MonophonicVoice g_mono_voice;  // Global monophonic voice instance

// Internal state for sine wave generation (legacy, for reference or potential
// reuse)
static float sine_phase = 0.0f;
static float sine_phase_increment = 0.0f;

// Global flag to keep threads running (assuming it's defined elsewhere, e.g.
// main.c or context.h) If not, this thread will run indefinitely until the
// program exits. Consider passing a context or a running flag as an argument to
// synth_fftMode_thread_func
extern volatile int keepRunning; // Declaration, assuming it's defined globally

void synth_fftMode_init(void) {
  printf("Initializing synth_fftMode...\n");

  // Calculate phase increment for 440 Hz sine wave
  sine_phase_increment = TWO_PI * 440.0f / (float)SAMPLING_FREQUENCY;
  sine_phase = 0.0f;

  // Initialize mutex and condition variables for each buffer
  for (int i = 0; i < 2; ++i) {
    if (pthread_mutex_init(&fft_audio_buffers[i].mutex, NULL) != 0) {
      die("Failed to initialize FFT audio buffer mutex");
    }
    if (pthread_cond_init(&fft_audio_buffers[i].cond, NULL) != 0) {
      die("Failed to initialize FFT audio buffer condition variable");
    }
    fft_audio_buffers[i].ready = 0; // Mark as not ready initially
    memset(fft_audio_buffers[i].data, 0, AUDIO_BUFFER_SIZE * sizeof(float));
  }

  if (pthread_mutex_init(&fft_buffer_index_mutex, NULL) != 0) {
    die("Failed to initialize FFT buffer index mutex");
  }
  fft_current_buffer_index = 0;

  // Initialisation du mutex de l'historique
  if (pthread_mutex_init(&image_history_mutex, NULL) != 0) {
    die("Failed to initialize image history mutex");
  }

  // Initialisation des variables de l'historique
  history_write_index = 0;
  history_fill_count = 0;
  memset(image_line_history, 0, sizeof(image_line_history));

  // Initialisation de la FFT
  // Configuration pour une FFT réelle (forward, non-inverse)
  fft_context.fft_cfg = kiss_fftr_alloc(CIS_MAX_PIXELS_NB, 0, NULL, NULL);
  if (fft_context.fft_cfg == NULL) {
    die("Failed to initialize FFT configuration");
  }

  // Initialisation des buffers d'entrée/sortie de la FFT
  memset(fft_context.fft_input, 0, sizeof(fft_context.fft_input));
  memset(fft_context.fft_output, 0, sizeof(fft_context.fft_output));

  // Initialize the monophonic voice
  g_mono_voice.fundamental_frequency = DEFAULT_FUNDAMENTAL_FREQUENCY;
  g_mono_voice.active = 1; // Active by default for Step 1
  for (int i = 0; i < NUM_OSCILLATORS; ++i) {
    g_mono_voice.oscillators[i].phase = 0.0f;
    g_mono_voice.smoothed_normalized_magnitudes[i] =
        0.0f; // Initialize smoothed magnitudes
  }
  printf("Monophonic voice initialized with fundamental frequency: %f Hz\n",
         g_mono_voice.fundamental_frequency);

  printf("synth_fftMode initialized with moving average window of %d frames.\n",
         MOVING_AVERAGE_WINDOW_SIZE);
}

void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size) {
  if (audio_buffer == NULL) {
    fprintf(stderr, "synth_fftMode_process: audio_buffer is NULL\n");
    return;
  }

  if (!g_mono_voice.active) {
    memset(audio_buffer, 0, buffer_size * sizeof(float));
    return;
  }

  // Array to store target normalized magnitudes for the current audio block
  float target_normalized_magnitudes[NUM_OSCILLATORS];

  // 1. Calculate target normalized FFT magnitudes from fft_context.fft_output
  // Ensure fft_context.fft_output is not NULL (it's a global struct, so base
  // address is fixed, but cfg inside might be an issue if init failed) However,
  // fft_output itself is an array within the struct, so its address is part of
  // fft_context. We assume fft_context and its members are valid after
  // synth_fftMode_init.

  // Bin 0 (DC component) for the fundamental oscillator
  // Magnitude for Bin 0 is fft_context.fft_output[0].r (since input is real and
  // positive, imag part is 0)
  target_normalized_magnitudes[0] =
      fft_context.fft_output[0].r / NORM_FACTOR_BIN0;
  if (target_normalized_magnitudes[0] < 0.0f) { // Safeguard, should be positive
    target_normalized_magnitudes[0] = 0.0f;
  }
  // No clipping for fundamental's amplitude as per spec (can exceed 1.0 if
  // NORM_FACTOR_BIN0 is too small for actual data)

  // Bins 1 to 29 for harmonic oscillators
  for (int i = 1; i < NUM_OSCILLATORS; ++i) {
    // fft_output has (CIS_MAX_PIXELS_NB / 2 + 1) complex entries.
    // NUM_OSCILLATORS is 30. So we need fft_output[0] through fft_output[29].
    // This is safe as CIS_MAX_PIXELS_NB/2+1 = 3456/2+1 = 1729, which is > 29.
    float real = fft_context.fft_output[i].r;
    float imag = fft_context.fft_output[i].i;
    float magnitude = sqrtf(real * real + imag * imag);
    target_normalized_magnitudes[i] =
        fminf(1.0f, magnitude / NORM_FACTOR_HARMONICS);
    if (target_normalized_magnitudes[i] <
        0.0f) { // Should not happen with sqrtf
      target_normalized_magnitudes[i] = 0.0f;
    }
  }

  // Apply smoothing to the normalized magnitudes
  for (int i = 0; i < NUM_OSCILLATORS; ++i) {
    g_mono_voice.smoothed_normalized_magnitudes[i] =
        AMPLITUDE_SMOOTHING_ALPHA * target_normalized_magnitudes[i] +
        (1.0f - AMPLITUDE_SMOOTHING_ALPHA) *
            g_mono_voice.smoothed_normalized_magnitudes[i];
  }

  // 2. Generate audio samples
  for (unsigned int sample_idx = 0; sample_idx < buffer_size; ++sample_idx) {
    float current_sample_sum = 0.0f;

    // Play all oscillators
    for (int osc_idx = 0; osc_idx < NUM_OSCILLATORS; ++osc_idx) {
      // if (osc_idx == 0) { // DEBUG: Only process the fundamental - REMOVED to
      // enable all oscillators
      float harmonic_multiple =
          (float)(osc_idx +
                  1); // Fundamental is 1*f0, first harmonic is 2*f0, etc.
      float osc_freq = g_mono_voice.fundamental_frequency * harmonic_multiple;
      float phase_increment = TWO_PI * osc_freq / (float)SAMPLING_FREQUENCY;

      // Use smoothed amplitudes
      float smoothed_amplitude =
          g_mono_voice.smoothed_normalized_magnitudes[osc_idx];

      // Apply spectral tilt (1/harmonic_number, where fundamental is 1st
      // harmonic) - REVERTED as per user feedback
      // float amplitude = smoothed_amplitude; // Was: smoothed_amplitude /
      // (float)(osc_idx + 1);

      // Apply non-linear amplitude mapping (gamma)
      float amplitude = powf(smoothed_amplitude, AMPLITUDE_GAMMA);
      // Ensure amplitude is not NaN if smoothed_amplitude is negative and gamma
      // is non-integer
      if (smoothed_amplitude < 0.0f &&
          (AMPLITUDE_GAMMA != floorf(AMPLITUDE_GAMMA))) {
        amplitude =
            0.0f; // or handle appropriately, e.g. -powf(-smoothed_amplitude,
                  // AMPLITUDE_GAMMA) if desired
      }

      float osc_sample =
          amplitude * sinf(g_mono_voice.oscillators[osc_idx].phase);
      current_sample_sum += osc_sample;

      g_mono_voice.oscillators[osc_idx].phase += phase_increment;
      if (g_mono_voice.oscillators[osc_idx].phase >= TWO_PI) {
        g_mono_voice.oscillators[osc_idx].phase -= TWO_PI;
      }
      // } else { // DEBUG related else block - REMOVED
      // } // DEBUG related if block - REMOVED
    }

    // Apply master volume
    current_sample_sum *= MASTER_VOLUME;

    // Simple limiter to prevent extreme clipping, can be improved later
    if (current_sample_sum > 1.0f)
      current_sample_sum = 1.0f;
    else if (current_sample_sum < -1.0f)
      current_sample_sum = -1.0f;

    audio_buffer[sample_idx] = current_sample_sum;
  }
}

// Fonction pour convertir une ligne d'image RGB en niveaux de gris et mettre à
// jour l'historique
static void process_image_data_for_fft(DoubleBuffer *image_db) {
  if (image_db == NULL) {
    fprintf(stderr, "process_image_data_for_fft: image_db is NULL\n");
    return;
  }

  // printf("process_image_data_for_fft: En attente de données d'image...\n");
  // // Commenté Attendre que les données d'image soient prêtes
  pthread_mutex_lock(&image_db->mutex);
  while (!image_db->dataReady && keepRunning) {
    // Timeout pour éviter un blocage infini
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; // Timeout de 1 seconde
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

  // À ce stade, image_db->dataReady est vrai et nous avons le mutex
  // Les données sont dans image_db->activeBuffer_R/G/B
  // Nous allons les copier localement pour libérer rapidement le mutex
  float current_grayscale_line[CIS_MAX_PIXELS_NB];

  // Convertir la ligne RGB en niveaux de gris (conversion standard)
  for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
    current_grayscale_line[i] = 0.299f * image_db->activeBuffer_R[i] +
                                0.587f * image_db->activeBuffer_G[i] +
                                0.114f * image_db->activeBuffer_B[i];
  }

  // On peut maintenant libérer le mutex du DoubleBuffer
  pthread_mutex_unlock(&image_db->mutex);

  // Mettre à jour l'historique des lignes en niveaux de gris
  pthread_mutex_lock(&image_history_mutex);

  // Stocker la ligne courante dans l'historique (écrasement des plus anciennes)
  memcpy(image_line_history[history_write_index].line_data,
         current_grayscale_line, CIS_MAX_PIXELS_NB * sizeof(float));

  // Mise à jour de l'index d'écriture circulaire
  history_write_index = (history_write_index + 1) % MOVING_AVERAGE_WINDOW_SIZE;

  // Mise à jour du compteur de remplissage
  if (history_fill_count < MOVING_AVERAGE_WINDOW_SIZE) {
    history_fill_count++;
  }

  // Calculer la moyenne glissante si l'historique contient assez de lignes
  if (history_fill_count > 0) {
    // Initialiser le buffer d'entrée de la FFT à zéro
    memset(fft_context.fft_input, 0,
           CIS_MAX_PIXELS_NB * sizeof(kiss_fft_scalar));

    // Calculer la moyenne pour chaque pixel
    for (int j = 0; j < CIS_MAX_PIXELS_NB; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < history_fill_count; ++k) {
        int idx = (history_write_index - k + MOVING_AVERAGE_WINDOW_SIZE) %
                  MOVING_AVERAGE_WINDOW_SIZE;
        idx = (idx < 0) ? idx + MOVING_AVERAGE_WINDOW_SIZE
                        : idx; // s'assurer que l'index est positif
        sum += image_line_history[idx].line_data[j];
      }
      // Stocker la moyenne dans le buffer d'entrée de la FFT
      fft_context.fft_input[j] = sum / history_fill_count;
    }

    // Effectuer la FFT
    kiss_fftr(fft_context.fft_cfg, fft_context.fft_input,
              fft_context.fft_output);

    // Afficher les magnitudes de la sortie FFT (seulement si nous avons une
    // fenêtre complète)
    // if (history_fill_count == MOVING_AVERAGE_WINDOW_SIZE) {
    //   static time_t last_print_time = 0;
    //   time_t current_time = time(NULL);

    //   if (current_time - last_print_time >= 1) {
    //     printf("\n--- FFT Output (Magnitudes) ---\n");
    //     // Afficher les 30 premiers bins FFT
    //     int num_bins_to_display = 30;
    //     for (int i = 0;
    //          i < num_bins_to_display && i < CIS_MAX_PIXELS_NB / 2 + 1; ++i) {
    //       float real = fft_context.fft_output[i].r;
    //       float imag = fft_context.fft_output[i].i;
    //       float magnitude = sqrtf(real * real + imag * imag);
    //       printf("Bin[%d] = %f\n", i, magnitude);
    //     }
    //     printf("----------------------------\n");
    //     last_print_time = current_time;
    //   }
    // }
  }

  pthread_mutex_unlock(&image_history_mutex);
}

// Fonction pour générer des données de test pour la FFT
static void generate_test_data_for_fft(void) {
  static int call_count = 0;

  printf("Génération de données de test pour la FFT (%d)...\n", call_count++);

  // Verrouiller le mutex pour accéder à l'historique
  pthread_mutex_lock(&image_history_mutex);

  // Créer une ligne de test avec une sinusoïde simple
  float test_line[CIS_MAX_PIXELS_NB];
  for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
    // Générer une onde sinusoïdale avec 10 cycles sur toute la largeur
    float phase = 10.0f * 2.0f * M_PI * (float)i / (float)CIS_MAX_PIXELS_NB;
    test_line[i] = sinf(phase) * 100.0f;

    // Ajouter une seconde fréquence
    test_line[i] += sinf(5.0f * phase) * 50.0f;

    // Ajouter un peu de bruit aléatoire
    test_line[i] += (rand() % 100) / 100.0f * 20.0f;
  }

  // Stocker dans l'historique
  memcpy(image_line_history[history_write_index].line_data, test_line,
         CIS_MAX_PIXELS_NB * sizeof(float));

  // Mise à jour de l'index circulaire
  history_write_index = (history_write_index + 1) % MOVING_AVERAGE_WINDOW_SIZE;

  // Mise à jour du compteur de remplissage
  if (history_fill_count < MOVING_AVERAGE_WINDOW_SIZE) {
    history_fill_count++;
    printf("Historique rempli à %d/%d\n", history_fill_count,
           MOVING_AVERAGE_WINDOW_SIZE);
  }

  // Calculer et afficher la FFT si l'historique est suffisamment rempli
  if (history_fill_count > 0) {
    // Initialiser le buffer d'entrée de la FFT à zéro
    memset(fft_context.fft_input, 0,
           CIS_MAX_PIXELS_NB * sizeof(kiss_fft_scalar));

    // Calculer la moyenne pour chaque pixel
    for (int j = 0; j < CIS_MAX_PIXELS_NB; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < history_fill_count; ++k) {
        int idx = (history_write_index - k + MOVING_AVERAGE_WINDOW_SIZE) %
                  MOVING_AVERAGE_WINDOW_SIZE;
        idx = (idx < 0) ? idx + MOVING_AVERAGE_WINDOW_SIZE : idx;
        sum += image_line_history[idx].line_data[j];
      }
      fft_context.fft_input[j] = sum / history_fill_count;
    }

    // Effectuer la FFT
    kiss_fftr(fft_context.fft_cfg, fft_context.fft_input,
              fft_context.fft_output);

    // Afficher les résultats de la FFT si l'historique est complet
    // if (history_fill_count == MOVING_AVERAGE_WINDOW_SIZE) {
    //   static time_t last_print_time_test = 0;
    //   time_t current_time_test = time(NULL);

    //   if (current_time_test - last_print_time_test >= 1) {
    //     printf("\n--- FFT Output (Test Data, Magnitudes) ---\n");

    //     // Afficher les 30 premiers bins FFT
    //     int num_bins_to_display = 30;
    //     for (int i = 0;
    //          i < num_bins_to_display && i < CIS_MAX_PIXELS_NB / 2 + 1; ++i) {
    //       float real = fft_context.fft_output[i].r;
    //       float imag = fft_context.fft_output[i].i;
    //       float magnitude = sqrtf(real * real + imag * imag);
    //       printf("Bin[%d] = %f\n", i, magnitude);
    //     }
    //     printf("----------------------------\n");
    //     last_print_time_test = current_time_test;
    //   }
    // }
  }

  pthread_mutex_unlock(&image_history_mutex);
}

void *synth_fftMode_thread_func(void *arg) {
  // Récupération du DoubleBuffer à partir du Context
  DoubleBuffer *image_db = NULL;

  // Si l'argument n'est pas NULL, on l'interprète comme Context
  if (arg != NULL) {
    Context *ctx = (Context *)arg;
    image_db = ctx->doubleBuffer;
    printf(
        "synth_fftMode_thread_func: DoubleBuffer obtenu depuis le contexte.\n");
    fflush(stdout);
  } else {
    printf("synth_fftMode_thread_func: Aucun contexte fourni, pas de "
           "DoubleBuffer disponible.\n");
    fflush(stdout);
  }

  printf("synth_fftMode_thread_func started.\n");
  fflush(stdout);

  // Ajouter de l'aléatoire pour la génération de données de test
  srand(time(NULL));

  while (keepRunning) {
    if (image_db != NULL) {
      process_image_data_for_fft(image_db);
    } else {
      // Si aucun DoubleBuffer n'est disponible (pas de contexte passé),
      // alors générer des données de test.
      printf("synth_fftMode_thread_func: Aucun DoubleBuffer. Utilisation des "
             "données de test.\n");
      generate_test_data_for_fft();
    }
    fflush(stdout); // S'assurer que les messages sont affichés

    // Portion audio (inchangée)
    int local_producer_idx;
    pthread_mutex_lock(&fft_buffer_index_mutex);
    local_producer_idx = fft_current_buffer_index;
    pthread_mutex_unlock(&fft_buffer_index_mutex);

    pthread_mutex_lock(&fft_audio_buffers[local_producer_idx].mutex);
    while (fft_audio_buffers[local_producer_idx].ready == 1 && keepRunning) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 1; // Wait for 1 second
      int wait_ret = pthread_cond_timedwait(
          &fft_audio_buffers[local_producer_idx].cond,
          &fft_audio_buffers[local_producer_idx].mutex, &ts);
      if (wait_ret == ETIMEDOUT && !keepRunning) {
        pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);
        goto cleanup_thread;
      }
    }

    // Generate audio data
    synth_fftMode_process(fft_audio_buffers[local_producer_idx].data,
                          AUDIO_BUFFER_SIZE);

    fft_audio_buffers[local_producer_idx].ready = 1;
    pthread_cond_signal(&fft_audio_buffers[local_producer_idx].cond);
    pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);

    pthread_mutex_lock(&fft_buffer_index_mutex);
    fft_current_buffer_index = 1 - local_producer_idx;
    pthread_mutex_unlock(&fft_buffer_index_mutex);

    // Pause légère pour alléger la charge CPU - REMOVED FOR DEBUGGING
    // GRANULARITY usleep(10000); // 10ms
  }

cleanup_thread:
  printf("synth_fftMode_thread_func stopping.\n");

  // Nettoyage des ressources FFT
  if (fft_context.fft_cfg != NULL) {
    kiss_fftr_free(fft_context.fft_cfg);
    fft_context.fft_cfg = NULL;
  }

  return NULL;
}

// --- MIDI Note Handling ---

// MIDI Note to Frequency (equal temperament A4 = 440 Hz = MIDI Note 69)
static float midi_note_to_frequency(int noteNumber) {
  if (noteNumber < 0 || noteNumber > 127) {
    fprintf(stderr, "Invalid MIDI note number: %d\n", noteNumber);
    return 0.0f; // Or a default frequency, or handle error differently
  }
  return 440.0f * powf(2.0f, (float)(noteNumber - 69) / 12.0f);
}

void synth_fft_note_on(int noteNumber, int velocity) {
  if (velocity > 0) { // True Note On
    g_mono_voice.fundamental_frequency = midi_note_to_frequency(noteNumber);
    g_mono_voice.active = 1;
    // Reset phases of oscillators for a clean attack - important for monophonic
    // retrigger
    for (int i = 0; i < NUM_OSCILLATORS; ++i) {
      g_mono_voice.oscillators[i].phase = 0.0f;
    }
    // Optional: could use velocity to scale a master amplitude or ADSR envelope
    // depth
    printf("SYNTH_FFT: Note On: %d, Vel: %d, Freq: %f Hz -> Voice Active\n",
           noteNumber, velocity, g_mono_voice.fundamental_frequency);
  } else { // Note On with velocity 0 is treated as Note Off
    synth_fft_note_off(noteNumber);
  }
}

void synth_fft_note_off(int noteNumber) {
  // Simple monophonic: if the released note is the current one, turn off.
  float released_note_freq = midi_note_to_frequency(noteNumber);

  // Compare frequencies with a small tolerance due to float precision
  if (g_mono_voice.active &&
      fabsf(g_mono_voice.fundamental_frequency - released_note_freq) <
          0.01f) { // Check if active to avoid deactivating an already inactive
                   // voice or one playing a different note (if logic changes)
    g_mono_voice.active = 0;
    printf("SYNTH_FFT: Note Off: %d, Freq: %f Hz -> Voice Inactive\n",
           noteNumber, released_note_freq);
  }
}
