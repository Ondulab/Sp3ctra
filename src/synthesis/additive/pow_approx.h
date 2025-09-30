/*
 * pow_approx.h
 *
 * Fast approximate power functions for hot audio paths:
 *  - pow_unit_fast(x, expo): x in [0,1]
 *  - pow_shifted_fast(x, base, expo): x in [base, base+1]
 *
 * Design:
 *  - Fast paths for common exponents (1/2/3/4, 0.5)
 *  - Thread-local LUTs with linear interpolation for general exponents
 *  - No dynamic allocation in hot path; LUTs rebuild lazily on param change
 *
 * Author: Cline
 */

#ifndef POW_APPROX_H
#define POW_APPROX_H

#include <stddef.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration */
#ifndef POW_LUT_SIZE
#define POW_LUT_SIZE 1024
#endif

#ifndef POW_APPROX_EPS
#define POW_APPROX_EPS 1e-6f
#endif

#ifndef POW_FAST_PATH_EPS
#define POW_FAST_PATH_EPS 1e-3f
#endif

/* API (float-only) */

/**
 * Compute x^expo for x in [0,1] with fast paths and a thread-local LUT.
 * - Fast paths: expo≈1, 2, 3, 4 (tolerance POW_FAST_PATH_EPS)
 * - Otherwise: LUT with linear interpolation
 */
float pow_unit_fast(float x, float expo);

/**
 * Compute x^expo for x in [base, base+1] with fast paths and a thread-local LUT.
 * - Fast paths: expo≈1 (identity), expo≈0.5 (sqrt), expo≈2 (square)
 * - Otherwise: LUT with linear interpolation over [base, base+1]
 */
float pow_shifted_fast(float x, float base, float expo);

#ifdef __ARM_NEON
/**
 * NEON-vectorized version of pow_unit_fast for 4 values simultaneously.
 * Computes x^expo for 4 values of x in [0,1] using LUT interpolation.
 * @param v_x NEON vector of 4 input values (will be clamped to [0,1])
 * @param expo Exponent value
 * @return NEON vector of 4 output values
 */
float32x4_t pow_unit_fast_neon_v4(float32x4_t v_x, float expo);
#endif /* __ARM_NEON */

#ifdef __cplusplus
}
#endif

#endif /* POW_APPROX_H */
