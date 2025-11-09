/**
 * @file synth_photowave.c
 * @brief Photowave synthesis engine implementation
 */

#include "synth_photowave.h"
#include "config_loader.h"  // For g_sp3ctra_config
#include "logger.h"         // For logging
#include "context.h"        // For Context structure
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>         // For usleep

/* ============================================================================
 * GLOBAL VARIABLES (for thread integration)
 * ========================================================================== */

// Double buffer for Photowave audio output
PhotowaveAudioBuffer photowave_audio_buffers[2];

// Current buffer index for producer thread
volatile int photowave_current_buffer_index = 0;

// Mutex for buffer index synchronization
pthread_mutex_t photowave_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global Photowave state instance
PhotowaveState g_photowave_state = {0};

// Thread running flag
static volatile int photowave_thread_running = 0;

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ========================================================================== */

/**
 * @brief Clamp a float value between min and max
 */
static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief Clamp an integer value between min and max
 */
static inline int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief Convert MIDI note number to frequency using standard MIDI tuning
 * 
 * Uses equal temperament tuning with A4 (note 69) = 440 Hz.
 * Formula: f = 440 × 2^((note - 69) / 12)
 * 
 * This ensures proper musical pitch correspondence:
 * - Note 60 (C4) = 261.63 Hz
 * - Note 69 (A4) = 440.00 Hz (reference)
 * - Note 72 (C5) = 523.25 Hz
 * 
 * @param note MIDI note number (0-127)
 * @param f_min Minimum frequency limit (unused, kept for API compatibility)
 * @param f_max Maximum frequency limit (clamps result if exceeded)
 * @return Frequency in Hz, clamped to [f_min, f_max]
 */
static float midi_note_to_frequency(uint8_t note, float f_min, float f_max) {
    // Standard MIDI tuning: A4 (note 69) = 440 Hz
    // Formula: f = 440 × 2^((note - 69) / 12)
    const float A4_FREQ = 440.0f;
    const int A4_NOTE = 69;
    
    // Calculate frequency using equal temperament
    float semitones_from_a4 = (float)(note - A4_NOTE);
    float frequency = A4_FREQ * powf(2.0f, semitones_from_a4 / 12.0f);
    
    // Clamp to valid range to avoid aliasing or subsonic frequencies
    if (frequency < f_min) frequency = f_min;
    if (frequency > f_max) frequency = f_max;
    
    return frequency;
}

/* ============================================================================
 * INITIALIZATION & CLEANUP
 * ========================================================================== */

int synth_photowave_init(PhotowaveState *state, float sample_rate, int pixel_count) {
    if (!state) return -1;
    if (sample_rate <= 0.0f) return -2;
    if (pixel_count <= 0 || pixel_count > PHOTOWAVE_MAX_PIXELS) return -3;
    
    // Zero out the entire structure
    memset(state, 0, sizeof(PhotowaveState));
    
    // Initialize configuration with defaults
    state->config.scan_mode = PHOTOWAVE_SCAN_LEFT_TO_RIGHT;
    state->config.interp_mode = PHOTOWAVE_INTERP_LINEAR;
    state->config.amplitude = PHOTOWAVE_DEFAULT_AMPLITUDE;
    
    // Initialize audio parameters
    state->sample_rate = sample_rate;
    state->pixel_count = pixel_count;
    state->f_min = sample_rate / (float)pixel_count;
    state->f_max = PHOTOWAVE_MAX_FREQUENCY;
    
    // Initialize playback state
    state->phase = 0.0f;
    state->current_frequency = state->f_min;
    state->target_frequency = state->f_min;
    state->note_active = false;
    state->continuous_mode = true;  // Enable continuous mode by default for photowave
    state->current_note = 0;
    state->current_velocity = 100;  // Default velocity for continuous mode
    
    // Calculate initial phase increment for f_min
    float period_samples = state->sample_rate / state->f_min;
    state->phase_increment = 1.0f / period_samples;
    
    // Initialize statistics
    state->samples_generated = 0;
    state->buffer_underruns = 0;
    
    return 0; // Success
}

void synth_photowave_cleanup(PhotowaveState *state) {
    if (!state) return;
    
    // Zero out the structure
    memset(state, 0, sizeof(PhotowaveState));
}

/* ============================================================================
 * PRIVATE SYNTHESIS FUNCTIONS
 * ========================================================================== */

/**
 * @brief Sample the waveform at a given phase position using linear interpolation
 * 
 * @param image_line Pointer to grayscale image data (uint8_t array)
 * @param pixel_count Number of pixels in the line
 * @param phase Phase position (0.0 to 1.0)
 * @param scan_mode Scanning mode (affects how phase maps to pixel position)
 * @return Interpolated sample value (-1.0 to 1.0)
 */
static float sample_waveform_linear(const uint8_t *image_line, int pixel_count,
                                   float phase, PhotowaveScanMode scan_mode) {
    if (!image_line || pixel_count <= 0) return 0.0f;
    
    // Wrap phase to [0.0, 1.0)
    phase = phase - floorf(phase);
    
    // Map phase to pixel position based on scan mode
    float pixel_pos;
    
    switch (scan_mode) {
        case PHOTOWAVE_SCAN_LEFT_TO_RIGHT:
            // Standard left-to-right scan
            pixel_pos = phase * (float)(pixel_count - 1);
            break;
            
        case PHOTOWAVE_SCAN_RIGHT_TO_LEFT:
            // Reverse scan (right-to-left)
            pixel_pos = (1.0f - phase) * (float)(pixel_count - 1);
            break;
            
        case PHOTOWAVE_SCAN_DUAL:
            // Ping-pong: first half L→R, second half R→L
            if (phase < 0.5f) {
                // First half: L→R
                pixel_pos = (phase * 2.0f) * (float)(pixel_count - 1);
            } else {
                // Second half: R→L
                pixel_pos = ((1.0f - phase) * 2.0f) * (float)(pixel_count - 1);
            }
            break;
            
        default:
            pixel_pos = phase * (float)(pixel_count - 1);
            break;
    }
    
    // Linear interpolation between adjacent pixels
    int pixel_index = (int)pixel_pos;
    float frac = pixel_pos - (float)pixel_index;
    
    // Clamp indices to valid range
    if (pixel_index < 0) pixel_index = 0;
    if (pixel_index >= pixel_count - 1) pixel_index = pixel_count - 2;
    
    // Get adjacent pixel values and normalize to [-1.0, 1.0]
    float sample0 = ((float)image_line[pixel_index] / 127.5f) - 1.0f;
    float sample1 = ((float)image_line[pixel_index + 1] / 127.5f) - 1.0f;
    
    // Linear interpolation
    return sample0 + frac * (sample1 - sample0);
}

/* ============================================================================
 * AUDIO PROCESSING (RT-SAFE)
 * ========================================================================== */

void synth_photowave_process(PhotowaveState *state,
                             float *output_left,
                             float *output_right,
                             int num_frames) {
    static int debug_counter = 0;
    int should_generate;
    int i;
    
    if (!state || !output_left || !output_right) return;
    
    // In continuous mode, generate audio even without note_active
    // In note mode, only generate if note is active
    should_generate = state->continuous_mode || state->note_active;
    
    // Debug log every 1000 calls (~23 times per second at 44.1kHz with 512 buffer)
    if (++debug_counter >= 1000) {
        log_info("PHOTOWAVE_DEBUG", "Process: should_gen=%d (cont=%d, note_act=%d), has_image=%d, pixels=%d",
                 should_generate, state->continuous_mode, state->note_active,
                 (state->image_line != NULL), state->pixel_count);
        debug_counter = 0;
    }
    
    // If no image line or shouldn't generate, output silence
    if (!should_generate || !state->image_line || state->pixel_count <= 0) {
        for (i = 0; i < num_frames; i++) {
            output_left[i] = 0.0f;
            output_right[i] = 0.0f;
        }
        return;
    }
    
    {
        // Get configuration parameters
        const float amplitude = state->config.amplitude;
        const float velocity_scale = (float)state->current_velocity / 127.0f;
        const float final_amplitude = amplitude * velocity_scale;
        const PhotowaveScanMode scan_mode = state->config.scan_mode;
        int i;
        
        // Generate audio samples
        for (i = 0; i < num_frames; i++) {
            float sample;
            
            // Sample the waveform at current phase
            sample = sample_waveform_linear(state->image_line, state->pixel_count,
                                                 state->phase, scan_mode);
            
            // Apply amplitude and velocity
            sample *= final_amplitude;
            
            // Output to both channels (mono for now)
            output_left[i] = sample;
            output_right[i] = sample;
            
            // Advance phase
            state->phase += state->phase_increment;
            
            // Wrap phase to [0.0, 1.0)
            if (state->phase >= 1.0f) {
                state->phase -= 1.0f;
            }
        }
    }
    
    state->samples_generated += num_frames;
}

/* ============================================================================
 * PARAMETER SETTERS (RT-SAFE)
 * ========================================================================== */

void synth_photowave_set_image_line(PhotowaveState *state,
                                   const uint8_t *image_line,
                                   int pixel_count) {
    if (!state) return;
    
    state->image_line = image_line;
    if (pixel_count > 0 && pixel_count <= PHOTOWAVE_MAX_PIXELS) {
        state->pixel_count = pixel_count;
        // Recalculate f_min based on new pixel count
        state->f_min = state->sample_rate / (float)pixel_count;
    }
}

void synth_photowave_set_scan_mode(PhotowaveState *state, PhotowaveScanMode mode) {
    if (!state) return;
    state->config.scan_mode = mode;
}

void synth_photowave_set_interp_mode(PhotowaveState *state, PhotowaveInterpMode mode) {
    if (!state) return;
    state->config.interp_mode = mode;
}

void synth_photowave_set_amplitude(PhotowaveState *state, float amplitude) {
    if (!state) return;
    state->config.amplitude = clamp_float(amplitude, 0.0f, 1.0f);
}

void synth_photowave_set_frequency(PhotowaveState *state, float frequency) {
    if (!state) return;
    
    // Clamp frequency to valid range
    frequency = clamp_float(frequency, state->f_min, state->f_max);
    
    state->target_frequency = frequency;
    state->current_frequency = frequency;
    
    // Calculate phase increment
    float period_samples = state->sample_rate / frequency;
    float period_multiplier = (state->config.scan_mode == PHOTOWAVE_SCAN_DUAL) ? 2.0f : 1.0f;
    state->phase_increment = period_multiplier / period_samples;
}

void synth_photowave_set_continuous_mode(PhotowaveState *state, bool enabled) {
    if (!state) return;
    state->continuous_mode = enabled;
}

/* ============================================================================
 * MIDI CONTROL (RT-SAFE)
 * ========================================================================== */

void synth_photowave_note_on(PhotowaveState *state, uint8_t note, uint8_t velocity) {
    if (!state) return;
    
    log_info("PHOTOWAVE_DEBUG", "Note On BEFORE: note_active=%d, continuous=%d, freq=%.1f Hz",
             state->note_active, state->continuous_mode, state->current_frequency);
    
    state->note_active = true;
    state->current_note = note;
    state->current_velocity = velocity;
    
    // Calculate target frequency from MIDI note
    state->target_frequency = midi_note_to_frequency(note, state->f_min, state->f_max);
    state->current_frequency = state->target_frequency;
    
    // Calculate phase increment
    float period_samples = state->sample_rate / state->current_frequency;
    float period_multiplier = (state->config.scan_mode == PHOTOWAVE_SCAN_DUAL) ? 2.0f : 1.0f;
    state->phase_increment = period_multiplier / period_samples;
    
    // Reset phase to start of waveform
    state->phase = 0.0f;
    
    log_info("PHOTOWAVE_DEBUG", "Note On AFTER: note=%d, vel=%d, freq=%.1f Hz, note_active=%d, has_image=%d",
             note, velocity, state->current_frequency, state->note_active, (state->image_line != NULL));
}

void synth_photowave_note_off(PhotowaveState *state, uint8_t note) {
    if (!state) return;
    
    log_info("PHOTOWAVE_DEBUG", "Note Off: note=%d, current_note=%d, will_deactivate=%d",
             note, state->current_note, (state->current_note == note));
    
    // Only turn off if this is the current note
    if (state->current_note == note) {
        state->note_active = false;
        log_info("PHOTOWAVE_DEBUG", "Note deactivated: note_active=%d", state->note_active);
    }
}

void synth_photowave_control_change(PhotowaveState *state, uint8_t cc_number, uint8_t cc_value) {
    if (!state) return;
    
    switch (cc_number) {
        case 1: // CC1 (Modulation): Scan mode
            if (cc_value < 43) {
                state->config.scan_mode = PHOTOWAVE_SCAN_LEFT_TO_RIGHT;
            } else if (cc_value < 85) {
                state->config.scan_mode = PHOTOWAVE_SCAN_RIGHT_TO_LEFT;
            } else {
                state->config.scan_mode = PHOTOWAVE_SCAN_DUAL;
            }
            break;
            
        case 7: // CC7 (Volume): Amplitude
            state->config.amplitude = (float)cc_value / 127.0f;
            break;
            
        case 74: // CC74 (Brightness): Interpolation mode
            state->config.interp_mode = (cc_value < 64) ? 
                PHOTOWAVE_INTERP_LINEAR : PHOTOWAVE_INTERP_CUBIC;
            break;
            
        default:
            // Ignore unknown CC numbers
            break;
    }
}

/* ============================================================================
 * PARAMETER GETTERS
 * ========================================================================== */

PhotowaveConfig synth_photowave_get_config(const PhotowaveState *state) {
    PhotowaveConfig config = {0};
    if (state) {
        config = state->config;
    }
    return config;
}

float synth_photowave_get_frequency(const PhotowaveState *state) {
    return state ? state->current_frequency : 0.0f;
}

bool synth_photowave_is_note_active(const PhotowaveState *state) {
    return state ? state->note_active : false;
}

/* ============================================================================
 * THREAD INTEGRATION FUNCTIONS
 * ========================================================================== */

void synth_photowave_apply_config(PhotowaveState *state) {
    if (!state) return;
    
    // Apply configuration from g_sp3ctra_config
    state->continuous_mode = (g_sp3ctra_config.photowave_continuous_mode != 0);
    state->config.scan_mode = (PhotowaveScanMode)g_sp3ctra_config.photowave_scan_mode;
    state->config.interp_mode = (PhotowaveInterpMode)g_sp3ctra_config.photowave_interp_mode;
    state->config.amplitude = g_sp3ctra_config.photowave_amplitude;
    
    log_info("PHOTOWAVE", "Configuration applied: continuous_mode=%d, scan_mode=%d, interp_mode=%d, amplitude=%.2f",
             state->continuous_mode, state->config.scan_mode, state->config.interp_mode, state->config.amplitude);
}

void synth_photowave_mode_init(void) {
    // Initialize double buffers
    for (int i = 0; i < 2; i++) {
        pthread_mutex_init(&photowave_audio_buffers[i].mutex, NULL);
        pthread_cond_init(&photowave_audio_buffers[i].cond, NULL);
        
        // Initialize ready state atomically for RT-safe operation
        __atomic_store_n(&photowave_audio_buffers[i].ready, 0, __ATOMIC_SEQ_CST);
        
        // Allocate dynamic audio buffer based on runtime configuration
        if (!photowave_audio_buffers[i].data) {
            photowave_audio_buffers[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
            // Ensure buffer is zeroed
            memset(photowave_audio_buffers[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
        }
    }
    
    // Initialize buffer index atomically
    __atomic_store_n(&photowave_current_buffer_index, 0, __ATOMIC_SEQ_CST);
    
    // Initialize global Photowave state
    int pixel_count = get_cis_pixels_nb();  // Get runtime pixel count
    int result = synth_photowave_init(&g_photowave_state, g_sp3ctra_config.sampling_frequency, pixel_count);
    
    if (result == 0) {
        log_info("PHOTOWAVE", "Initialized: %.1f Hz sample rate, %d pixels, f_min=%.2f Hz",
                 g_sp3ctra_config.sampling_frequency, pixel_count, g_photowave_state.f_min);
        
        // Apply configuration from loaded config file
        synth_photowave_apply_config(&g_photowave_state);
    } else {
        log_error("PHOTOWAVE", "Initialization failed with error code %d", result);
    }
}

void synth_photowave_thread_stop(void) {
    // Signal thread to stop
    photowave_thread_running = 0;
    log_info("PHOTOWAVE", "Thread stop signal sent");
}

void synth_photowave_mode_cleanup(void) {
    // Cleanup global state
    synth_photowave_cleanup(&g_photowave_state);
    
    // Free buffers
    for (int i = 0; i < 2; i++) {
        if (photowave_audio_buffers[i].data) {
            free(photowave_audio_buffers[i].data);
            photowave_audio_buffers[i].data = NULL;
        }
        pthread_mutex_destroy(&photowave_audio_buffers[i].mutex);
        pthread_cond_destroy(&photowave_audio_buffers[i].cond);
    }
    
    log_info("PHOTOWAVE", "Cleanup complete");
}

void *synth_photowave_thread_func(void *arg) {
    (void)arg;  // Unused parameter
    
    log_info("PHOTOWAVE", "Thread started");
    photowave_thread_running = 1;
    
    // Local buffer for stereo generation
    int buffer_size = g_sp3ctra_config.audio_buffer_size;
    float *temp_left = (float *)malloc(buffer_size * sizeof(float));
    float *temp_right = (float *)malloc(buffer_size * sizeof(float));
    
    if (!temp_left || !temp_right) {
        log_error("PHOTOWAVE", "Failed to allocate temporary buffers");
        if (temp_left) free(temp_left);
        if (temp_right) free(temp_right);
        return NULL;
    }
    
    while (photowave_thread_running) {
        // CPU OPTIMIZATION: If mix level is essentially zero, sleep instead of generating buffers
        // This prevents wasting CPU cycles when photowave is not being used
        extern float getSynthPhotowaveMixLevel(void);
        if (getSynthPhotowaveMixLevel() < 0.01f) {
            usleep(10000);  // 10ms sleep - reduce CPU waste
            continue;
        }
        
        // Get current write buffer index
        pthread_mutex_lock(&photowave_buffer_index_mutex);
        int write_index = photowave_current_buffer_index;
        pthread_mutex_unlock(&photowave_buffer_index_mutex);
        
        // Check if buffer is available (not ready = available for writing)
        int buffer_ready = __atomic_load_n(&photowave_audio_buffers[write_index].ready, __ATOMIC_ACQUIRE);
        
        if (buffer_ready == 0) {
            // Buffer is available, generate audio
            synth_photowave_process(&g_photowave_state, temp_left, temp_right, buffer_size);
            
            // Mix stereo to mono for output buffer (or keep stereo if needed)
            // For now, just use left channel
            memcpy(photowave_audio_buffers[write_index].data, temp_left, buffer_size * sizeof(float));
            
            // Mark buffer as ready atomically
            __atomic_store_n(&photowave_audio_buffers[write_index].ready, 1, __ATOMIC_RELEASE);
            
            // Switch to next buffer
            pthread_mutex_lock(&photowave_buffer_index_mutex);
            photowave_current_buffer_index = (write_index == 0) ? 1 : 0;
            pthread_mutex_unlock(&photowave_buffer_index_mutex);
        } else {
            // Buffer not yet consumed, wait a bit
            usleep(100);  // 100 microseconds
        }
    }
    
    // Cleanup
    free(temp_left);
    free(temp_right);
    
    log_info("PHOTOWAVE", "Thread stopped");
    return NULL;
}
