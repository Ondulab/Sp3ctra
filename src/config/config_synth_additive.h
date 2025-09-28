/* config_synth_additive.h */

#ifndef __CONFIG_SYNTH_ADDITIVE_H__
#define __CONFIG_SYNTH_ADDITIVE_H__

#include "config_loader.h"  // For g_additive_config
#include "config_instrument.h"  // For CIS_MAX_PIXELS_NB

/**************************************************************************************
 * Stereo Configuration
 **************************************************************************************/
// Pan law configuration
#define STEREO_PAN_LAW_CONSTANT_POWER    1      // 1 = cos/sin law (constant power), 0 = linear law

// Center compensation parameters (technical constants)
#define STEREO_CENTER_COMPENSATION_THRESHOLD  0.1f   // Threshold for center compensation detection
#define STEREO_CENTER_BOOST_FACTOR           1.02f   // Center boost factor to maintain perceived loudness

/**************************************************************************************
 * Image Processing Configuration
 **************************************************************************************/
// Global Volume Contrast Modulation Control
// Uncomment the line below to disable global volume modulation based on image contrast
// #define DISABLE_CONTRAST_MODULATION

// Image Processing and Contrast Modulation
#define CONTRAST_MIN                 0.01f     // Minimum volume for blurred images (0.0 to 1.0)
#define CONTRAST_STRIDE              1.0f      // Pixel sampling stride for optimization
#define CONTRAST_ADJUSTMENT_POWER    0.5f      // Exponent for adjusting the contrast curve

// Non-Linear Intensity Mapping
#define ENABLE_NON_LINEAR_MAPPING    1         // Set to 1 to enable non-linear mapping, or 0 to disable
#define GAMMA_VALUE                  3.8f      // Gamma value for non-linear intensity correction

/**************************************************************************************
 * Summation Normalization and Volume Weighting
 **************************************************************************************/
// Intelligent volume weighting to prioritize strong oscillators over weak background noise
#define VOLUME_WEIGHTING_EXPONENT    2.0f      // 1.0=linear, 2.0=quadratic, 3.0=cubic (higher = stronger volumes dominate more)
#define SUMMATION_RESPONSE_EXPONENT  0.7f      // Final response curve (0.5=anti-compress, 1.0=linear, 2.0=compress)
#define SUMMATION_BASE_LEVEL        (0.05f * VOLUME_AMP_RESOLUTION)  // Base level to avoid division issues

/**************************************************************************************
 * Gap Limiter Configuration
 **************************************************************************************/
#define GAP_LIMITER

/**************************************************************************************
 * Adaptive Slew/Decay Configuration
 **************************************************************************************/
// Select decay mode: 0 = legacy linear ramp, 1 = exponential (recommended)
#define SLEW_DECAY_MODE_EXPO 1

// Base time constants for envelope slew (multiplied by runtime divisors)
#define TAU_UP_BASE_MS 0.5f     // Base attack time in milliseconds
#define TAU_DOWN_BASE_MS 0.5f   // Base release time in milliseconds

// Frequency-dependent release weighting (stabilizes highs vs lows)
#define DECAY_FREQ_REF_HZ 440.0f
#define DECAY_FREQ_BETA -1.2f    // >0 slows highs, <0 speeds them

#define DECAY_FREQ_MIN 0.004f
#define DECAY_FREQ_MAX 200.0f

// Numerical safety and bounds (to avoid underflow/denormals and instability)
#define TAU_UP_MAX_MS 2000.0f     // Cap extremely long attacks
#define TAU_DOWN_MAX_MS 2000.0f   // Cap extremely long releases (per-base before divisor)
#define ALPHA_MIN 1e-5f           // Minimum effective alpha to ensure progress and avoid denormals

// Enable phase-weighted slew to minimize gain changes at waveform peaks
#define ENABLE_PHASE_WEIGHTED_SLEW 1

// Phase weighting parameters (applied per sample)
#define PHASE_WEIGHT_POWER 2.0f // 1.0 = linear, 2.0 = square
#define PHASE_WEIGHT_EPS 0.01f // Prevents zero alpha at peaks

// Dynamic phase epsilon bounds (helps when tau is very large)
#define PHASE_WEIGHT_EPS_MIN 0.005f
#define PHASE_WEIGHT_EPS_MAX 0.05f

/**************************************************************************************
 * Debug Configuration
 **************************************************************************************/
// Enable debug traces for additive oscillators (compile-time flag)
#define DEBUG_OSC

// Logging Parameters
#define LOG_FREQUENCY                (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE) // Approximate logging frequency in Hz

/**************************************************************************************
 * Auto-volume Configuration
 **************************************************************************************/
// IMU detection thresholds (hardware-dependent constants)
#define IMU_ACTIVE_THRESHOLD_X       0.01f     // Minimum IMU movement to detect activity
#define IMU_FILTER_ALPHA_X           0.25f     // Low-pass filter coefficient for IMU smoothing

// Auto-volume timing (performance-critical constant)
#define AUTO_VOLUME_POLL_MS          10        // Polling interval for auto-volume thread (ms)

/**************************************************************************************
 * Debug Auto-Freeze (for development)
 * After N received images, freeze synth data (keep reception and pipeline running).
 **************************************************************************************/
#ifndef ADDITIVE_DEBUG_AUTOFREEZE_ENABLE
#define ADDITIVE_DEBUG_AUTOFREEZE_ENABLE 0
#endif

#ifndef ADDITIVE_DEBUG_AUTOFREEZE_AFTER_IMAGES
#define ADDITIVE_DEBUG_AUTOFREEZE_AFTER_IMAGES 5000
#endif

#endif // __CONFIG_SYNTH_ADDITIVE_H__
