/*
 * synth_fft.h
 *
 *  Created on: 16 May 2025
 *      Author: Cline
 */

#ifndef SYNTH_FFT_H
#define SYNTH_FFT_H

#include "config.h" // For AUDIO_BUFFER_SIZE, SAMPLING_FREQUENCY, CIS_MAX_PIXELS_NB
#include "kissfft/kiss_fftr.h" // Pour la FFT réelle
#include <pthread.h>           // For mutex and cond
#include <stdint.h>            // For uint32_t, etc.

/* Synth Definitions */
#define NUM_OSCILLATORS 30
#define DEFAULT_FUNDAMENTAL_FREQUENCY 440.0f // A4 for testing

// Structure for a single oscillator
typedef struct {
  float phase;
  // Frequency will be derived from fundamental and harmonic index
  // Amplitude will be derived from FFT magnitudes
} OscillatorState;

// Structure for a monophonic synth voice
typedef struct {
  OscillatorState oscillators[NUM_OSCILLATORS];
  float smoothed_normalized_magnitudes[NUM_OSCILLATORS]; // For amplitude
                                                         // smoothing
  volatile float fundamental_frequency; // Volatile due to inter-thread access
  volatile int active;                  // Volatile due to inter-thread access
} MonophonicVoice;

/* Définitions pour la moyenne glissante */
#define MOVING_AVERAGE_WINDOW_SIZE 8 // Reduced from 32 for faster response

/* Structure pour stocker une ligne d'image en niveaux de gris */
typedef struct {
  float line_data[CIS_MAX_PIXELS_NB];
} GrayscaleLine;

/* Structure pour la FFT */
typedef struct {
  kiss_fftr_cfg fft_cfg;                        // Configuration de KissFFT
  kiss_fft_scalar fft_input[CIS_MAX_PIXELS_NB]; // Buffer d'entrée pour la FFT
  kiss_fft_cpx
      fft_output[CIS_MAX_PIXELS_NB / 2 + 1]; // Buffer de sortie pour la FFT
} FftContext;

/* Exported types ------------------------------------------------------------*/
typedef struct {
  float data[AUDIO_BUFFER_SIZE];
  volatile int ready; // 0 = not ready, 1 = ready for consumption
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} FftAudioDataBuffer;

/* Exported variables --------------------------------------------------------*/
extern FftAudioDataBuffer fft_audio_buffers[2]; // Double buffer for FFT synth
extern volatile int
    fft_current_buffer_index; // Index of the buffer to be filled by producer
extern pthread_mutex_t
    fft_buffer_index_mutex; // Mutex for fft_current_buffer_index

/* Variables pour la moyenne glissante et la FFT */
extern GrayscaleLine
    image_line_history[MOVING_AVERAGE_WINDOW_SIZE]; // Historique des lignes
extern int
    history_write_index; // Index pour l'écriture circulaire dans l'historique
extern int history_fill_count; // Nombre d'éléments valides dans l'historique
extern pthread_mutex_t
    image_history_mutex;             // Pour protéger l'accès à l'historique
extern FftContext fft_context;       // Contexte pour la FFT
extern MonophonicVoice g_mono_voice; // Global monophonic voice instance

/* Exported functions prototypes ---------------------------------------------*/
void synth_fftMode_init(void);
void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size);
void *
synth_fftMode_thread_func(void *arg); // Renamed to avoid conflict if
                                      // synth_fftMode_thread is used elsewhere

// MIDI Note handling functions for synth_fft
void synth_fft_note_on(int noteNumber, int velocity);
void synth_fft_note_off(int noteNumber);

#endif /* SYNTH_FFT_H */
