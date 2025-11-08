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
 * @brief Convert MIDI note number to frequency using exponential mapping
 * 
 * Maps MIDI note 0-127 exponentially from f_min to f_max.
 * Formula: f = f_min * (f_max/f_min)^(note/127)
 */
static float midi_note_to_frequency(uint8_t note, float f_min, float f_max) {
    float normalized = (float)note / 127.0f;
    float ratio = f_max / f_min;
    return f_min * powf(ratio, normalized);
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
    
    // Allocate buffers
    state->blur_buffer = (float*)calloc(PHOTOWAVE_MAX_PIXELS, sizeof(float));
    state->temp_buffer = (float*)calloc(PHOTOWAVE_MAX_PIXELS, sizeof(float));
    
    if (!state->blur_buffer || !state->temp_buffer) {
        synth_photowave_cleanup(state);
        return -4; // Allocation failed
    }
    
    // Initialize configuration with defaults
    state->config.scan_mode = PHOTOWAVE_SCAN_LEFT_TO_RIGHT;
    state->config.interp_mode = PHOTOWAVE_INTERP_LINEAR;
    state->config.amplitude = PHOTOWAVE_DEFAULT_AMPLITUDE;
    state->config.blur_radius = 0;
    state->config.blur_amount = 0.0f;
    
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
    state->continuous_mode = true;  // Enable continuous mode by default
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
    
    if (state->blur_buffer) {
        free(state->blur_buffer);
        state->blur_buffer = NULL;
    }
    
    if (state->temp_buffer) {
        free(state->temp_buffer);
        state->temp_buffer = NULL;
    }
    
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

/**
 * @brief Apply spatial blur filter to the waveform
 * 
 * This function applies a simple box blur to smooth the waveform.
 * The blur is computed once per buffer and cached in blur_buffer.
 * 
 * @param state Pointer to PhotowaveState structure
 */
static void apply_blur_filter(PhotowaveState *state) {
    if (!state || !state->image_line || state->config.blur_radius <= 0) return;
    
    const int radius = state->config.blur_radius;
    const int pixel_count = state->pixel_count;
    const uint8_t *src = state->image_line;
    float *temp = state->temp_buffer;
    float *blur = state->blur_buffer;
    
    // Convert uint8_t to float and normalize to [-1.0, 1.0]
    for (int i = 0; i < pixel_count; i++) {
        temp[i] = ((float)src[i] / 127.5f) - 1.0f;
    }
    
    // Apply box blur (simple moving average)
    for (int i = 0; i < pixel_count; i++) {
        float sum = 0.0f;
        int count = 0;
        
        // Sum pixels within radius
        for (int j = -radius; j <= radius; j++) {
            int idx = i + j;
            if (idx >= 0 && idx < pixel_count) {
                sum += temp[idx];
                count++;
            }
        }
        
        // Average
        blur[i] = (count > 0) ? (sum / (float)count) : temp[i];
    }
}

/**
 * @brief Sample from the blurred waveform buffer
 * 
 * @param blur_buffer Pointer to blurred waveform data
 * @param pixel_count Number of pixels
 * @param phase Phase position (0.0 to 1.0)
 * @param scan_mode Scanning mode
 * @return Interpolated sample value (-1.0 to 1.0)
 */
static float sample_blur_buffer(const float *blur_buffer, int pixel_count,
                               float phase, PhotowaveScanMode scan_mode) {
    if (!blur_buffer || pixel_count <= 0) return 0.0f;
    
    // Wrap phase to [0.0, 1.0)
    phase = phase - floorf(phase);
    
    // Map phase to pixel position based on scan mode
    float pixel_pos;
    
    switch (scan_mode) {
        case PHOTOWAVE_SCAN_LEFT_TO_RIGHT:
            pixel_pos = phase * (float)(pixel_count - 1);
            break;
            
        case PHOTOWAVE_SCAN_RIGHT_TO_LEFT:
            pixel_pos = (1.0f - phase) * (float)(pixel_count - 1);
            break;
            
        case PHOTOWAVE_SCAN_DUAL:
            if (phase < 0.5f) {
                pixel_pos = (phase * 2.0f) * (float)(pixel_count - 1);
            } else {
                pixel_pos = ((1.0f - phase) * 2.0f) * (float)(pixel_count - 1);
            }
            break;
            
        default:
            pixel_pos = phase * (float)(pixel_count - 1);
            break;
    }
    
    // Linear interpolation
    int pixel_index = (int)pixel_pos;
    float frac = pixel_pos - (float)pixel_index;
    
    if (pixel_index < 0) pixel_index = 0;
    if (pixel_index >= pixel_count - 1) pixel_index = pixel_count - 2;
    
    float sample0 = blur_buffer[pixel_index];
    float sample1 = blur_buffer[pixel_index + 1];
    
    return sample0 + frac * (sample1 - sample0);
}

/* ============================================================================
 * AUDIO PROCESSING (RT-SAFE)
 * ========================================================================== */

void synth_photowave_process(PhotowaveState *state,
                             float *output_left,
                             float *output_right,
                             int num_frames) {
    if (!state || !output_left || !output_right) return;
    
    // In continuous mode, generate audio even without note_active
    // In note mode, only generate if note is active
    bool should_generate = state->continuous_mode || state->note_active;
    
    // If no image line or shouldn't generate, output silence
    if (!should_generate || !state->image_line || state->pixel_count <= 0) {
        for (int i = 0; i < num_frames; i++) {
            output_left[i] = 0.0f;
            output_right[i] = 0.0f;
        }
        return;
    }
    
    // Apply blur filter if needed (only when blur amount > 0)
    const bool use_blur = (state->config.blur_amount > 0.0f && state->config.blur_radius > 0);
    if (use_blur) {
        apply_blur_filter(state);
    }
    
    // Get configuration parameters
    const float amplitude = state->config.amplitude;
    const float velocity_scale = (float)state->current_velocity / 127.0f;
    const float final_amplitude = amplitude * velocity_scale;
    const PhotowaveScanMode scan_mode = state->config.scan_mode;
    const float blur_amount = state->config.blur_amount;
    
    // Generate audio samples
    for (int i = 0; i < num_frames; i++) {
        // Sample the waveform at current phase
        float sample_dry = sample_waveform_linear(state->image_line, state->pixel_count,
                                                  state->phase, scan_mode);
        
        // Mix with blurred version if blur is enabled
        float sample;
        if (use_blur) {
            float sample_wet = sample_blur_buffer(state->blur_buffer, state->pixel_count,
                                                 state->phase, scan_mode);
            // Linear crossfade between dry and wet
            sample = sample_dry * (1.0f - blur_amount) + sample_wet * blur_amount;
        } else {
            sample = sample_dry;
        }
        
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

void synth_photowave_set_blur(PhotowaveState *state, int radius, float amount) {
    if (!state) return;
    state->config.blur_radius = clamp_int(radius, 0, PHOTOWAVE_MAX_BLUR_RADIUS);
    state->config.blur_amount = clamp_float(amount, 0.0f, 1.0f);
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
}

void synth_photowave_note_off(PhotowaveState *state, uint8_t note) {
    if (!state) return;
    
    // Only turn off if this is the current note
    if (state->current_note == note) {
        state->note_active = false;
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
            
        case 71: // CC71 (Resonance): Blur amount
            state->config.blur_amount = (float)cc_value / 127.0f;
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
    state->config.scan_mode = (PhotowaveScanMode)g_sp3ctra_config.photowave_scan_mode;
    state->config.interp_mode = (PhotowaveInterpMode)g_sp3ctra_config.photowave_interp_mode;
    state->config.amplitude = g_sp3ctra_config.photowave_amplitude;
    state->config.blur_radius = g_sp3ctra_config.photowave_blur_radius;
    state->config.blur_amount = g_sp3ctra_config.photowave_blur_amount;
    
    log_info("PHOTOWAVE", "Configuration applied: scan_mode=%d, interp_mode=%d, amplitude=%.2f, blur_radius=%d, blur_amount=%.2f",
             state->config.scan_mode, state->config.interp_mode, state->config.amplitude,
             state->config.blur_radius, state->config.blur_amount);
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

void synth_photowave_mode_cleanup(void) {
    // Stop thread if running
    photowave_thread_running = 0;
    
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
