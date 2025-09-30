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
#define WAVE_AMP_RESOLUTION          (16777215)             // Decimal value for wave amplitude
#define VOLUME_AMP_RESOLUTION        (65535)                // Decimal value for volume amplitude

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

/* Color conversion utilities */
uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                   int32_t *gray, uint32_t size);


#endif /* __SYNTH_ADDITIVE_MATH_H__ */
