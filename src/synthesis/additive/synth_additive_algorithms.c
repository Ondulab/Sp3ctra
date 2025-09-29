/*
 * synth_additive_algorithms.c
 *
 * Centralized algorithms for additive synthesis
 * Contains core processing algorithms used by both threading and sequential modes
 *
 * Author: zhonx
 * Created: 21 sep. 2025
 */

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_algorithms.h"
#include "synth_additive_math.h"
#include "wave_generation.h"
#include "../../core/context.h"
#include "../../config/config_debug.h"
#include "../../config/config_loader.h"
#include "../../utils/image_debug.h"
#include <stdio.h>
#include <math.h>


/* Function implementations --------------------------------------------------*/

/**
 * @brief Process image preprocessing (averaging and color inversion)
 * @param imageData Input image data
 * @param imageBuffer_q31 Output processed buffer
 * @param start_note Starting note index
 * @param end_note Ending note index (exclusive)
 * @retval None
 */
void process_image_preprocessing(int32_t *imageData, int32_t *imageBuffer_q31, 
                                int start_note, int end_note) {
    int32_t idx, acc;
    
    // Calculate averages for each note
    for (idx = start_note; idx < end_note; idx++) {
        int local_note_idx = idx - start_note;
        imageBuffer_q31[local_note_idx] = 0;
        
        for (acc = 0; acc < g_sp3ctra_config.pixels_per_note; acc++) {
            imageBuffer_q31[local_note_idx] += (imageData[idx * g_sp3ctra_config.pixels_per_note + acc]);
        }
        // Average the accumulated values
        imageBuffer_q31[local_note_idx] /= g_sp3ctra_config.pixels_per_note;

        // Apply optional color inversion based on runtime config
        if (g_sp3ctra_config.invert_intensity) {
            imageBuffer_q31[local_note_idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[local_note_idx];
        }
        // Clamp to valid range
        if (imageBuffer_q31[local_note_idx] < 0)
            imageBuffer_q31[local_note_idx] = 0;
        if (imageBuffer_q31[local_note_idx] > VOLUME_AMP_RESOLUTION)
            imageBuffer_q31[local_note_idx] = VOLUME_AMP_RESOLUTION;
    }
    
    // Bug correction - only for the range that processes note 0
    if (start_note == 0) {
        imageBuffer_q31[0] = 0;
    }
}

/**
 * @brief Apply GAP_LIMITER volume ramp for a single note
 * @param note Note index
 * @param target_volume Target volume for the note
 * @param volumeBuffer Output volume buffer for audio samples
 * @retval None
 */
void apply_gap_limiter_ramp(int note, float target_volume, const float *pre_wave, float *volumeBuffer) {
#ifdef GAP_LIMITER
    // Set the target volume for the oscillator (per-buffer constant)
    waves[note].target_volume = target_volume;

    // Local copies (avoid repeated volatile access)
    float v = waves[note].current_volume;
    const float t = waves[note].target_volume;

    // Compute per-buffer base time constants from runtime config
    // tau_ms are directly provided by configuration; alpha = 1 - exp(-1/(tau_s * Fs))
    float tau_up_ms   = g_sp3ctra_config.tau_up_base_ms;
    float tau_down_ms = g_sp3ctra_config.tau_down_base_ms;

    // Clamp taus to avoid division by zero or denormals
    if (tau_up_ms   < 0.01f) tau_up_ms   = 0.01f;
    if (tau_down_ms < 0.01f) tau_down_ms = 0.01f;
    // Cap extremely long time constants to avoid underflow/denormals and residual hiss
    if (tau_up_ms   > TAU_UP_MAX_MS)   tau_up_ms   = TAU_UP_MAX_MS;
    if (tau_down_ms > TAU_DOWN_MAX_MS) tau_down_ms = TAU_DOWN_MAX_MS;

    const float Fs = (float)SAMPLING_FREQUENCY;
    const float tau_up_s   = tau_up_ms   * 0.001f;
    const float tau_down_s = tau_down_ms * 0.001f;

    // Compute base alphas once per buffer
    float alpha_up   = 1.0f - expf(-1.0f / (tau_up_s   * Fs));
    float alpha_down = 1.0f - expf(-1.0f / (tau_down_s * Fs));

    // Clamp alphas to reasonable bounds
    if (alpha_up   < ALPHA_MIN) alpha_up   = ALPHA_MIN;
    if (alpha_down < ALPHA_MIN) alpha_down = ALPHA_MIN;
    if (alpha_up   > 1.0f)      alpha_up   = 1.0f;
    if (alpha_down > 1.0f)      alpha_down = 1.0f;

    // Frequency-dependent release weighting (applied only when t < v)
    float g_down = 1.0f;
    {
        float f = waves[note].frequency;
        if (f < 1.0f) f = 1.0f;
        float ratio = f / g_sp3ctra_config.decay_freq_ref_hz;
        // powf is used once per buffer (acceptable). If performance becomes critical, approximate.
        float w = powf(ratio, -g_sp3ctra_config.decay_freq_beta);
        if (w < DECAY_FREQ_MIN) w = DECAY_FREQ_MIN;
        if (w > DECAY_FREQ_MAX) w = DECAY_FREQ_MAX;
        g_down = w;
    }

    // Constants for phase weighting
    const float half_wave = (float)WAVE_AMP_RESOLUTION * 0.5f;
    const float inv_half  = (half_wave > 0.0f) ? (1.0f / half_wave) : 0.0f;

    // Adaptive phase epsilon for very long releases to avoid ultra-small alpha_eff
    float phase_eps = PHASE_WEIGHT_EPS;
    if (tau_down_ms > 500.0f) {
        float k = (tau_down_ms - 500.0f) / (TAU_DOWN_MAX_MS - 500.0f);
        if (k < 0.0f) k = 0.0f;
        if (k > 1.0f) k = 1.0f;
        phase_eps = PHASE_WEIGHT_EPS_MIN + k * (PHASE_WEIGHT_EPS_MAX - PHASE_WEIGHT_EPS_MIN);
    }

    // Debug counters removed to clean up logs
    
    for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        // Phase weighting using precomputed waveform sample
        float s_norm = 0.0f;
        if (pre_wave) {
            // Normalize to [-1, 1]
            s_norm = pre_wave[buff_idx] * inv_half;
            if (s_norm >  1.0f) s_norm =  1.0f;
            if (s_norm < -1.0f) s_norm = -1.0f;
        }
        float one_minus_s2 = 1.0f - (s_norm * s_norm);  // in [0,1]
        if (one_minus_s2 < phase_eps) one_minus_s2 = phase_eps;

        float w_phase;
        // Avoid powf in hot path: support common p = 1 or 2 fast, else fallback linear
        {
            float p = g_sp3ctra_config.phase_weight_power;
            if (p >= 1.9f && p <= 2.1f) {
                w_phase = one_minus_s2 * one_minus_s2; // p = 2
            } else {
                w_phase = one_minus_s2; // p ≈ 1 (default)
            }
        }

        // Exponential approach (recommended): v += alpha_eff * (t - v)
        const int is_attack = (t > v) ? 1 : 0;
        float alpha_base = is_attack ? alpha_up : (alpha_down * g_down);
        float alpha_eff = alpha_base;
        if (g_sp3ctra_config.enable_phase_weighted_slew) {
            alpha_eff *= w_phase;
        }
        // Clamp alpha_eff
        if (alpha_eff < ALPHA_MIN) alpha_eff = ALPHA_MIN;
        if (alpha_eff > 1.0f)      alpha_eff = 1.0f;

        v += alpha_eff * (t - v);
        // Clamp volume to legal range
        if (v < 0.0f) v = 0.0f;
        if (v > (float)VOLUME_AMP_RESOLUTION) v = (float)VOLUME_AMP_RESOLUTION;

        volumeBuffer[buff_idx] = v;
    }
    
    // Write back current volume once per buffer
    waves[note].current_volume = v;
#else
    // Without GAP_LIMITER, just fill with constant volume
    fill_float(target_volume, volumeBuffer, AUDIO_BUFFER_SIZE);

    // Keep state consistent
    waves[note].current_volume = target_volume;
    waves[note].target_volume = target_volume;
#endif
}


/**
 * @brief Apply non-linear gamma mapping to image buffer
 * @param imageBuffer_f32 Input/output float buffer
 * @param count Number of elements to process
 * @retval None
 */
void apply_gamma_mapping(float *imageBuffer_f32, int count) {
#if ENABLE_NON_LINEAR_MAPPING
    for (int i = 0; i < count; i++) {
        float normalizedIntensity = imageBuffer_f32[i] / (float)VOLUME_AMP_RESOLUTION;
        float gamma = GAMMA_VALUE;
        normalizedIntensity = powf(normalizedIntensity, gamma);
        imageBuffer_f32[i] = normalizedIntensity * VOLUME_AMP_RESOLUTION;
    }
#endif
}

/**
 * @brief Apply RELATIVE_MODE processing to image buffer
 * @param imageBuffer_q31 Input/output buffer
 * @param start_note Starting note index
 * @param end_note Ending note index (exclusive)
 * @retval None
 */
void apply_relative_mode(int32_t *imageBuffer_q31, int start_note, int end_note) {
  (void)imageBuffer_q31; // Mark as unused to suppress warning
  (void)start_note;      // Mark as unused to suppress warning
  (void)end_note;        // Mark as unused to suppress warning
#ifdef RELATIVE_MODE
    // Special processing for RELATIVE_MODE
    if (start_note < end_note - 1) {
        sub_int32((int32_t *)&imageBuffer_q31[0],
                  (int32_t *)&imageBuffer_q31[1],
                  (int32_t *)&imageBuffer_q31[0],
                  end_note - start_note - 1);

        clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION,
                   end_note - start_note);
    }

    if (end_note == NUMBER_OF_NOTES) {
        imageBuffer_q31[end_note - start_note - 1] = 0;
    }
#endif
}

/**
 * @brief Generate waveform samples using precomputed data
 * @param note Note index
 * @param waveBuffer Output waveform buffer
 * @param precomputed_wave_data Precomputed waveform data
 * @retval None
 */
void generate_waveform_samples(int note, float *waveBuffer, 
                              float precomputed_wave_data[AUDIO_BUFFER_SIZE]) {
    (void)note; // Mark as unused to suppress warning
    // CRITICAL FIX: Normalize waveform data from integer range to float [-1.0, +1.0]
    const float normalization_factor = 1.0f / (float)WAVE_AMP_RESOLUTION;
    for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        waveBuffer[buff_idx] = precomputed_wave_data[buff_idx] * normalization_factor;
    }
}
