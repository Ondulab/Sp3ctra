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
#include "../../utils/shared.h"
#include "../../config/config_debug.h"
#include "../../config/config_loader.h"
#include <stdio.h>
#include <math.h>

#ifdef DEBUG_OSC
extern debug_additive_osc_config_t g_debug_osc_config;
#endif

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
        
        for (acc = 0; acc < g_additive_config.pixels_per_note; acc++) {
            imageBuffer_q31[local_note_idx] += (imageData[idx * g_additive_config.pixels_per_note + acc]);
        }
        // Average the accumulated values
        imageBuffer_q31[local_note_idx] /= g_additive_config.pixels_per_note;
        
        // Apply color inversion (dark pixels = more energy)
        imageBuffer_q31[local_note_idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[local_note_idx];
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
void apply_gap_limiter_ramp(int note, float target_volume, float *volumeBuffer) {
#ifdef GAP_LIMITER
    // Set the target volume for the oscillator
    waves[note].target_volume = target_volume;
    
    // Apply volume ramp per sample
    for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (waves[note].current_volume < waves[note].target_volume) {
            waves[note].current_volume += waves[note].volume_increment;
            if (waves[note].current_volume > waves[note].target_volume) {
                waves[note].current_volume = waves[note].target_volume;
            }
        } else if (waves[note].current_volume > waves[note].target_volume) {
            waves[note].current_volume -= waves[note].volume_decrement;
            if (waves[note].current_volume < waves[note].target_volume) {
                waves[note].current_volume = waves[note].target_volume;
            }
        }
        volumeBuffer[buff_idx] = waves[note].current_volume;

#ifdef DEBUG_OSC
        // Debug: Print oscillator values for specified range/single oscillator
        if (g_debug_osc_config.enabled) {
            int should_debug = 0;
            
            if (g_debug_osc_config.single_osc >= 0) {
                // Single oscillator mode
                should_debug = (note == g_debug_osc_config.single_osc);
            } else {
                // Range mode
                should_debug = (note >= g_debug_osc_config.start_osc && note <= g_debug_osc_config.end_osc);
            }
            
            if (should_debug) {
                printf("[DEBUG_OSC_%d] sample=%d target=%.1f current=%.1f inc=%.3f dec=%.3f max_inc=%.3f max_dec=%.3f freq=%.1f\n",
                       note,
                       buff_idx,
                       waves[note].target_volume,
                       waves[note].current_volume,
                       waves[note].volume_increment,
                       waves[note].volume_decrement,
                       waves[note].max_volume_increment,
                       waves[note].max_volume_decrement,
                       waves[note].frequency);
                fflush(stdout);
            }
        }
#endif
    }
#else
    // Without GAP_LIMITER, just fill with constant volume
    fill_float(target_volume, volumeBuffer, AUDIO_BUFFER_SIZE);
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
    // Use pre-computed data to avoid concurrent access to waves[]
    for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        waveBuffer[buff_idx] = precomputed_wave_data[buff_idx];
    }
}
