/*
 * synth_additive_math.h
 *
 * Mathematical utilities for additive synthesis
 * Contains reusable mathematical functions and color conversion utilities
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_MATH_H__
#define __SYNTH_ADDITIVE_MATH_H__

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stddef.h>
#include "../../config/config_synth_additive.h"

/**************************************************************************************
 * Core Synthesis Mathematical Constants
 **************************************************************************************/
// Resolution constants for amplitude calculations
// REFACTORED: Normalized to 1.0 for float operations (waveforms already in [-1,+1], volumes in [0,1])
#define WAVE_AMP_RESOLUTION          (1.0f)                  // Normalized to 1.0 (waveforms in [-1,+1])
#define VOLUME_AMP_RESOLUTION        (1.0f)                  // Normalized to 1.0 (volumes in [0,1])

/* Exported function prototypes ----------------------------------------------*/

/* Integer array operations */
void sub_int32(const int32_t *a, const int32_t *b, int32_t *result, size_t length);
void clip_int32(int32_t *array, int32_t min, int32_t max, size_t length);
void fill_int32(int32_t value, int32_t *array, size_t length);

/* Float array operations */
void mult_float(const float *a, const float *b, float *result, size_t length);
void add_float(const float *a, const float *b, float *result, size_t length);
void scale_float(float *array, float scale, size_t length);
void fill_float(float value, float *array, size_t length);

/* Volume weighting with power function (optimized with NEON on ARM) */
void apply_volume_weighting(float *sum_buffer, const float *volume_buffer, 
                           float exponent, size_t length);

/* Stereo panning with linear interpolation (optimized with NEON on ARM) */
void apply_stereo_pan_ramp(const float *mono_buffer, float *left_buffer, float *right_buffer,
                           float start_left, float start_right, float end_left, float end_right,
                           size_t length);

/* Exponential envelope generation (optimized with NEON on ARM) */
float apply_envelope_ramp(float *volumeBuffer, float start_volume, float target_volume,
                          float alpha, size_t length, float min_vol, float max_vol);

/* Color conversion utilities */
uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                   float *gray, uint32_t size);


#endif /* __SYNTH_ADDITIVE_MATH_H__ */
