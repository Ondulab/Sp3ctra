/**
 * @file synth_photowave.c
 * @brief Photowave synthesis engine implementation with polyphony, ADSR, and LFO
 */

#include "synth_photowave.h"
#include "config_loader.h"  // For g_sp3ctra_config
#include "logger.h"         // For logging
#include "context.h"        // For Context structure
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>         // For usleep

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#define TWO_PI (2.0 * M_PI)

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
 * GLOBAL ADSR/LFO/FILTER PARAMETERS
 * ========================================================================== */

// Volume ADSR defaults
static float G_PHOTOWAVE_VOLUME_ATTACK_S = 0.01f;
static float G_PHOTOWAVE_VOLUME_DECAY_S = 0.1f;
static float G_PHOTOWAVE_VOLUME_SUSTAIN = 0.8f;
static float G_PHOTOWAVE_VOLUME_RELEASE_S = 0.2f;

// Filter ADSR defaults
static float G_PHOTOWAVE_FILTER_ATTACK_S = 0.02f;
static float G_PHOTOWAVE_FILTER_DECAY_S = 0.2f;
static float G_PHOTOWAVE_FILTER_SUSTAIN = 0.3f;
static float G_PHOTOWAVE_FILTER_RELEASE_S = 0.3f;

// LFO defaults
static float G_PHOTOWAVE_LFO_RATE_HZ = 5.0f;
static float G_PHOTOWAVE_LFO_DEPTH_SEMITONES = 0.25f;

// Filter defaults
static float G_PHOTOWAVE_FILTER_CUTOFF_HZ = 8000.0f;
static float G_PHOTOWAVE_FILTER_ENV_DEPTH = -6000.0f;

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
 * @brief Convert MIDI note number to frequency using standard MIDI tuning
 * 
 * Uses equal temperament tuning with A4 (note 69) = 440 Hz.
 * Formula: f = 440 × 2^((note - 69) / 12)
 * 
 * @param note MIDI note number (0-127)
 * @param f_min Minimum frequency limit (clamps result if below)
 * @param f_max Maximum frequency limit (clamps result if exceeded)
 * @return Frequency in Hz, clamped to [f_min, f_max]
 */
static float midi_note_to_frequency(uint8_t note, float f_min, float f_max) {
    const float A4_FREQ = 440.0f;
    const int A4_NOTE = 69;
    
    float semitones_from_a4 = (float)(note - A4_NOTE);
    float frequency = A4_FREQ * powf(2.0f, semitones_from_a4 / 12.0f);
    
    if (frequency < f_min) frequency = f_min;
    if (frequency > f_max) frequency = f_max;
    
    return frequency;
}

/* ============================================================================
 * ADSR ENVELOPE IMPLEMENTATION
 * ========================================================================== */

static void adsr_init_envelope(AdsrEnvelope *env, float attack_s, float decay_s,
                               float sustain_level, float release_s,
                               float sample_rate) {
    env->attack_s = attack_s;
    env->decay_s = decay_s;
    env->sustain_level = sustain_level;
    env->release_s = release_s;
    env->attack_time_samples = (attack_s > 0.0f) ? fmaxf(1.0f, attack_s * sample_rate) : 0.0f;
    env->decay_time_samples = (decay_s > 0.0f) ? fmaxf(1.0f, decay_s * sample_rate) : 0.0f;
    env->release_time_samples = (release_s > 0.0f) ? fmaxf(1.0f, release_s * sample_rate) : 0.0f;
    env->attack_increment = (env->attack_time_samples > 0.0f) ? (1.0f / env->attack_time_samples) : 1.0f;
    env->decay_decrement = (env->decay_time_samples > 0.0f && (1.0f - sustain_level) > 0.0f) ?
                          ((1.0f - sustain_level) / env->decay_time_samples) : (1.0f - sustain_level);
    env->state = ADSR_STATE_IDLE;
    env->current_output = 0.0f;
    env->current_samples = 0;
}

static void adsr_trigger_attack(AdsrEnvelope *env) {
    env->state = ADSR_STATE_ATTACK;
    env->current_samples = 0;
    env->current_output = 0.0f;
    
    if (env->attack_time_samples > 0.0f) {
        env->attack_increment = 1.0f / env->attack_time_samples;
    } else {
        env->current_output = 1.0f;
        env->attack_increment = 0.0f;
        if (env->sustain_level < 1.0f && env->decay_time_samples > 0.0f) {
            env->state = ADSR_STATE_DECAY;
            env->decay_decrement = (1.0f - env->sustain_level) / env->decay_time_samples;
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
            (env->attack_time_samples > 0.0f && env->current_samples >= env->attack_time_samples)) {
            env->current_output = 1.0f;
            env->state = ADSR_STATE_DECAY;
            env->current_samples = 0;
            if (env->decay_time_samples > 0.0f) {
                env->decay_decrement = (1.0f - env->sustain_level) / env->decay_time_samples;
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
            (env->decay_time_samples > 0.0f && env->current_samples >= env->decay_time_samples)) {
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
            (env->release_time_samples > 0.0f && env->current_samples >= env->release_time_samples)) {
            env->current_output = 0.0f;
            env->state = ADSR_STATE_IDLE;
        }
        break;
    }
    if (env->current_output > 1.0f) env->current_output = 1.0f;
    if (env->current_output < 0.0f) env->current_output = 0.0f;
    return env->current_output;
}

static void adsr_update_settings_and_recalculate_rates(AdsrEnvelope *env, float attack_s,
                                                       float decay_s, float sustain_level,
                                                       float release_s, float sample_rate) {
    env->attack_s = attack_s;
    env->decay_s = decay_s;
    env->sustain_level = sustain_level;
    env->release_s = release_s;
    
    env->attack_time_samples = (attack_s > 0.0f) ? fmaxf(1.0f, attack_s * sample_rate) : 0.0f;
    env->decay_time_samples = (decay_s > 0.0f) ? fmaxf(1.0f, decay_s * sample_rate) : 0.0f;
    env->release_time_samples = (release_s > 0.0f) ? fmaxf(1.0f, release_s * sample_rate) : 0.0f;
    
    env->attack_increment = (env->attack_time_samples > 0.0f) ? (1.0f / env->attack_time_samples) : 1.0f;
    
    if (env->state == ADSR_STATE_DECAY && env->current_output > env->sustain_level) {
        float time_remaining = env->decay_time_samples - env->current_samples;
        if (time_remaining > 0.0f) {
            env->decay_decrement = (env->current_output - env->sustain_level) / time_remaining;
        } else {
            env->decay_decrement = (env->current_output - env->sustain_level);
        }
    } else {
        env->decay_decrement = (env->decay_time_samples > 0.0f && (1.0f - env->sustain_level) > 0.00001f) ?
                              ((1.0f - env->sustain_level) / env->decay_time_samples) : (1.0f - env->sustain_level);
        if (env->decay_decrement < 0.0f) env->decay_decrement = 0.0f;
    }
    
    if (env->state == ADSR_STATE_RELEASE && env->current_output > 0.0f) {
        float time_remaining = env->release_time_samples - env->current_samples;
        if (time_remaining > 0.0f) {
            env->release_decrement = env->current_output / time_remaining;
        } else {
            env->release_decrement = env->current_output;
        }
    } else {
        env->release_decrement = (env->release_time_samples > 0.0f && env->current_output > 0.00001f) ?
                                (env->current_output / env->release_time_samples) : env->current_output;
        if (env->release_decrement < 0.0f) env->release_decrement = 0.0f;
    }
}

/* ============================================================================
 * LFO IMPLEMENTATION
 * ========================================================================== */

static void lfo_init(LfoState *lfo, float rate_hz, float depth_semitones, float sample_rate) {
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

/* ============================================================================
 * LOWPASS FILTER IMPLEMENTATION
 * ========================================================================== */

static void lowpass_init(PhotowaveLowpassFilter *filter, float sample_rate) {
    filter->base_cutoff_hz = G_PHOTOWAVE_FILTER_CUTOFF_HZ;
    filter->filter_env_depth = G_PHOTOWAVE_FILTER_ENV_DEPTH;
    filter->prev_output = 0.0f;
    
    float rc = 1.0f / (TWO_PI * filter->base_cutoff_hz);
    float dt = 1.0f / sample_rate;
    filter->alpha = dt / (rc + dt);
}

static float lowpass_process(PhotowaveLowpassFilter *filter, float input, 
                            float modulated_cutoff, float sample_rate) {
    float rc = 1.0f / (TWO_PI * modulated_cutoff);
    float dt = 1.0f / sample_rate;
    float alpha = dt / (rc + dt);
    
    filter->prev_output = alpha * input + (1.0f - alpha) * filter->prev_output;
    return filter->prev_output;
}

/* ============================================================================
 * WAVEFORM SAMPLING
 * ========================================================================== */

static float sample_waveform_linear(const uint8_t *image_line, int pixel_count,
                                   float phase, PhotowaveScanMode scan_mode) {
    if (!image_line || pixel_count <= 0) return 0.0f;
    
    phase = phase - floorf(phase);
    
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
    
    int pixel_index = (int)pixel_pos;
    float frac = pixel_pos - (float)pixel_index;
    
    if (pixel_index < 0) pixel_index = 0;
    if (pixel_index >= pixel_count - 1) pixel_index = pixel_count - 2;
    
    float sample0 = ((float)image_line[pixel_index] / 127.5f) - 1.0f;
    float sample1 = ((float)image_line[pixel_index + 1] / 127.5f) - 1.0f;
    
    return sample0 + frac * (sample1 - sample0);
}

/**
 * @brief Sample waveform using cubic (Catmull-Rom) interpolation
 * 
 * Provides smoother interpolation than linear, reducing aliasing artifacts.
 * Uses Catmull-Rom spline which passes through control points.
 * 
 * @param image_line Pointer to grayscale image line data
 * @param pixel_count Number of pixels in the line
 * @param phase Current phase position (0.0 to 1.0)
 * @param scan_mode Scanning direction mode
 * @return Interpolated sample value (-1.0 to 1.0)
 */
static float sample_waveform_cubic(const uint8_t *image_line, int pixel_count,
                                  float phase, PhotowaveScanMode scan_mode) {
    if (!image_line || pixel_count <= 0) return 0.0f;
    
    phase = phase - floorf(phase);
    
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
    
    int pixel_index = (int)pixel_pos;
    float frac = pixel_pos - (float)pixel_index;
    
    // Clamp indices for boundary conditions
    if (pixel_index < 0) pixel_index = 0;
    if (pixel_index >= pixel_count - 1) pixel_index = pixel_count - 2;
    
    // Get 4 samples for cubic interpolation (with boundary handling)
    int idx_m1 = (pixel_index > 0) ? pixel_index - 1 : pixel_index;
    int idx_0 = pixel_index;
    int idx_1 = pixel_index + 1;
    int idx_2 = (pixel_index + 2 < pixel_count) ? pixel_index + 2 : pixel_index + 1;
    
    // Convert to normalized float samples [-1.0, 1.0]
    float y_m1 = ((float)image_line[idx_m1] / 127.5f) - 1.0f;
    float y_0 = ((float)image_line[idx_0] / 127.5f) - 1.0f;
    float y_1 = ((float)image_line[idx_1] / 127.5f) - 1.0f;
    float y_2 = ((float)image_line[idx_2] / 127.5f) - 1.0f;
    
    // Catmull-Rom cubic interpolation
    // P(t) = 0.5 * [(2*P1) + (-P0 + P2)*t + (2*P0 - 5*P1 + 4*P2 - P3)*t^2 + (-P0 + 3*P1 - 3*P2 + P3)*t^3]
    float t = frac;
    float t2 = t * t;
    float t3 = t2 * t;
    
    float a0 = -0.5f * y_m1 + 1.5f * y_0 - 1.5f * y_1 + 0.5f * y_2;
    float a1 = y_m1 - 2.5f * y_0 + 2.0f * y_1 - 0.5f * y_2;
    float a2 = -0.5f * y_m1 + 0.5f * y_1;
    float a3 = y_0;
    
    return a0 * t3 + a1 * t2 + a2 * t + a3;
}

/* ============================================================================
 * INITIALIZATION & CLEANUP
 * ========================================================================== */

int synth_photowave_init(PhotowaveState *state, float sample_rate, int pixel_count) {
    int i;
    
    if (!state) return -1;
    if (sample_rate <= 0.0f) return -2;
    if (pixel_count <= 0 || pixel_count > PHOTOWAVE_MAX_PIXELS) return -3;
    
    memset(state, 0, sizeof(PhotowaveState));
    
    state->config.scan_mode = PHOTOWAVE_SCAN_LEFT_TO_RIGHT;
    state->config.interp_mode = PHOTOWAVE_INTERP_LINEAR;
    state->config.amplitude = PHOTOWAVE_DEFAULT_AMPLITUDE;
    
    state->sample_rate = sample_rate;
    state->pixel_count = pixel_count;
    state->f_min = sample_rate / (float)pixel_count;
    state->f_max = PHOTOWAVE_MAX_FREQUENCY;
    
    state->current_trigger_order = 0;
    
    lfo_init(&state->global_vibrato_lfo, G_PHOTOWAVE_LFO_RATE_HZ, 
             G_PHOTOWAVE_LFO_DEPTH_SEMITONES, sample_rate);
    
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        state->voices[i].phase = 0.0f;
        state->voices[i].frequency = state->f_min;
        state->voices[i].midi_note = 0;
        state->voices[i].velocity = 100;
        state->voices[i].active = false;
        state->voices[i].trigger_order = 0;
        
        adsr_init_envelope(&state->voices[i].volume_adsr, G_PHOTOWAVE_VOLUME_ATTACK_S,
                          G_PHOTOWAVE_VOLUME_DECAY_S, G_PHOTOWAVE_VOLUME_SUSTAIN,
                          G_PHOTOWAVE_VOLUME_RELEASE_S, sample_rate);
        adsr_init_envelope(&state->voices[i].filter_adsr, G_PHOTOWAVE_FILTER_ATTACK_S,
                          G_PHOTOWAVE_FILTER_DECAY_S, G_PHOTOWAVE_FILTER_SUSTAIN,
                          G_PHOTOWAVE_FILTER_RELEASE_S, sample_rate);
        
        lowpass_init(&state->voices[i].lowpass, sample_rate);
    }
    
    state->samples_generated = 0;
    state->buffer_underruns = 0;
    
    return 0;
}

void synth_photowave_cleanup(PhotowaveState *state) {
    if (!state) return;
    memset(state, 0, sizeof(PhotowaveState));
}

/* ============================================================================
 * AUDIO PROCESSING (RT-SAFE)
 * ========================================================================== */

void synth_photowave_process(PhotowaveState *state,
                             float *output_left,
                             float *output_right,
                             int num_frames) {
    int i, v;
    
    if (!state || !output_left || !output_right) return;
    
    if (!state->image_line || state->pixel_count <= 0) {
        for (i = 0; i < num_frames; i++) {
            output_left[i] = 0.0f;
            output_right[i] = 0.0f;
        }
        return;
    }
    
    for (i = 0; i < num_frames; i++) {
        float master_sum = 0.0f;
        int active_voices = 0;
        float lfo_output = lfo_process(&state->global_vibrato_lfo);
        
        for (v = 0; v < NUM_PHOTOWAVE_VOICES; v++) {
            PhotowaveVoice *voice = &state->voices[v];
            
            float vol_adsr = adsr_get_output(&voice->volume_adsr);
            float filt_adsr = adsr_get_output(&voice->filter_adsr);
            
            if (vol_adsr < MIN_AUDIBLE_AMPLITUDE && voice->volume_adsr.state == ADSR_STATE_IDLE) {
                voice->active = false;
                continue;
            }
            
            active_voices++;
            
            float freq_ratio = powf(2.0f, (lfo_output * state->global_vibrato_lfo.depth_semitones) / 12.0f);
            float modulated_freq = voice->frequency * freq_ratio;
            
            float base_cutoff = voice->lowpass.base_cutoff_hz;
            float modulated_cutoff = base_cutoff + filt_adsr * voice->lowpass.filter_env_depth;
            modulated_cutoff = clamp_float(modulated_cutoff, 20.0f, state->sample_rate / 2.0f);
            
            // Select interpolation method based on configuration
            float raw_sample;
            if (state->config.interp_mode == PHOTOWAVE_INTERP_CUBIC) {
                raw_sample = sample_waveform_cubic(state->image_line, state->pixel_count,
                                                   voice->phase, state->config.scan_mode);
            } else {
                raw_sample = sample_waveform_linear(state->image_line, state->pixel_count,
                                                   voice->phase, state->config.scan_mode);
            }
            
            float filtered = lowpass_process(&voice->lowpass, raw_sample, modulated_cutoff, state->sample_rate);
            
            float velocity_scale = voice->velocity / 127.0f;
            float final_sample = filtered * vol_adsr * velocity_scale;
            
            master_sum += final_sample;
            
            float phase_incr = modulated_freq / state->sample_rate;
            if (state->config.scan_mode == PHOTOWAVE_SCAN_DUAL) {
                phase_incr *= 2.0f;
            }
            voice->phase += phase_incr;
            if (voice->phase >= 1.0f) voice->phase -= 1.0f;
        }
        
        // Normalize by number of active voices to prevent clipping
        if (active_voices > 1) {
            master_sum /= sqrtf((float)active_voices);  // Use sqrt for better perceived loudness
        }
        
        master_sum *= state->config.amplitude;
        master_sum = clamp_float(master_sum, -1.0f, 1.0f);
        
        output_left[i] = master_sum;
        output_right[i] = master_sum;
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

/* ============================================================================
 * ADSR PARAMETER SETTERS
 * ========================================================================== */

void synth_photowave_set_volume_adsr_attack(float attack_s) {
    int i;
    if (attack_s < 0.0f) attack_s = 0.0f;
    G_PHOTOWAVE_VOLUME_ATTACK_S = attack_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].volume_adsr,
            G_PHOTOWAVE_VOLUME_ATTACK_S, G_PHOTOWAVE_VOLUME_DECAY_S,
            G_PHOTOWAVE_VOLUME_SUSTAIN, G_PHOTOWAVE_VOLUME_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_volume_adsr_decay(float decay_s) {
    int i;
    if (decay_s < 0.0f) decay_s = 0.0f;
    G_PHOTOWAVE_VOLUME_DECAY_S = decay_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].volume_adsr,
            G_PHOTOWAVE_VOLUME_ATTACK_S, G_PHOTOWAVE_VOLUME_DECAY_S,
            G_PHOTOWAVE_VOLUME_SUSTAIN, G_PHOTOWAVE_VOLUME_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_volume_adsr_sustain(float sustain_level) {
    int i;
    if (sustain_level < 0.0f) sustain_level = 0.0f;
    if (sustain_level > 1.0f) sustain_level = 1.0f;
    G_PHOTOWAVE_VOLUME_SUSTAIN = sustain_level;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].volume_adsr,
            G_PHOTOWAVE_VOLUME_ATTACK_S, G_PHOTOWAVE_VOLUME_DECAY_S,
            G_PHOTOWAVE_VOLUME_SUSTAIN, G_PHOTOWAVE_VOLUME_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_volume_adsr_release(float release_s) {
    int i;
    if (release_s < 0.0f) release_s = 0.0f;
    G_PHOTOWAVE_VOLUME_RELEASE_S = release_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].volume_adsr,
            G_PHOTOWAVE_VOLUME_ATTACK_S, G_PHOTOWAVE_VOLUME_DECAY_S,
            G_PHOTOWAVE_VOLUME_SUSTAIN, G_PHOTOWAVE_VOLUME_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_filter_adsr_attack(float attack_s) {
    int i;
    if (attack_s < 0.0f) attack_s = 0.0f;
    G_PHOTOWAVE_FILTER_ATTACK_S = attack_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].filter_adsr,
            G_PHOTOWAVE_FILTER_ATTACK_S, G_PHOTOWAVE_FILTER_DECAY_S,
            G_PHOTOWAVE_FILTER_SUSTAIN, G_PHOTOWAVE_FILTER_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_filter_adsr_decay(float decay_s) {
    int i;
    if (decay_s < 0.0f) decay_s = 0.0f;
    G_PHOTOWAVE_FILTER_DECAY_S = decay_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].filter_adsr,
            G_PHOTOWAVE_FILTER_ATTACK_S, G_PHOTOWAVE_FILTER_DECAY_S,
            G_PHOTOWAVE_FILTER_SUSTAIN, G_PHOTOWAVE_FILTER_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_filter_adsr_sustain(float sustain_level) {
    int i;
    if (sustain_level < 0.0f) sustain_level = 0.0f;
    if (sustain_level > 1.0f) sustain_level = 1.0f;
    G_PHOTOWAVE_FILTER_SUSTAIN = sustain_level;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].filter_adsr,
            G_PHOTOWAVE_FILTER_ATTACK_S, G_PHOTOWAVE_FILTER_DECAY_S,
            G_PHOTOWAVE_FILTER_SUSTAIN, G_PHOTOWAVE_FILTER_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

void synth_photowave_set_filter_adsr_release(float release_s) {
    int i;
    if (release_s < 0.0f) release_s = 0.0f;
    G_PHOTOWAVE_FILTER_RELEASE_S = release_s;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        adsr_update_settings_and_recalculate_rates(&g_photowave_state.voices[i].filter_adsr,
            G_PHOTOWAVE_FILTER_ATTACK_S, G_PHOTOWAVE_FILTER_DECAY_S,
            G_PHOTOWAVE_FILTER_SUSTAIN, G_PHOTOWAVE_FILTER_RELEASE_S,
            g_photowave_state.sample_rate);
    }
}

/* ============================================================================
 * LFO PARAMETER SETTERS
 * ========================================================================== */

void synth_photowave_set_vibrato_rate(float rate_hz) {
    if (rate_hz < 0.0f) rate_hz = 0.0f;
    g_photowave_state.global_vibrato_lfo.rate_hz = rate_hz;
    g_photowave_state.global_vibrato_lfo.phase_increment = 
        TWO_PI * rate_hz / g_photowave_state.sample_rate;
}

void synth_photowave_set_vibrato_depth(float depth_semitones) {
    g_photowave_state.global_vibrato_lfo.depth_semitones = depth_semitones;
}

/* ============================================================================
 * FILTER PARAMETER SETTERS
 * ========================================================================== */

void synth_photowave_set_filter_cutoff(float cutoff_hz) {
    int i;
    if (cutoff_hz < 20.0f) cutoff_hz = 20.0f;
    if (cutoff_hz > g_photowave_state.sample_rate / 2.0f) {
        cutoff_hz = g_photowave_state.sample_rate / 2.0f;
    }
    G_PHOTOWAVE_FILTER_CUTOFF_HZ = cutoff_hz;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        g_photowave_state.voices[i].lowpass.base_cutoff_hz = cutoff_hz;
    }
}

void synth_photowave_set_filter_env_depth(float depth_hz) {
    int i;
    G_PHOTOWAVE_FILTER_ENV_DEPTH = depth_hz;
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        g_photowave_state.voices[i].lowpass.filter_env_depth = depth_hz;
    }
}

/* ============================================================================
 * MIDI CONTROL (RT-SAFE)
 * ========================================================================== */

void synth_photowave_note_on(PhotowaveState *state, uint8_t note, uint8_t velocity) {
    int i, voice_idx;
    unsigned long long oldest_order;
    float quietest_env;
    PhotowaveVoice *voice;
    
    if (!state) return;
    
    // MIDI protocol: Note On with velocity=0 is actually a Note Off
    if (velocity == 0) {
        synth_photowave_note_off(state, note);
        return;
    }
    
    state->current_trigger_order++;
    voice_idx = -1;
    
    // Priority 1: Find IDLE voice
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        if (state->voices[i].volume_adsr.state == ADSR_STATE_IDLE) {
            voice_idx = i;
            break;
        }
    }
    
    // Priority 2: Steal oldest active (non-release) voice
    if (voice_idx == -1) {
        oldest_order = state->current_trigger_order + 1;
        for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
            if (state->voices[i].volume_adsr.state != ADSR_STATE_RELEASE &&
                state->voices[i].volume_adsr.state != ADSR_STATE_IDLE) {
                if (state->voices[i].trigger_order < oldest_order) {
                    oldest_order = state->voices[i].trigger_order;
                    voice_idx = i;
                }
            }
        }
    }
    
    // Priority 3: Steal quietest release voice
    if (voice_idx == -1) {
        quietest_env = 2.0f;
        for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
            if (state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
                if (state->voices[i].volume_adsr.current_output < quietest_env) {
                    quietest_env = state->voices[i].volume_adsr.current_output;
                    voice_idx = i;
                }
            }
        }
    }
    
    // Fallback: steal voice 0
    if (voice_idx == -1) voice_idx = 0;
    
    voice = &state->voices[voice_idx];
    
    // Initialize voice
    voice->midi_note = note;
    voice->velocity = velocity;
    voice->frequency = midi_note_to_frequency(note, state->f_min, state->f_max);
    voice->phase = 0.0f;
    voice->active = true;
    voice->trigger_order = state->current_trigger_order;
    
    // Initialize ADSR with current global parameters
    adsr_init_envelope(&voice->volume_adsr, G_PHOTOWAVE_VOLUME_ATTACK_S,
                      G_PHOTOWAVE_VOLUME_DECAY_S, G_PHOTOWAVE_VOLUME_SUSTAIN,
                      G_PHOTOWAVE_VOLUME_RELEASE_S, state->sample_rate);
    adsr_init_envelope(&voice->filter_adsr, G_PHOTOWAVE_FILTER_ATTACK_S,
                      G_PHOTOWAVE_FILTER_DECAY_S, G_PHOTOWAVE_FILTER_SUSTAIN,
                      G_PHOTOWAVE_FILTER_RELEASE_S, state->sample_rate);
    
    // Trigger attacks
    adsr_trigger_attack(&voice->volume_adsr);
    adsr_trigger_attack(&voice->filter_adsr);
    
    // Reset filter state
    voice->lowpass.prev_output = 0.0f;
    
    log_debug("PHOTOWAVE", "Note On: voice=%d, note=%d, vel=%d, freq=%.1f Hz",
             voice_idx, note, velocity, voice->frequency);
}

void synth_photowave_note_off(PhotowaveState *state, uint8_t note) {
    int i;
    
    if (!state) return;
    
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].active &&
            state->voices[i].volume_adsr.state != ADSR_STATE_IDLE &&
            state->voices[i].volume_adsr.state != ADSR_STATE_RELEASE) {
            
            adsr_trigger_release(&state->voices[i].volume_adsr);
            adsr_trigger_release(&state->voices[i].filter_adsr);
            
            log_debug("PHOTOWAVE", "Note Off: voice=%d, note=%d", i, note);
        }
    }
}

void synth_photowave_control_change(PhotowaveState *state, uint8_t cc_number, uint8_t cc_value) {
    if (!state) return;
    
    switch (cc_number) {
        case 1: // CC1 (Modulation): Scan mode
            if (cc_value < 43) {
                state->config.scan_mode = PHOTOWAVE_SCAN_LEFT_TO_RIGHT;
                log_info("PHOTOWAVE", "Scan mode: L→R (CC1=%d, range 0-42)", cc_value);
            } else if (cc_value < 85) {
                state->config.scan_mode = PHOTOWAVE_SCAN_RIGHT_TO_LEFT;
                log_info("PHOTOWAVE", "Scan mode: R→L (CC1=%d, range 43-84)", cc_value);
            } else {
                state->config.scan_mode = PHOTOWAVE_SCAN_DUAL;
                log_info("PHOTOWAVE", "Scan mode: Dual (CC1=%d, range 85-127)", cc_value);
            }
            break;
            
        case 7: // CC7 (Volume): Amplitude
            state->config.amplitude = (float)cc_value / 127.0f;
            break;
            
        case 74: // CC74 (Brightness): Interpolation mode
            state->config.interp_mode = (cc_value < 64) ? 
                PHOTOWAVE_INTERP_LINEAR : PHOTOWAVE_INTERP_CUBIC;
            log_info("PHOTOWAVE", "Interpolation: %s (CC74=%d)", 
                     (cc_value < 64) ? "Linear" : "Cubic", cc_value);
            break;
            
        default:
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

bool synth_photowave_is_note_active(const PhotowaveState *state) {
    int i;
    if (!state) return false;
    
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        if (state->voices[i].active && 
            state->voices[i].volume_adsr.state != ADSR_STATE_IDLE) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * THREAD INTEGRATION FUNCTIONS
 * ========================================================================== */

void synth_photowave_apply_config(PhotowaveState *state) {
    if (!state) return;
    
    state->config.scan_mode = (PhotowaveScanMode)g_sp3ctra_config.photowave_scan_mode;
    state->config.interp_mode = (PhotowaveInterpMode)g_sp3ctra_config.photowave_interp_mode;
    state->config.amplitude = g_sp3ctra_config.photowave_amplitude;
    
    log_info("PHOTOWAVE", "Configuration applied: scan_mode=%d, interp_mode=%d, amplitude=%.2f",
             state->config.scan_mode, state->config.interp_mode, state->config.amplitude);
}

void synth_photowave_mode_init(void) {
    int i, pixel_count, result;
    
    log_info("PHOTOWAVE", "Initializing polyphonic photowave synthesis mode");
    
    for (i = 0; i < 2; i++) {
        pthread_mutex_init(&photowave_audio_buffers[i].mutex, NULL);
        pthread_cond_init(&photowave_audio_buffers[i].cond, NULL);
        
        __atomic_store_n(&photowave_audio_buffers[i].ready, 0, __ATOMIC_SEQ_CST);
        
        if (!photowave_audio_buffers[i].data) {
            photowave_audio_buffers[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
            memset(photowave_audio_buffers[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
        }
    }
    
    __atomic_store_n(&photowave_current_buffer_index, 0, __ATOMIC_SEQ_CST);
    
    pixel_count = get_cis_pixels_nb();
    result = synth_photowave_init(&g_photowave_state, g_sp3ctra_config.sampling_frequency, pixel_count);
    
    if (result == 0) {
        log_info("PHOTOWAVE", "Initialized: %.1f Hz sample rate, %d pixels, %d voices, f_min=%.2f Hz",
                 g_sp3ctra_config.sampling_frequency, pixel_count, NUM_PHOTOWAVE_VOICES, 
                 g_photowave_state.f_min);
        
        synth_photowave_apply_config(&g_photowave_state);
    } else {
        log_error("PHOTOWAVE", "Initialization failed with error code %d", result);
    }
}

void synth_photowave_thread_stop(void) {
    photowave_thread_running = 0;
    log_info("PHOTOWAVE", "Thread stop signal sent");
}

void synth_photowave_mode_cleanup(void) {
    int i;
    
    synth_photowave_cleanup(&g_photowave_state);
    
    for (i = 0; i < 2; i++) {
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
    int buffer_size, write_index;
    float *temp_left, *temp_right;
    extern float getSynthPhotowaveMixLevel(void);
    
    (void)arg;
    
    log_info("PHOTOWAVE", "Thread started");
    photowave_thread_running = 1;
    
    buffer_size = g_sp3ctra_config.audio_buffer_size;
    temp_left = (float *)malloc(buffer_size * sizeof(float));
    temp_right = (float *)malloc(buffer_size * sizeof(float));
    
    if (!temp_left || !temp_right) {
        log_error("PHOTOWAVE", "Failed to allocate temporary buffers");
        if (temp_left) free(temp_left);
        if (temp_right) free(temp_right);
        return NULL;
    }
    
    while (photowave_thread_running) {
        if (getSynthPhotowaveMixLevel() < 0.01f) {
            usleep(10000);
            continue;
        }
        
        pthread_mutex_lock(&photowave_buffer_index_mutex);
        write_index = photowave_current_buffer_index;
        pthread_mutex_unlock(&photowave_buffer_index_mutex);
        
        // RT-SAFE: Wait for buffer to be consumed with timeout and exponential backoff
        int wait_iterations = 0;
        const int MAX_WAIT_ITERATIONS = 100; // ~10ms max wait
        
        while (__atomic_load_n(&photowave_audio_buffers[write_index].ready, __ATOMIC_ACQUIRE) == 1 && 
               photowave_thread_running && wait_iterations < MAX_WAIT_ITERATIONS) {
            // Exponential backoff: start with short sleeps, increase if needed
            int sleep_us = (wait_iterations < 10) ? 10 : 
                           (wait_iterations < 50) ? 50 : 100;
            struct timespec sleep_time = {0, sleep_us * 1000}; // Convert µs to ns
            nanosleep(&sleep_time, NULL);
            wait_iterations++;
        }
        
        if (!photowave_thread_running) {
            break;
        }
        
        // If timeout, log warning but continue (graceful degradation)
        if (wait_iterations >= MAX_WAIT_ITERATIONS) {
            static int timeout_counter = 0;
            if (++timeout_counter % 100 == 0) {
                log_warning("PHOTOWAVE", "Buffer wait timeout (callback too slow)");
            }
        }
        
        // Process audio
        synth_photowave_process(&g_photowave_state, temp_left, temp_right, buffer_size);
        
        memcpy(photowave_audio_buffers[write_index].data, temp_left, buffer_size * sizeof(float));
        
        __atomic_store_n(&photowave_audio_buffers[write_index].ready, 1, __ATOMIC_RELEASE);
        
        pthread_mutex_lock(&photowave_buffer_index_mutex);
        photowave_current_buffer_index = (write_index == 0) ? 1 : 0;
        pthread_mutex_unlock(&photowave_buffer_index_mutex);
    }
    
    free(temp_left);
    free(temp_right);
    
    log_info("PHOTOWAVE", "Thread stopped");
    return NULL;
}
