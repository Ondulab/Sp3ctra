/**
 * @file synth_photowave.h
 * @brief Photowave synthesis engine - transforms image lines into audio waveforms
 * 
 * Photowave is a novel synthesis method that performs spatial→temporal transduction,
 * converting pixel luminance values directly into audio samples. Each image line
 * becomes a "dynamic optical wavetable" that can be scanned at different rates
 * and directions to produce pitched audio.
 * 
 * Key features:
 * - 8-voice polyphony with intelligent voice stealing
 * - MIDI-controlled pitch (standard MIDI tuning A4=440Hz)
 * - ADSR envelope for volume and filter modulation
 * - LFO for vibrato effect
 * - Lowpass filter with envelope modulation
 * - 3 scanning modes: Left→Right, Right→Left, Dual/Ping-Pong
 * - Linear and cubic interpolation
 * - Real-time safe (no allocations, no locks in audio callback)
 */

#ifndef SYNTH_PHOTOWAVE_H
#define SYNTH_PHOTOWAVE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "../common/synth_common.h"  // For AdsrState and AdsrEnvelope

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ========================================================================== */

#define PHOTOWAVE_MAX_PIXELS 4096           // Maximum supported DPI (400 DPI = 3456 pixels)
#define PHOTOWAVE_MIN_FREQUENCY 10.0f       // Minimum frequency (Hz)
#define PHOTOWAVE_MAX_FREQUENCY 12000.0f    // Maximum frequency (Hz)
#define PHOTOWAVE_DEFAULT_AMPLITUDE 0.5f    // Default amplitude (0.0 to 1.0)

#define NUM_PHOTOWAVE_VOICES 8              // Number of polyphonic voices
#define MIN_AUDIBLE_AMPLITUDE 0.001f        // Skip voices below this threshold

/* ============================================================================
 * ENUMERATIONS
 * ========================================================================== */

/**
 * @brief Scanning direction modes for reading the image line
 */
typedef enum {
    PHOTOWAVE_SCAN_LEFT_TO_RIGHT = 0,  // Standard left-to-right scan
    PHOTOWAVE_SCAN_RIGHT_TO_LEFT = 1,  // Reverse scan (right-to-left)
    PHOTOWAVE_SCAN_DUAL = 2            // Ping-pong: L→R then R→L (double period)
} PhotowaveScanMode;

/**
 * @brief Interpolation methods for sub-pixel sampling
 */
typedef enum {
    PHOTOWAVE_INTERP_LINEAR = 0,  // Linear interpolation (fast, good quality)
    PHOTOWAVE_INTERP_CUBIC = 1    // Cubic interpolation (slower, smoother)
} PhotowaveInterpMode;

/* ============================================================================
 * DATA STRUCTURES
 * ========================================================================== */

/**
 * @brief Simple first-order lowpass filter with envelope modulation
 * 
 * Implements a basic RC lowpass filter with cutoff frequency modulated by ADSR.
 */
typedef struct {
    float base_cutoff_hz;       // Base cutoff frequency when ADSR is at 0
    float filter_env_depth;     // How much ADSR modulates cutoff (Hz, can be negative)
    float prev_output;          // Previous output sample (filter state)
    float alpha;                // Smoothing coefficient (recalculated per sample)
} PhotowaveLowpassFilter;

/**
 * @brief Single polyphonic voice for Photowave synthesis
 * 
 * Each voice maintains its own playback state, ADSR envelopes, and filter state.
 */
typedef struct {
    // Playback state
    float phase;                    // Current phase position (0.0 to 1.0)
    float frequency;                // Current playback frequency (Hz)
    
    // MIDI information
    uint8_t midi_note;              // MIDI note number (0-127)
    uint8_t velocity;               // MIDI velocity (0-127)
    bool active;                    // True if voice is in use
    unsigned long long trigger_order; // Order in which voice was triggered (for stealing)
    
    // ADSR envelopes
    AdsrEnvelope volume_adsr;       // Volume envelope
    AdsrEnvelope filter_adsr;       // Filter envelope
    
    // Filter state (per-voice)
    PhotowaveLowpassFilter lowpass; // Lowpass filter with state
} PhotowaveVoice;

/**
 * @brief Configuration parameters for Photowave synthesis
 * 
 * These parameters can be loaded from sp3ctra.ini [photowave] section
 * and modified at runtime via MIDI CC or API calls.
 */
typedef struct {
    PhotowaveScanMode scan_mode;      // Scanning direction mode
    PhotowaveInterpMode interp_mode;  // Interpolation method
    float amplitude;                  // Master amplitude (0.0 to 1.0)
} PhotowaveConfig;

/**
 * @brief Runtime state for Photowave synthesis engine
 * 
 * This structure contains all state needed for real-time audio generation.
 * All buffers are preallocated at init time to ensure RT safety.
 */
typedef struct {
    // Configuration (can be modified atomically)
    PhotowaveConfig config;
    
    // Image data (read-only in audio callback)
    const uint8_t *image_line;        // Pointer to current image line (grayscale)
    int pixel_count;                  // Number of pixels in image line
    
    // Polyphonic voices
    PhotowaveVoice voices[NUM_PHOTOWAVE_VOICES]; // Array of polyphonic voices
    unsigned long long current_trigger_order;    // Global trigger order counter
    
    // Global modulation
    LfoState global_vibrato_lfo;      // Global LFO for vibrato (shared by all voices)
    
    // Audio parameters
    float sample_rate;                // Audio sample rate (Hz)
    float f_min;                      // Minimum frequency (calculated from DPI)
    float f_max;                      // Maximum frequency (12 kHz)
    
    // Statistics (for debugging/monitoring)
    uint64_t samples_generated;       // Total samples generated
    uint32_t buffer_underruns;        // Count of buffer underruns (should be 0)
} PhotowaveState;

/**
 * @brief Audio buffer structure for double-buffering with producer thread
 * 
 * This structure is used to pass audio data from the Photowave generation
 * thread to the audio callback in a thread-safe manner.
 */
typedef struct {
    float *data;              // Dynamically allocated buffer (size = audio_buffer_size)
    volatile int ready;       // 0 = not ready, 1 = ready for consumption
    pthread_mutex_t mutex;    // Mutex for thread synchronization
    pthread_cond_t cond;      // Condition variable for signaling
} PhotowaveAudioBuffer;

/* ============================================================================
 * PUBLIC API - INITIALIZATION & CLEANUP
 * ========================================================================== */

/**
 * @brief Initialize the Photowave synthesis engine
 * 
 * Allocates all necessary buffers and initializes state.
 * Must be called before any other Photowave functions.
 * 
 * @param state Pointer to PhotowaveState structure to initialize
 * @param sample_rate Audio sample rate in Hz (e.g., 48000.0f)
 * @param pixel_count Number of pixels in image line (DPI-dependent)
 * @return 0 on success, negative error code on failure
 */
int synth_photowave_init(PhotowaveState *state, float sample_rate, int pixel_count);

/**
 * @brief Clean up and free resources used by Photowave engine
 * 
 * Frees all allocated buffers. After calling this, the state
 * must be re-initialized before use.
 * 
 * @param state Pointer to PhotowaveState structure to clean up
 */
void synth_photowave_cleanup(PhotowaveState *state);

/* ============================================================================
 * PUBLIC API - AUDIO PROCESSING
 * ========================================================================== */

/**
 * @brief Generate audio samples from current image line
 * 
 * This is the main RT-safe audio callback function. It reads from the
 * current image line and generates audio samples based on the current
 * frequency and scanning mode.
 * 
 * REAL-TIME CONSTRAINTS:
 * - No dynamic allocation
 * - No locks/mutexes
 * - No blocking I/O
 * - No logging/printf
 * - Bounded execution time (O(n) where n = num_frames)
 * 
 * @param state Pointer to PhotowaveState structure
 * @param output_left Output buffer for left channel (stereo)
 * @param output_right Output buffer for right channel (stereo)
 * @param num_frames Number of frames to generate
 */
void synth_photowave_process(PhotowaveState *state,
                             float *output_left,
                             float *output_right,
                             int num_frames);

/* ============================================================================
 * PUBLIC API - PARAMETER SETTERS (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Set the current image line to read from
 * 
 * @param state Pointer to PhotowaveState structure
 * @param image_line Pointer to grayscale image line data (uint8_t array)
 * @param pixel_count Number of pixels in the line
 */
void synth_photowave_set_image_line(PhotowaveState *state,
                                   const uint8_t *image_line,
                                   int pixel_count);

/**
 * @brief Set scanning mode (Left→Right, Right→Left, or Dual)
 * 
 * @param state Pointer to PhotowaveState structure
 * @param mode Scanning mode to use
 */
void synth_photowave_set_scan_mode(PhotowaveState *state, PhotowaveScanMode mode);

/**
 * @brief Set interpolation mode (Linear or Cubic)
 * 
 * @param state Pointer to PhotowaveState structure
 * @param mode Interpolation mode to use
 */
void synth_photowave_set_interp_mode(PhotowaveState *state, PhotowaveInterpMode mode);

/**
 * @brief Set master amplitude (0.0 to 1.0)
 * 
 * @param state Pointer to PhotowaveState structure
 * @param amplitude Amplitude value (clamped to 0.0-1.0)
 */
void synth_photowave_set_amplitude(PhotowaveState *state, float amplitude);

/* ============================================================================
 * PUBLIC API - ADSR PARAMETER SETTERS (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Set volume ADSR attack time
 * @param attack_s Attack time in seconds
 */
void synth_photowave_set_volume_adsr_attack(float attack_s);

/**
 * @brief Set volume ADSR decay time
 * @param decay_s Decay time in seconds
 */
void synth_photowave_set_volume_adsr_decay(float decay_s);

/**
 * @brief Set volume ADSR sustain level
 * @param sustain_level Sustain level (0.0 to 1.0)
 */
void synth_photowave_set_volume_adsr_sustain(float sustain_level);

/**
 * @brief Set volume ADSR release time
 * @param release_s Release time in seconds
 */
void synth_photowave_set_volume_adsr_release(float release_s);

/**
 * @brief Set filter ADSR attack time
 * @param attack_s Attack time in seconds
 */
void synth_photowave_set_filter_adsr_attack(float attack_s);

/**
 * @brief Set filter ADSR decay time
 * @param decay_s Decay time in seconds
 */
void synth_photowave_set_filter_adsr_decay(float decay_s);

/**
 * @brief Set filter ADSR sustain level
 * @param sustain_level Sustain level (0.0 to 1.0)
 */
void synth_photowave_set_filter_adsr_sustain(float sustain_level);

/**
 * @brief Set filter ADSR release time
 * @param release_s Release time in seconds
 */
void synth_photowave_set_filter_adsr_release(float release_s);

/* ============================================================================
 * PUBLIC API - LFO PARAMETER SETTERS (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Set vibrato LFO rate
 * @param rate_hz LFO frequency in Hz
 */
void synth_photowave_set_vibrato_rate(float rate_hz);

/**
 * @brief Set vibrato LFO depth
 * @param depth_semitones Modulation depth in semitones
 */
void synth_photowave_set_vibrato_depth(float depth_semitones);

/* ============================================================================
 * PUBLIC API - FILTER PARAMETER SETTERS (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Set global filter base cutoff frequency
 * @param cutoff_hz Base cutoff frequency in Hz
 */
void synth_photowave_set_filter_cutoff(float cutoff_hz);

/**
 * @brief Set global filter envelope depth
 * @param depth_hz Envelope modulation depth in Hz (can be negative)
 */
void synth_photowave_set_filter_env_depth(float depth_hz);

/* ============================================================================
 * PUBLIC API - MIDI CONTROL (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Handle MIDI Note On event
 * 
 * Starts playback at the frequency corresponding to the MIDI note.
 * Uses standard MIDI tuning with A4 (note 69) = 440 Hz.
 * 
 * @param state Pointer to PhotowaveState structure
 * @param note MIDI note number (0-127, typically 21-108 for piano range)
 * @param velocity MIDI velocity (0-127, affects amplitude)
 */
void synth_photowave_note_on(PhotowaveState *state, uint8_t note, uint8_t velocity);

/**
 * @brief Handle MIDI Note Off event
 * 
 * Triggers release phase of ADSR envelopes for matching note.
 * 
 * @param state Pointer to PhotowaveState structure
 * @param note MIDI note number (0-127)
 */
void synth_photowave_note_off(PhotowaveState *state, uint8_t note);

/**
 * @brief Handle MIDI Control Change (CC) event
 * 
 * Supported CC mappings:
 * - CC1 (Modulation): Scan mode (0-42=L→R, 43-84=R→L, 85-127=Dual)
 * - CC7 (Volume): Amplitude (0-127 → 0.0-1.0)
 * - CC74 (Brightness): Interpolation mode (0-63=Linear, 64-127=Cubic)
 * 
 * @param state Pointer to PhotowaveState structure
 * @param cc_number CC number (0-127)
 * @param cc_value CC value (0-127)
 */
void synth_photowave_control_change(PhotowaveState *state, uint8_t cc_number, uint8_t cc_value);

/* ============================================================================
 * PUBLIC API - PARAMETER GETTERS
 * ========================================================================== */

/**
 * @brief Get current configuration
 * 
 * @param state Pointer to PhotowaveState structure
 * @return Copy of current configuration
 */
PhotowaveConfig synth_photowave_get_config(const PhotowaveState *state);

/**
 * @brief Check if any voice is currently active
 * 
 * @param state Pointer to PhotowaveState structure
 * @return true if at least one voice is active, false otherwise
 */
bool synth_photowave_is_note_active(const PhotowaveState *state);

/* ============================================================================
 * THREAD INTEGRATION API
 * ========================================================================== */

/**
 * @brief Apply configuration from loaded config file
 * 
 * Applies photowave parameters from g_sp3ctra_config to the state.
 * Should be called after synth_photowave_init() and after config is loaded.
 * 
 * @param state Pointer to PhotowaveState structure
 */
void synth_photowave_apply_config(PhotowaveState *state);

/**
 * @brief Initialize Photowave mode (called once at startup)
 * 
 * Initializes global state and buffers for Photowave synthesis.
 */
void synth_photowave_mode_init(void);

/**
 * @brief Stop Photowave thread (called before pthread_join)
 * 
 * Signals the Photowave thread to stop by setting the running flag to 0.
 * Must be called before pthread_join() to ensure clean thread termination.
 */
void synth_photowave_thread_stop(void);

/**
 * @brief Cleanup Photowave mode (called at shutdown)
 * 
 * Frees all global resources allocated by Photowave mode.
 */
void synth_photowave_mode_cleanup(void);

/**
 * @brief Thread function for Photowave audio generation
 * 
 * This function runs in a separate thread and continuously generates
 * audio samples, filling the double buffers for consumption by the
 * audio callback.
 * 
 * @param arg Pointer to Context structure (unused currently)
 * @return NULL
 */
void *synth_photowave_thread_func(void *arg);

/* ============================================================================
 * GLOBAL VARIABLES (for thread integration)
 * ========================================================================== */

// Double buffer for Photowave audio output
extern PhotowaveAudioBuffer photowave_audio_buffers[2];

// Current buffer index for producer thread
extern volatile int photowave_current_buffer_index;

// Mutex for buffer index synchronization
extern pthread_mutex_t photowave_buffer_index_mutex;

// Global Photowave state instance
extern PhotowaveState g_photowave_state;

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_PHOTOWAVE_H */
