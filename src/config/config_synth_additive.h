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
#define CONTRAST_STRIDE              4.0f      // Pixel sampling stride for optimization
#define CONTRAST_ADJUSTMENT_POWER    0.7f      // Exponent for adjusting the contrast curve

// Non-Linear Intensity Mapping
#define ENABLE_NON_LINEAR_MAPPING    1         // Set to 1 to enable non-linear mapping, or 0 to disable
#define GAMMA_VALUE                  1.8f      // Gamma value for non-linear intensity correction

/**************************************************************************************
 * Gap Limiter Configuration
 **************************************************************************************/
#define GAP_LIMITER

/**************************************************************************************
 * Adaptive Slew/Decay Configuration
 **************************************************************************************/
// Enable phase-weighted slew to minimize gain changes at waveform peaks
#ifndef ENABLE_PHASE_WEIGHTED_SLEW
#define ENABLE_PHASE_WEIGHTED_SLEW 1
#endif

// Select decay mode: 0 = legacy linear ramp, 1 = exponential (recommended)
#ifndef SLEW_DECAY_MODE_EXPO
#define SLEW_DECAY_MODE_EXPO 1
#endif

// Base time constants for envelope slew (multiplied by runtime divisors)
#ifndef TAU_UP_BASE_MS
#define TAU_UP_BASE_MS 2.0f     // Base attack time in milliseconds
#endif

#ifndef TAU_DOWN_BASE_MS
#define TAU_DOWN_BASE_MS 3.0f   // Base release time in milliseconds
#endif

// Phase weighting parameters (applied per sample)
#ifndef PHASE_WEIGHT_POWER
#define PHASE_WEIGHT_POWER 1.0f // 1.0 = linear, 2.0 = square
#endif

#ifndef PHASE_WEIGHT_EPS
#define PHASE_WEIGHT_EPS 1.01f // Prevents zero alpha at peaks
#endif

// Frequency-dependent release weighting (stabilizes highs vs lows)
#ifndef DECAY_FREQ_REF_HZ
#define DECAY_FREQ_REF_HZ 440.0f
#endif

#ifndef DECAY_FREQ_BETA
#define DECAY_FREQ_BETA -0.8f    // >0 slows highs, <0 speeds them
#endif

#ifndef DECAY_FREQ_MIN
#define DECAY_FREQ_MIN 0.7f
#endif

#ifndef DECAY_FREQ_MAX
#define DECAY_FREQ_MAX 1.3f
#endif

// Numerical safety and bounds (to avoid underflow/denormals and instability)
#ifndef TAU_UP_MAX_MS
#define TAU_UP_MAX_MS 2000.0f     // Cap extremely long attacks
#endif

#ifndef TAU_DOWN_MAX_MS
#define TAU_DOWN_MAX_MS 2000.0f   // Cap extremely long releases (per-base before divisor)
#endif

#ifndef ALPHA_MIN
#define ALPHA_MIN 1e-5f           // Minimum effective alpha to ensure progress and avoid denormals
#endif

// If target and current volume are both under this floor, snap to 0 to avoid residual hiss/denormals
#ifndef RELEASE_FLOOR_VOLUME
#define RELEASE_FLOOR_VOLUME 1.0f  // in VOLUME_AMP_RESOLUTION units (0..65535). Tune 1..8 if needed.
#endif

// Dynamic phase epsilon bounds (helps when tau is very large)
#ifndef PHASE_WEIGHT_EPS_MIN
#define PHASE_WEIGHT_EPS_MIN 0.005f
#endif

#ifndef PHASE_WEIGHT_EPS_MAX
#define PHASE_WEIGHT_EPS_MAX 0.05f
#endif

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
