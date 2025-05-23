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
#define NUM_POLY_VOICES 32 // Increased to 32 polyphonic voices
#define DEFAULT_FUNDAMENTAL_FREQUENCY 440.0f // A4 for testing

/* ADSR Envelope Definitions */
typedef enum {
  ADSR_STATE_IDLE,
  ADSR_STATE_ATTACK,
  ADSR_STATE_DECAY,
  ADSR_STATE_SUSTAIN,
  ADSR_STATE_RELEASE
} AdsrState;

typedef struct {
  AdsrState state;
  float attack_time_samples;  // Attack time in samples
  float decay_time_samples;   // Decay time in samples
  float sustain_level;        // Sustain level (0.0 to 1.0)
  float release_time_samples; // Release time in samples

  float current_output;      // Current envelope output value (0.0 to 1.0)
  long long current_samples; // Counter for samples in current state
  float attack_increment;    // Value to add per sample in attack phase
  float decay_decrement;     // Value to subtract per sample in decay phase
  float release_decrement;   // Value to subtract per sample in release phase
  // Default ADSR values (can be made configurable later)
  // Times in seconds, will be converted to samples in init
  float attack_s;
  float decay_s;
  float release_s;
} AdsrEnvelope;

/* Filter Definitions */
typedef struct S_SpectralFilterParams { // Renamed struct tag
  // Parameters for filter modulation by ADSR
  float base_cutoff_hz;   // Base cutoff frequency when ADSR is at 0
  float filter_env_depth; // How much ADSR modulates cutoff (can be positive or
                          // negative)
  // prev_output and alpha are removed as they are not needed for this approach
} SpectralFilterParams; // Renamed typedef alias

// Structure for a single oscillator
typedef struct {
  float phase;
  // Frequency will be derived from fundamental and harmonic index
  // Amplitude will be derived from FFT magnitudes
} OscillatorState;

// Structure for a single polyphonic synth voice (renamed from MonophonicVoice)
typedef struct {
  OscillatorState oscillators[NUM_OSCILLATORS]; // Per-voice phase
  // smoothed_normalized_magnitudes will be global, shared by all voices for
  // timbre

  volatile float fundamental_frequency;
  AdsrState
      voice_state; // Tracks overall state of the voice (idle, attack, decay,
                   // sustain, release) This replaces the simple 'active' flag.
  int midi_note_number; // MIDI note number this voice is playing, for Note Off
                        // matching

  AdsrEnvelope volume_adsr; // Each voice has its own volume ADSR state
  AdsrEnvelope filter_adsr; // Each voice has its own filter ADSR state
  // SpectralFilterParams will be global, shared by all voices for filter
  // character

  float last_velocity; // Normalized velocity (0.0 to 1.0) of the last Note On
                       // for this voice
  unsigned long long
      last_triggered_order; // Order in which this voice was triggered
} SynthVoice;

/* Définitions pour la moyenne glissante */
#define MOVING_AVERAGE_WINDOW_SIZE 1 // Reduced from 32 for faster response

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
extern unsigned long long
    g_current_trigger_order;                    // Global trigger order counter
extern FftAudioDataBuffer fft_audio_buffers[2]; // Double buffer for FFT synth
extern volatile int
    fft_current_buffer_index; // Index of the buffer to be filled by producer
extern pthread_mutex_t
    fft_buffer_index_mutex; // Mutex for fft_current_buffer_index

/* Variables pour la moyenne glissante et la FFT */
extern GrayscaleLine image_line_history[MOVING_AVERAGE_WINDOW_SIZE];
extern int history_write_index;
extern int history_fill_count;
extern pthread_mutex_t image_history_mutex;
extern FftContext fft_context;

// Polyphony related globals
extern SynthVoice poly_voices[NUM_POLY_VOICES];
extern float global_smoothed_magnitudes[NUM_OSCILLATORS];
extern SpectralFilterParams global_spectral_filter_params;

/* LFO State Definition */
typedef struct {
  float phase;
  float phase_increment;
  float current_output; // Output of LFO, typically -1.0 to 1.0
  // Parameters
  float rate_hz;
  float depth_semitones; // Modulation depth in semitones
} LfoState;

extern LfoState global_vibrato_lfo; // Global LFO for vibrato

/* Exported functions prototypes ---------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

void synth_fftMode_init(void);
void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size);
void *
synth_fftMode_thread_func(void *arg); // Renamed to avoid conflict if
                                      // synth_fftMode_thread is used elsewhere

// MIDI Note handling functions for synth_fft
void synth_fft_note_on(int noteNumber, int velocity);
void synth_fft_note_off(int noteNumber);

// Functions to set ADSR parameters for synth_fft volume envelope
void synth_fft_set_volume_adsr_attack(float attack_s);
void synth_fft_set_volume_adsr_decay(float decay_s);
void synth_fft_set_volume_adsr_sustain(float sustain_level); // 0.0 to 1.0
void synth_fft_set_volume_adsr_release(float release_s);

// Functions to set LFO parameters
void synth_fft_set_vibrato_rate(float rate_hz);
void synth_fft_set_vibrato_depth(float depth_semitones);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_FFT_H */
