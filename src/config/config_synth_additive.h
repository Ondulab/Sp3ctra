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

#endif // __CONFIG_SYNTH_ADDITIVE_H__
