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

#if !defined(__LINUX__) || !defined(__ARM_NEON)
// Standard C implementations (used on non-Linux platforms or when NEON is not available)
// On Linux with NEON, optimized versions from synth_additive_math_neon.c are used instead

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

#endif /* !__LINUX__ || !__ARM_NEON */

void fill_int32(int32_t value, int32_t *array, size_t length) {
  if (array == NULL) {
    return; // Error handling if array is NULL
  }

  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

#if !defined(__LINUX__) || !defined(__ARM_NEON)
// Standard C implementation (used on non-Linux platforms or when NEON is not available)
// On Linux with NEON, the optimized version from synth_additive_math_neon.c is used instead
void apply_volume_weighting(float *sum_buffer, const float *volume_buffer, 
                           float exponent, size_t length) {
  // Import pow_unit_fast for power calculation
  extern float pow_unit_fast(float x, float expo);
  
  for (size_t i = 0; i < length; ++i) {
    float current_volume = volume_buffer[i];
    float volume_normalized = current_volume / (float)VOLUME_AMP_RESOLUTION;
    float weighted_volume = pow_unit_fast(volume_normalized, exponent) * (float)VOLUME_AMP_RESOLUTION;
    sum_buffer[i] += weighted_volume;
  }
}
#endif /* !__LINUX__ || !__ARM_NEON */

#if !defined(__LINUX__) || !defined(__ARM_NEON)
// Standard C implementations for stereo panning and envelope (non-NEON platforms)

/**
 * @brief  Standard C stereo panning with linear interpolation
 */
void apply_stereo_pan_ramp(const float *mono_buffer, float *left_buffer, float *right_buffer,
                           float start_left, float start_right, float end_left, float end_right,
                           size_t length) {
  const float delta_l = end_left - start_left;
  const float delta_r = end_right - start_right;
  const float step = 1.0f / (float)length;
  
  float t = 0.0f;
  for (size_t i = 0; i < length; i++) {
    t += step;
    float gl = start_left + delta_l * t;
    float gr = start_right + delta_r * t;
    left_buffer[i] = mono_buffer[i] * gl;
    right_buffer[i] = mono_buffer[i] * gr;
  }
}

/**
 * @brief  Standard C exponential envelope with clamping
 */
float apply_envelope_ramp(float *volumeBuffer, float start_volume, float target_volume,
                          float alpha, size_t length, float min_vol, float max_vol) {
  float v = start_volume;
  const float t = target_volume;
  
  for (size_t i = 0; i < length; i++) {
    v += alpha * (t - v);
    if (v < min_vol) v = min_vol;
    if (v > max_vol) v = max_vol;
    volumeBuffer[i] = v;
  }
  
  return v;
}

#endif /* !__LINUX__ || !__ARM_NEON */

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
