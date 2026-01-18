/*
 * pow_approx.c
 *
 * Fast approximate power functions for hot audio paths.
 * See pow_approx.h for API.
 *
 * Author: Cline
 */

#include "vst_adapters_c.h"
#include "pow_approx.h"
#include <math.h>
#include <stdint.h>

/* Internal helpers */

static inline float clampf(float x, float lo, float hi) {
  return (x < lo) ? lo : (x > hi) ? hi : x;
}

static inline int approx_eq(float a, float b, float eps) {
  float d = a - b;
  return (d < 0.0f ? -d : d) <= eps;
}

/* =========================
 *  Unit-domain cache [0,1]
 * ========================= */

typedef struct {
  float last_expo;
  int   valid;
  float lut[POW_LUT_SIZE];
} unit_cache_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
static _Thread_local unit_cache_t g_unit_cache;
#else
/* Fallback if _Thread_local is unavailable (single-threaded or TLS not required) */
static unit_cache_t g_unit_cache;
#endif

static void build_unit_lut(float expo) {
  const int N = POW_LUT_SIZE;
  const float invN = 1.0f / (float)(N - 1);
  for (int i = 0; i < N; ++i) {
    float t = (float)i * invN;
    g_unit_cache.lut[i] = powf(t, expo);
  }
  g_unit_cache.last_expo = expo;
  g_unit_cache.valid = 1;
}

float pow_unit_fast(float x, float expo) {
#ifndef USE_POW_APPROX
  return powf(clampf(x, 0.0f, 1.0f), expo);
#else
  /* Clamp domain */
  x = clampf(x, 0.0f, 1.0f);

  /* Fast paths: common exponents */
  if (approx_eq(expo, 1.0f, POW_FAST_PATH_EPS)) {
    return x;
  }
  if (approx_eq(expo, 2.0f, POW_FAST_PATH_EPS)) {
    return x * x;
  }
  if (approx_eq(expo, 3.0f, POW_FAST_PATH_EPS)) {
    float x2 = x * x;
    return x2 * x;
  }
  if (approx_eq(expo, 4.0f, POW_FAST_PATH_EPS)) {
    float x2 = x * x;
    return x2 * x2;
  }

  /* Thread-local LUT build on demand */
  if (!g_unit_cache.valid || !approx_eq(expo, g_unit_cache.last_expo, POW_APPROX_EPS)) {
    build_unit_lut(expo);
  }

  /* Linear interpolation */
  const float f = x * (float)(POW_LUT_SIZE - 1);
  int idx = (int)f;
  if (idx >= POW_LUT_SIZE - 1) {
    return g_unit_cache.lut[POW_LUT_SIZE - 1];
  }
  const float t = f - (float)idx;
  const float y0 = g_unit_cache.lut[idx];
  const float y1 = g_unit_cache.lut[idx + 1];
  return y0 + (y1 - y0) * t;
#endif
}

#ifdef __ARM_NEON
/**
 * @brief NEON-vectorized pow_unit_fast for 4 values simultaneously
 * @param v_x NEON vector of 4 input values (will be clamped to [0,1])
 * @param expo Exponent value
 * @return NEON vector of 4 output values (x^expo for each element)
 * 
 * This function uses the same LUT as pow_unit_fast but processes 4 values
 * in parallel using NEON SIMD instructions.
 */
float32x4_t pow_unit_fast_neon_v4(float32x4_t v_x, float expo) {
#ifndef USE_POW_APPROX
  // Fallback: scalar powf for each element (slow but functional)
  float x[4];
  vst1q_f32(x, v_x);
  float result[4];
  for (int i = 0; i < 4; i++) {
    float val = clampf(x[i], 0.0f, 1.0f);
    result[i] = powf(val, expo);
  }
  return vld1q_f32(result);
#else
  // Clamp domain to [0,1]
  float32x4_t v_zero = vdupq_n_f32(0.0f);
  float32x4_t v_one = vdupq_n_f32(1.0f);
  v_x = vmaxq_f32(v_x, v_zero);  // max(x, 0)
  v_x = vminq_f32(v_x, v_one);   // min(x, 1)
  
  // Fast path detection for common exponents
  int is_linear = approx_eq(expo, 1.0f, POW_FAST_PATH_EPS);
  int is_square = approx_eq(expo, 2.0f, POW_FAST_PATH_EPS);
  int is_cubic = approx_eq(expo, 3.0f, POW_FAST_PATH_EPS);
  int is_quartic = approx_eq(expo, 4.0f, POW_FAST_PATH_EPS);
  
  if (is_linear) {
    return v_x;
  }
  if (is_square) {
    return vmulq_f32(v_x, v_x);
  }
  if (is_cubic) {
    float32x4_t v_x2 = vmulq_f32(v_x, v_x);
    return vmulq_f32(v_x2, v_x);
  }
  if (is_quartic) {
    float32x4_t v_x2 = vmulq_f32(v_x, v_x);
    return vmulq_f32(v_x2, v_x2);
  }
  
  // Ensure LUT is built for this exponent
  if (!g_unit_cache.valid || !approx_eq(expo, g_unit_cache.last_expo, POW_APPROX_EPS)) {
    build_unit_lut(expo);
  }
  
  // LUT interpolation (vectorized)
  // Compute fractional index: f = x * (POW_LUT_SIZE - 1)
  float32x4_t v_scale = vdupq_n_f32((float)(POW_LUT_SIZE - 1));
  float32x4_t v_f = vmulq_f32(v_x, v_scale);
  
  // Convert to integer indices (NEON doesn't have direct float-to-int vector conversion for indices)
  // We need to extract values and do scalar LUT lookups
  // This is still faster than scalar loop due to reduced overhead
  float f[4];
  vst1q_f32(f, v_f);
  
  float result[4];
  for (int i = 0; i < 4; i++) {
    int idx = (int)f[i];
    if (idx >= POW_LUT_SIZE - 1) {
      result[i] = g_unit_cache.lut[POW_LUT_SIZE - 1];
    } else {
      float t = f[i] - (float)idx;
      float y0 = g_unit_cache.lut[idx];
      float y1 = g_unit_cache.lut[idx + 1];
      result[i] = y0 + (y1 - y0) * t;
    }
  }
  
  return vld1q_f32(result);
#endif
}
#endif /* __ARM_NEON */

/* =================================
 *  Shifted-domain cache [base, base+1]
 * ================================= */

typedef struct {
  float last_base;
  float last_expo;
  int   valid;
  float lut[POW_LUT_SIZE];
} shifted_cache_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
static _Thread_local shifted_cache_t g_shifted_cache;
#else
static shifted_cache_t g_shifted_cache;
#endif

static void build_shifted_lut(float base, float expo) {
  const int N = POW_LUT_SIZE;
  const float invN = 1.0f / (float)(N - 1);
  for (int i = 0; i < N; ++i) {
    float t01 = (float)i * invN;
    float x = base + t01 * POW_SHIFTED_DOMAIN_SIZE;
    g_shifted_cache.lut[i] = powf(x, expo);
  }
  g_shifted_cache.last_base = base;
  g_shifted_cache.last_expo = expo;
  g_shifted_cache.valid = 1;
}

float pow_shifted_fast(float x, float base, float expo) {
  (void)base; /* Parameter reserved for future use */
#ifndef USE_POW_APPROX
  return powf(x, expo);
#else
  /* Fast paths first (no domain clamp needed for these exponents) */
  if (approx_eq(expo, 1.0f, POW_FAST_PATH_EPS)) {
    return x;
  }
  if (approx_eq(expo, 0.5f, POW_FAST_PATH_EPS)) {
    /* sqrt is typically much faster than powf */
    return sqrtf(x < 0.0f ? 0.0f : x);
  }
  if (approx_eq(expo, 2.0f, POW_FAST_PATH_EPS)) {
    return x * x;
  }

  /* Domain handling and fallback to powf if x exceeds LUT range */
  const float lo = base;
  const float hi = base + POW_SHIFTED_DOMAIN_SIZE;
  if (x < lo) {
    x = lo;
  } else if (x > hi) {
  #if defined(POW_APPROX_ENABLE_FALLBACK_COUNT)
    /* Thread-local fallback counter (no logging in RT path) */
    static _Thread_local unsigned long g_shifted_fallback_count = 0;
    g_shifted_fallback_count++;
  #endif
    return powf(x, expo);
  }

  /* Thread-local LUT build on demand (rebuild if base or expo changed) */
  if (!g_shifted_cache.valid ||
      !approx_eq(base, g_shifted_cache.last_base, POW_APPROX_EPS) ||
      !approx_eq(expo, g_shifted_cache.last_expo, POW_APPROX_EPS)) {
    build_shifted_lut(base, expo);
  }

  /* Map x to [0,1] for interpolation */
  const float t01 = (x - base) / POW_SHIFTED_DOMAIN_SIZE;
  const float f = t01 * (float)(POW_LUT_SIZE - 1);
  int idx = (int)f;
  if (idx >= POW_LUT_SIZE - 1) {
    return g_shifted_cache.lut[POW_LUT_SIZE - 1];
  }
  const float t = f - (float)idx;
  const float y0 = g_shifted_cache.lut[idx];
  const float y1 = g_shifted_cache.lut[idx + 1];
  return y0 + (y1 - y0) * t;
#endif
}
