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

/* Color conversion utilities */
uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                   int32_t *gray, uint32_t size);

/* Q24 fixed-point array operations */
void mult_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length);
void add_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length);
void sub_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length);
void scale_q24(q24_t *array, q24_t scale, size_t length);
void fill_q24(q24_t value, q24_t *array, size_t length);
void clip_q24(q24_t *array, q24_t min, q24_t max, size_t length);

/* Q24 conversion utilities */
void q24_to_float_array(const q24_t *q24_array, float *float_array, size_t length);
void float_to_q24_array(const float *float_array, q24_t *q24_array, size_t length);

#endif /* __SYNTH_ADDITIVE_MATH_H__ */
