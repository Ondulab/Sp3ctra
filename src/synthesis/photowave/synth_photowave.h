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
 * - MIDI-controlled pitch (exponential mapping from f_min to 12kHz)
 * - 3 scanning modes: Left→Right, Right→Left, Dual/Ping-Pong
 * - Spatial blur filter for waveform smoothing
 * - Linear and cubic interpolation
 * - Real-time safe (no allocations, no locks in audio callback)
 */

#ifndef SYNTH_PHOTOWAVE_H
#define SYNTH_PHOTOWAVE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * AUDIO BUFFER STRUCTURE (for thread integration)
 * ========================================================================== */

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
 * CONSTANTS
 * ========================================================================== */

#define PHOTOWAVE_MAX_PIXELS 4096  // Maximum supported DPI (400 DPI = 3456 pixels)
#define PHOTOWAVE_MIN_FREQUENCY 10.0f   // Minimum frequency (Hz)
#define PHOTOWAVE_MAX_FREQUENCY 12000.0f // Maximum frequency (Hz)
#define PHOTOWAVE_DEFAULT_AMPLITUDE 0.5f // Default amplitude (0.0 to 1.0)
#define PHOTOWAVE_MAX_BLUR_RADIUS 10    // Maximum blur kernel radius

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
 * @brief Configuration parameters for Photowave synthesis
 * 
 * These parameters can be loaded from sp3ctra.ini [photowave] section
 * and modified at runtime via MIDI CC or API calls.
 */
typedef struct {
    PhotowaveScanMode scan_mode;      // Scanning direction mode
    PhotowaveInterpMode interp_mode;  // Interpolation method
    float amplitude;                  // Master amplitude (0.0 to 1.0)
    int blur_radius;                  // Spatial blur kernel radius (0 to MAX_BLUR_RADIUS)
    float blur_amount;                // Blur mix amount (0.0 = dry, 1.0 = full blur)
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
    
    // Preallocated buffers for RT processing
    float *blur_buffer;               // Blurred waveform buffer [PHOTOWAVE_MAX_PIXELS]
    float *temp_buffer;               // Temporary buffer for blur computation [PHOTOWAVE_MAX_PIXELS]
    
    // Playback state
    float phase;                      // Current phase position (0.0 to 1.0)
    float phase_increment;            // Phase increment per sample
    float current_frequency;          // Current playback frequency (Hz)
    float target_frequency;           // Target frequency for smooth transitions
    bool note_active;                 // True when a MIDI note is active
    bool continuous_mode;             // True for continuous oscillator mode (always on)
    uint8_t current_note;             // Current MIDI note number (0-127)
    uint8_t current_velocity;         // Current MIDI velocity (0-127)
    
    // Audio parameters
    float sample_rate;                // Audio sample rate (Hz)
    float f_min;                      // Minimum frequency (calculated from DPI)
    float f_max;                      // Maximum frequency (12 kHz)
    
    // Statistics (for debugging/monitoring)
    uint64_t samples_generated;       // Total samples generated
    uint32_t buffer_underruns;        // Count of buffer underruns (should be 0)
} PhotowaveState;

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

/**
 * @brief Set blur filter parameters
 * 
 * @param state Pointer to PhotowaveState structure
 * @param radius Blur kernel radius (0 to PHOTOWAVE_MAX_BLUR_RADIUS)
 * @param amount Blur mix amount (0.0 = dry, 1.0 = full blur)
 */
void synth_photowave_set_blur(PhotowaveState *state, int radius, float amount);

/**
 * @brief Set playback frequency directly (for continuous oscillator mode)
 * 
 * Sets the frequency without using the note on/off system.
 * Useful for continuous pitch control via CC.
 * 
 * @param state Pointer to PhotowaveState structure
 * @param frequency Frequency in Hz (will be clamped to f_min-f_max range)
 */
void synth_photowave_set_frequency(PhotowaveState *state, float frequency);

/**
 * @brief Enable or disable continuous oscillator mode
 * 
 * In continuous mode, Photowave generates audio continuously without
 * requiring note_active to be true. Frequency is controlled directly.
 * 
 * @param state Pointer to PhotowaveState structure
 * @param enabled True to enable continuous mode, false for note-based mode
 */
void synth_photowave_set_continuous_mode(PhotowaveState *state, bool enabled);

/* ============================================================================
 * PUBLIC API - MIDI CONTROL (RT-SAFE)
 * ========================================================================== */

/**
 * @brief Handle MIDI Note On event
 * 
 * Starts playback at the frequency corresponding to the MIDI note.
 * Uses exponential mapping from f_min to f_max.
 * 
 * @param state Pointer to PhotowaveState structure
 * @param note MIDI note number (0-127, typically 21-108 for piano range)
 * @param velocity MIDI velocity (0-127, affects amplitude)
 */
void synth_photowave_note_on(PhotowaveState *state, uint8_t note, uint8_t velocity);

/**
 * @brief Handle MIDI Note Off event
 * 
 * Stops playback (sets note_active to false).
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
 * - CC71 (Resonance): Blur amount (0-127 → 0.0-1.0)
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
 * @brief Get current playback frequency
 * 
 * @param state Pointer to PhotowaveState structure
 * @return Current frequency in Hz
 */
float synth_photowave_get_frequency(const PhotowaveState *state);

/**
 * @brief Check if a note is currently active
 * 
 * @param state Pointer to PhotowaveState structure
 * @return true if note is active, false otherwise
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
