/*
 * synth_additive_math.c
 *
 * Mathematical utilities for additive synthesis
 * Contains reusable mathematical functions and color conversion utilities
 *
 * Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_math.h"

/* Private user code ---------------------------------------------------------*/

void sub_int32(const int32_t *a, const int32_t *b, int32_t *result,
               size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] - b[i];
  }
}

void clip_int32(int32_t *array, int32_t min, int32_t max, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (array[i] < min) {
      array[i] = min;
    } else if (array[i] > max) {
      array[i] = max;
    }
  }
}

void mult_float(const float *a, const float *b, float *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] * b[i];
  }
}

void add_float(const float *a, const float *b, float *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] + b[i];
  }
}

void scale_float(float *array, float scale, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    array[i] *= scale;
  }
}

void fill_float(float value, float *array, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

void fill_int32(int32_t value, int32_t *array, size_t length) {
  if (array == NULL) {
    return; // Error handling if array is NULL
  }

  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                   int32_t *gray, uint32_t size) {
  uint32_t i = 0;

  for (i = 0; i < size; i++) {
    uint32_t r = (uint32_t)buffer_R[i];
    uint32_t g = (uint32_t)buffer_G[i];
    uint32_t b = (uint32_t)buffer_B[i];

    uint32_t weighted = (r * 299 + g * 587 + b * 114);
    // Normalization to 16 bits (0 - 65535)
    gray[i] = (int32_t)((weighted * 65535UL) / 255000UL);
  }

  return 0;
}
