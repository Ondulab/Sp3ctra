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
 * @brief Precompute GAP_LIMITER envelope coefficients for all oscillators
 * Called at startup and when tau parameters change at runtime
 * @retval None
 */
void update_gap_limiter_coefficients(void) {
#ifdef GAP_LIMITER
    const float Fs = (float)g_sp3ctra_config.sampling_frequency;
    
    // Get runtime tau parameters
    float tau_up_ms = g_sp3ctra_config.tau_up_base_ms;
    float tau_down_ms = g_sp3ctra_config.tau_down_base_ms;
    
    // Clamp taus to avoid division by zero or denormals
    if (tau_up_ms < 0.01f) tau_up_ms = 0.01f;
    if (tau_down_ms < 0.01f) tau_down_ms = 0.01f;
    if (tau_up_ms > TAU_UP_MAX_MS) tau_up_ms = TAU_UP_MAX_MS;
    if (tau_down_ms > TAU_DOWN_MAX_MS) tau_down_ms = TAU_DOWN_MAX_MS;
    
    const float tau_up_s = tau_up_ms * 0.001f;
    const float tau_down_s = tau_down_ms * 0.001f;
    
    // Compute base alphas (exponential envelope coefficients)
    float alpha_up = 1.0f - expf(-1.0f / (tau_up_s * Fs));
    float alpha_down = 1.0f - expf(-1.0f / (tau_down_s * Fs));
    
    // Clamp alphas to reasonable bounds
    if (alpha_up < ALPHA_MIN) alpha_up = ALPHA_MIN;
    if (alpha_down < ALPHA_MIN) alpha_down = ALPHA_MIN;
    if (alpha_up > 1.0f) alpha_up = 1.0f;
    if (alpha_down > 1.0f) alpha_down = 1.0f;
    
    // Precompute for each oscillator
    const int num_notes = get_current_number_of_notes();
    for (int note = 0; note < num_notes; note++) {
        // Store attack coefficient (frequency-independent)
        waves[note].alpha_up = alpha_up;
        
        // Compute frequency-dependent release weighting
        float f = waves[note].frequency;
        if (f < 1.0f) f = 1.0f;
        
        float ratio = f / g_sp3ctra_config.decay_freq_ref_hz;
        float g_down = powf(ratio, -g_sp3ctra_config.decay_freq_beta);
        
        // Clamp frequency weight
        if (g_down < DECAY_FREQ_MIN) g_down = DECAY_FREQ_MIN;
        if (g_down > DECAY_FREQ_MAX) g_down = DECAY_FREQ_MAX;
        
        // Store weighted release coefficient
        waves[note].alpha_down_weighted = alpha_down * g_down;
    }
#endif
}

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
    (void)pre_wave; // No longer used (phase weighting removed)
    
#ifdef GAP_LIMITER
    // Set the target volume for the oscillator
    waves[note].target_volume = target_volume;

    // Local copies (avoid repeated volatile access)
    float v = waves[note].current_volume;
    const float t = waves[note].target_volume;

    // Use precomputed envelope coefficients (no complex calculations in RT path!)
    const float alpha = (t > v) ? waves[note].alpha_up : waves[note].alpha_down_weighted;
    
    // RT-optimized envelope loop (exponential approach: v += alpha * (t - v))
    for (int buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        v += alpha * (t - v);
        
        // Clamp volume to valid range
        if (v < 0.0f) v = 0.0f;
        if (v > (float)VOLUME_AMP_RESOLUTION) v = (float)VOLUME_AMP_RESOLUTION;
        
        volumeBuffer[buff_idx] = v;
    }
    
    // Write back current volume once per buffer
    waves[note].current_volume = v;
#else
    // Without GAP_LIMITER, just fill with constant volume
    fill_float(target_volume, volumeBuffer, g_sp3ctra_config.audio_buffer_size);

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
                              const float *precomputed_wave_data) {
    (void)note; // Mark as unused to suppress warning
    // CRITICAL FIX: Normalize waveform data from integer range to float [-1.0, +1.0]
    const float normalization_factor = 1.0f / (float)WAVE_AMP_RESOLUTION;
    for (int buff_idx = 0; buff_idx < g_sp3ctra_config.audio_buffer_size; buff_idx++) {
        waveBuffer[buff_idx] = precomputed_wave_data[buff_idx] * normalization_factor;
    }
}
