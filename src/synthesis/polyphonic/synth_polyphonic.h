/*
 * synth_polyphonic.h
 *
 *  Created on: 16 May 2025
 *      Author: Cline
 */

#ifndef SYNTH_POLYPHONIC_H
#define SYNTH_POLYPHONIC_H

#include "config.h" // For AUDIO_BUFFER_SIZE, SAMPLING_FREQUENCY, CIS_MAX_PIXELS_NB
#include "kissfft/kiss_fftr.h" // Pour la FFT réelle
#include <pthread.h>           // For mutex and cond
#include <stdint.h>            // For uint32_t, etc.
#include "../common/synth_common.h"  // For AdsrState and AdsrEnvelope

/* Synth Definitions */
// Maximum compile-time limits for static array allocation
#define MAX_POLY_VOICES 32              // Maximum number of polyphonic voices
#define MAX_MAPPED_OSCILLATORS 256      // Maximum oscillators per voice

// Runtime-configurable polyphonic synthesis parameters (loaded from sp3ctra.ini)
// These will be <= the MAX_ values above
extern int g_num_poly_voices;           // Actual number of voices to use (1-32)
extern int g_max_mapped_oscillators;    // Actual oscillators per voice (1-256)

#define DEFAULT_FUNDAMENTAL_FREQUENCY 440.0f // A4 for testing

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
  OscillatorState oscillators[MAX_MAPPED_OSCILLATORS]; // Per-voice phase
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

/* Exported types ------------------------------------------------------------*/
typedef struct {
  float *data; // dynamically allocated with size = g_sp3ctra_config.audio_buffer_size
  volatile int ready; // 0 = not ready, 1 = ready for consumption
  uint64_t write_timestamp_us; // Timestamp when buffer was written (microseconds)
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} FftAudioDataBuffer;

/* Exported variables --------------------------------------------------------*/
extern unsigned long long
    g_current_trigger_order; // Global trigger order counter
extern FftAudioDataBuffer
    polyphonic_audio_buffers[2]; // Double buffer for polyphonic synth
extern volatile int polyphonic_current_buffer_index; // Index of the buffer to
                                                     // be filled by producer
extern pthread_mutex_t
    polyphonic_buffer_index_mutex; // Mutex for polyphonic_current_buffer_index

// Polyphony related globals
extern SynthVoice poly_voices[MAX_POLY_VOICES];
extern float global_smoothed_magnitudes[MAX_MAPPED_OSCILLATORS];
extern SpectralFilterParams global_spectral_filter_params;

extern LfoState global_vibrato_lfo; // Global LFO for vibrato

/* Exported functions prototypes ---------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

void synth_polyphonicMode_init(void);
void synth_polyphonicMode_process(float *audio_buffer,
                                  unsigned int buffer_size);
void *synth_polyphonicMode_thread_func(
    void *arg); // Renamed to avoid conflict if
                // synth_polyphonicMode_thread is used elsewhere

// MIDI Note handling functions for synth_polyphonic
void synth_polyphonic_note_on(int noteNumber, int velocity);
void synth_polyphonic_note_off(int noteNumber);

// Functions to set ADSR parameters for synth_polyphonic volume envelope
void synth_polyphonic_set_volume_adsr_attack(float attack_s);
void synth_polyphonic_set_volume_adsr_decay(float decay_s);
void synth_polyphonic_set_volume_adsr_sustain(
    float sustain_level); // 0.0 to 1.0
void synth_polyphonic_set_volume_adsr_release(float release_s);

// Functions to set LFO parameters
void synth_polyphonic_set_vibrato_rate(float rate_hz);
void synth_polyphonic_set_vibrato_depth(float depth_semitones);

// Functions to set filter parameters
void synth_polyphonic_set_filter_cutoff(float cutoff_hz);
void synth_polyphonic_set_filter_env_depth(float depth_hz);

// Functions to set filter ADSR parameters
void synth_polyphonic_set_filter_adsr_attack(float attack_s);
void synth_polyphonic_set_filter_adsr_decay(float decay_s);
void synth_polyphonic_set_filter_adsr_sustain(float sustain_level);
void synth_polyphonic_set_filter_adsr_release(float release_s);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_POLYPHONIC_H */
