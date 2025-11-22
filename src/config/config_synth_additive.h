/* config_synth_additive.h */

#ifndef __CONFIG_SYNTH_ADDITIVE_H__
#define __CONFIG_SYNTH_ADDITIVE_H__

#include "config_loader.h"  // For g_additive_config
#include "config_audio.h"   // For AUDIO_BUFFER_SIZE and SAMPLING_FREQUENCY
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
 * Summation Normalization and Volume Weighting
 **************************************************************************************/
// Intelligent volume weighting to prioritize strong oscillators over weak background noise
// INCREASED: Higher base level provides more headroom for compression without saturation
// REFACTORED: With VOLUME_AMP_RESOLUTION = 1.0, simply use the normalized value directly
#define SUMMATION_BASE_LEVEL        (0.2f)  // Base level to avoid division issues (normalized scale)

// Soft Limiter Configuration (prevents hard clipping while preserving dynamics)
// Runtime configurable values are loaded from sp3ctra.ini via g_sp3ctra_config
#define SOFT_LIMIT_THRESHOLD_DEFAULT  0.8f    // Default: 80% before soft compression
#define SOFT_LIMIT_KNEE_DEFAULT       0.1f    // Default: knee width for smooth transition

/**************************************************************************************
 * Adaptive Slew/Decay Configuration
 * Gap Limiter is always enabled with progressive attack/release envelope
 **************************************************************************************/
// Frequency-dependent release weighting (stabilizes highs vs lows)
#define DECAY_FREQ_MIN 0.001f
#define DECAY_FREQ_MAX 1000.0f

// Numerical safety and bounds (to avoid underflow/denormals and instability)
#define TAU_UP_MAX_MS 10000.0f     // Cap extremely long attacks
#define TAU_DOWN_MAX_MS 10000.0f   // Cap extremely long releases
#define ALPHA_MIN 1e-5f           // Minimum effective alpha to ensure progress and avoid denormals

/**************************************************************************************
 * Debug Configuration
 **************************************************************************************/
// Enable debug traces for additive oscillators (compile-time flag)
#define DEBUG_OSC

// Logging Parameters
#define LOG_FREQUENCY                100  // Approximate logging interval (in callbacks), decoupled from buffer size

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
