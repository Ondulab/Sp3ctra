/*
 * synth_additive_algorithms.h
 *
 * Centralized algorithms for additive synthesis
 * Contains core processing algorithms used by both threading and sequential modes
 *
 * Author: zhonx
 * Created: 21 sep. 2025
 */

#ifndef SYNTH_ADDITIVE_ALGORITHMS_H
#define SYNTH_ADDITIVE_ALGORITHMS_H

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "../../config/config_synth_additive.h"
#include "wave_generation.h"

/* Function prototypes -------------------------------------------------------*/

/**
 * @brief Process image preprocessing (averaging and color inversion)
 * @param imageData Input image data
 * @param imageBuffer_q31 Output processed buffer
 * @param start_note Starting note index
 * @param end_note Ending note index (exclusive)
 * @retval None
 */
void process_image_preprocessing(int32_t *imageData, int32_t *imageBuffer_q31, 
                                int start_note, int end_note);

/**
 * @brief Apply GAP_LIMITER volume ramp for a single note
 * @param note Note index
 * @param target_volume Target volume for the note
 * @param volumeBuffer Output volume buffer for audio samples
 * @retval None
 */
void apply_gap_limiter_ramp(int note, float target_volume, float *volumeBuffer);


/**
 * @brief Apply non-linear gamma mapping to image buffer
 * @param imageBuffer_f32 Input/output float buffer
 * @param count Number of elements to process
 * @retval None
 */
void apply_gamma_mapping(float *imageBuffer_f32, int count);

/**
 * @brief Apply RELATIVE_MODE processing to image buffer
 * @param imageBuffer_q31 Input/output buffer
 * @param start_note Starting note index
 * @param end_note Ending note index (exclusive)
 * @retval None
 */
void apply_relative_mode(int32_t *imageBuffer_q31, int start_note, int end_note);

/**
 * @brief Generate waveform samples using precomputed data
 * @param note Note index
 * @param waveBuffer Output waveform buffer
 * @param precomputed_wave_data Precomputed waveform data
 * @retval None
 */
void generate_waveform_samples(int note, float *waveBuffer, 
                              float precomputed_wave_data[AUDIO_BUFFER_SIZE]);

#endif /* SYNTH_ADDITIVE_ALGORITHMS_H */
