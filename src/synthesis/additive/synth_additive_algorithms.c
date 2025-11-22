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
    // Safety check: waves array must be initialized before we can update coefficients
    if (waves == NULL) {
        log_warning("ADDITIVE", "update_gap_limiter_coefficients: waves is NULL, skipping");
        return;  // Silently return if called before initialization
    }
    
    log_info("ADDITIVE", "update_gap_limiter_coefficients: Starting coefficient update");
    
    const float Fs = (float)g_sp3ctra_config.sampling_frequency;
    
#if !INSTANT_ATTACK
    // Get runtime tau parameter for attack (only if not instant attack)
    float tau_up_ms = g_sp3ctra_config.tau_up_base_ms;
    
    // Clamp tau to avoid division by zero or denormals
    if (tau_up_ms < 0.01f) tau_up_ms = 0.01f;
    if (tau_up_ms > TAU_UP_MAX_MS) tau_up_ms = TAU_UP_MAX_MS;
    
    const float tau_up_s = tau_up_ms * 0.001f;
    
    // Compute attack alpha (exponential envelope coefficient)
    float alpha_up = 1.0f - expf(-1.0f / (tau_up_s * Fs));
    
    // Clamp alpha to reasonable bounds
    if (alpha_up < ALPHA_MIN) alpha_up = ALPHA_MIN;
    if (alpha_up > 1.0f) alpha_up = 1.0f;
#endif
    
    // Get runtime tau parameter for release
    float tau_down_ms = g_sp3ctra_config.tau_down_base_ms;
    
    log_info("ADDITIVE", "  tau_down_base_ms from config: %.3f ms", tau_down_ms);
    
    // Clamp tau to avoid division by zero or denormals
    if (tau_down_ms < 0.01f) tau_down_ms = 0.01f;
    if (tau_down_ms > TAU_DOWN_MAX_MS) tau_down_ms = TAU_DOWN_MAX_MS;
    
    const float tau_down_s = tau_down_ms * 0.001f;
    
    // Compute release alpha (exponential envelope coefficient)
    float alpha_down = 1.0f - expf(-1.0f / (tau_down_s * Fs));
    
    // Clamp alpha to reasonable bounds
    if (alpha_down < ALPHA_MIN) alpha_down = ALPHA_MIN;
    if (alpha_down > 1.0f) alpha_down = 1.0f;
    
    log_info("ADDITIVE", "  alpha_down (base): %.6f", alpha_down);
    log_info("ADDITIVE", "  decay_freq_ref_hz: %.1f Hz", g_sp3ctra_config.decay_freq_ref_hz);
    log_info("ADDITIVE", "  decay_freq_beta: %.3f", g_sp3ctra_config.decay_freq_beta);
    
    // Precompute for each oscillator
    const int num_notes = get_current_number_of_notes();
    
    // Log first and last note for debugging
    int debug_notes[] = {0, num_notes / 2, num_notes - 1};
    
    for (int note = 0; note < num_notes; note++) {
#if !INSTANT_ATTACK
        // Store attack coefficient (frequency-independent) - only if not instant
        waves[note].alpha_up = alpha_up;
#endif
        
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
        
        // Debug log for selected notes
        for (int i = 0; i < 3; i++) {
            if (note == debug_notes[i]) {
                log_info("ADDITIVE", "  Note %d: freq=%.1f Hz, ratio=%.3f, g_down=%.3f, alpha_down_weighted=%.6f",
                         note, f, ratio, g_down, waves[note].alpha_down_weighted);
            }
        }
    }
    
    log_info("ADDITIVE", "update_gap_limiter_coefficients: Completed for %d notes", num_notes);
#endif
}

/**
 * @brief DEPRECATED - Image preprocessing now done in image_preprocessor.c
 * This function is kept for compatibility but should not be used.
 * Use preprocessed_data.additive.notes[] directly instead.
 */
void process_image_preprocessing(float *imageData, int32_t *imageBuffer_q31, 
                                int start_note, int end_note) {
    (void)imageData;
    (void)imageBuffer_q31;
    (void)start_note;
    (void)end_note;
    // DEPRECATED: All preprocessing is now done in image_preprocessor.c
    // This function should not be called anymore
}

/**
 * @brief Apply GAP_LIMITER volume ramp for a single note
 * @param note Note index
 * @param target_volume Target volume for the note
 * @param pre_wave Precomputed waveform data (unused)
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

#if INSTANT_ATTACK
    // ✅ INSTANT ATTACK MODE: Maximum performance optimization
    // Attack is instantaneous, only release is progressive
    if (t > v) {
        // Attack phase: instant transition to target volume
        fill_float(t, volumeBuffer, g_sp3ctra_config.audio_buffer_size);
        waves[note].current_volume = t;
    } else {
        // Release phase: progressive decay to avoid clicks
        const float alpha_down = waves[note].alpha_down_weighted;
        float final_volume = apply_envelope_ramp(volumeBuffer, v, t, alpha_down,
                                                g_sp3ctra_config.audio_buffer_size,
                                                0.0f, (float)VOLUME_AMP_RESOLUTION);
        waves[note].current_volume = final_volume;
    }
#else
    // PROGRESSIVE ATTACK MODE: Traditional envelope with attack and release
    // Use precomputed envelope coefficients (no complex calculations in RT path!)
    const float alpha = (t > v) ? waves[note].alpha_up : waves[note].alpha_down_weighted;
    
    // Use optimized envelope function (NEON-accelerated on ARM)
    float final_volume = apply_envelope_ramp(volumeBuffer, v, t, alpha,
                                            g_sp3ctra_config.audio_buffer_size,
                                            0.0f, (float)VOLUME_AMP_RESOLUTION);
    
    // Write back current volume once per buffer
    waves[note].current_volume = final_volume;
#endif

#else
    // Without GAP_LIMITER, just fill with constant volume
    fill_float(target_volume, volumeBuffer, g_sp3ctra_config.audio_buffer_size);

    // Keep state consistent
    waves[note].current_volume = target_volume;
    waves[note].target_volume = target_volume;
#endif
}

/**
 * @brief DEPRECATED - Gamma mapping now done in image_preprocessor.c
 * This function is kept for compatibility but should not be used.
 * Gamma is already applied in preprocessed_data.additive.grayscale[]
 */
void apply_gamma_mapping(float *imageBuffer_f32, int count) {
    (void)imageBuffer_f32;
    (void)count;
    // DEPRECATED: Gamma mapping is now done in image_preprocessor.c
    // This function should not be called anymore
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
