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

/**************************************************************************************
 * Q24 Fixed-Point Mathematical Functions
 **************************************************************************************/

/**
 * @brief Multiply two Q24 arrays element-wise with overflow protection
 * @param a First input array (Q24)
 * @param b Second input array (Q24)
 * @param result Output array (Q24)
 * @param length Number of elements to process
 * @retval None
 */
void mult_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    // Use 64-bit intermediate to prevent overflow
    int64_t temp = ((int64_t)a[i] * (int64_t)b[i]) >> 24;
    
    // Clamp to Q24 range
    if (temp > Q24_MAX) {
      result[i] = Q24_MAX;
    } else if (temp < Q24_MIN) {
      result[i] = Q24_MIN;
    } else {
      result[i] = (q24_t)temp;
    }
  }
}

/**
 * @brief Add two Q24 arrays element-wise with overflow protection
 * @param a First input array (Q24)
 * @param b Second input array (Q24)
 * @param result Output array (Q24)
 * @param length Number of elements to process
 * @retval None
 */
void add_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    // Use 64-bit intermediate to detect overflow
    int64_t temp = (int64_t)a[i] + (int64_t)b[i];
    
    // Clamp to Q24 range
    if (temp > Q24_MAX) {
      result[i] = Q24_MAX;
    } else if (temp < Q24_MIN) {
      result[i] = Q24_MIN;
    } else {
      result[i] = (q24_t)temp;
    }
  }
}

/**
 * @brief Scale Q24 array by Q24 scalar with overflow protection
 * @param array Input/output array (Q24)
 * @param scale Scaling factor (Q24)
 * @param length Number of elements to process
 * @retval None
 */
void scale_q24(q24_t *array, q24_t scale, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    // Use 64-bit intermediate to prevent overflow
    int64_t temp = ((int64_t)array[i] * (int64_t)scale) >> 24;
    
    // Clamp to Q24 range
    if (temp > Q24_MAX) {
      array[i] = Q24_MAX;
    } else if (temp < Q24_MIN) {
      array[i] = Q24_MIN;
    } else {
      array[i] = (q24_t)temp;
    }
  }
}

/**
 * @brief Fill Q24 array with constant value
 * @param value Fill value (Q24)
 * @param array Output array (Q24)
 * @param length Number of elements to fill
 * @retval None
 */
void fill_q24(q24_t value, q24_t *array, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

/**
 * @brief Subtract two Q24 arrays element-wise with overflow protection
 * @param a First input array (Q24)
 * @param b Second input array (Q24)
 * @param result Output array (Q24)
 * @param length Number of elements to process
 * @retval None
 */
void sub_q24(const q24_t *a, const q24_t *b, q24_t *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    // Use 64-bit intermediate to detect overflow
    int64_t temp = (int64_t)a[i] - (int64_t)b[i];
    
    // Clamp to Q24 range
    if (temp > Q24_MAX) {
      result[i] = Q24_MAX;
    } else if (temp < Q24_MIN) {
      result[i] = Q24_MIN;
    } else {
      result[i] = (q24_t)temp;
    }
  }
}

/**
 * @brief Clip Q24 array values to specified range
 * @param array Input/output array (Q24)
 * @param min Minimum value (Q24)
 * @param max Maximum value (Q24)
 * @param length Number of elements to process
 * @retval None
 */
void clip_q24(q24_t *array, q24_t min, q24_t max, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (array[i] < min) {
      array[i] = min;
    } else if (array[i] > max) {
      array[i] = max;
    }
  }
}

/**
 * @brief Convert Q24 array to float array
 * @param q24_array Input Q24 array
 * @param float_array Output float array
 * @param length Number of elements to convert
 * @retval None
 */
void q24_to_float_array(const q24_t *q24_array, float *float_array, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    float_array[i] = Q24_TO_FLOAT(q24_array[i]);
  }
}

/**
 * @brief Convert float array to Q24 array
 * @param float_array Input float array
 * @param q24_array Output Q24 array
 * @param length Number of elements to convert
 * @retval None
 */
void float_to_q24_array(const float *float_array, q24_t *q24_array, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    q24_array[i] = FLOAT_TO_Q24(float_array[i]);
  }
}
