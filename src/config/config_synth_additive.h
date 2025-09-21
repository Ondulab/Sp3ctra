/* config_synth_additive.h */

#ifndef __CONFIG_SYNTH_ADDITIVE_H__
#define __CONFIG_SYNTH_ADDITIVE_H__

// #define DISABLE_ADDITIVE

/**************************************************************************************
 * Stereo Panoramization Configuration
 **************************************************************************************/
// Enable/disable stereo mode
#define STEREO_MODE

// Pan law configuration
#define STEREO_PAN_LAW_CONSTANT_POWER    1      // 1 = cos/sin law (constant power), 0 = linear law

// Normalization configuration  
#define STEREO_NORMALIZE_METHOD          2      // 0=none, 1=fixed, 2=dynamic peak, 3=adaptive RMS
#define STEREO_NORMALIZE_FIXED_FACTOR    0.1f   // Used when method=1

// Color temperature analysis - perceptual luminance weights
#define PERCEPTUAL_WEIGHT_R          (0.21f)    // Red contribution to perceived brightness
#define PERCEPTUAL_WEIGHT_G          (0.72f)    // Green contribution to perceived brightness
#define PERCEPTUAL_WEIGHT_B          (0.07f)    // Blue contribution to perceived brightness

// Opponent color axes for warm/cold separation
#define OPPONENT_ALPHA               (1.0f)     // Weight for red-blue opponent axis
#define OPPONENT_BETA                (0.5f)     // Weight for green-magenta opponent axis

// Color classification thresholds
#define CHROMATIC_THRESHOLD          (0.1f)     // Minimum saturation to be considered chromatic
#define ACHROMATIC_SPLIT             (0.5f)     // Pan split for achromatic colors (0.5 = center)

/**************************************************************************************
 * Synth and Image Processing Configuration
 **************************************************************************************/

// Image Processing and Contrast Modulation
#define CONTRAST_MIN                 0.01f                   // Minimum volume for blurred images (0.0 to 1.0)
#define CONTRAST_STRIDE              4.0f                   // Pixel sampling stride for optimization
#define CONTRAST_ADJUSTMENT_POWER    0.7f                   // Exponent for adjusting the contrast curve
// Non-Linear Intensity Mapping
#define ENABLE_NON_LINEAR_MAPPING    1                      // Set to 1 to enable non-linear mapping, or 0 to disable
#define GAMMA_VALUE                  1.8f                   // Gamma value for non-linear intensity correction

#define ENABLE_IMAGE_TEMPORAL_SMOOTHING 0
//  Temporal Image Smoothing Configuration
//  Default: disabled at compile time. Define ENABLE_IMAGE_TEMPORAL_SMOOTHING=1
//  in your build flags if you want to enable without editing this file.
#ifndef ENABLE_IMAGE_TEMPORAL_SMOOTHING
#define ENABLE_IMAGE_TEMPORAL_SMOOTHING 0                   /* 0 = disabled, 1 = enabled (default) */
#endif

#ifndef IMAGE_TEMPORAL_SMOOTHING_ALPHA
#define IMAGE_TEMPORAL_SMOOTHING_ALPHA 0.98f                /* Smoothing factor (0.0-1.0, higher = more smoothing) */
#endif

#ifndef IMAGE_NOISE_GATE_THRESHOLD
#define IMAGE_NOISE_GATE_THRESHOLD   0.001f                 /* Noise gate threshold (relative to max amplitude) */
#endif

#ifndef IMAGE_ADAPTIVE_SMOOTHING
#define IMAGE_ADAPTIVE_SMOOTHING     0                      /* Enable adaptive smoothing based on variation magnitude (0/1) */
#endif

#define WAVE_AMP_RESOLUTION          (16777215)             // Decimal value
#define VOLUME_AMP_RESOLUTION        (65535)                // Decimal value
#define START_FREQUENCY              (65.41)
#define MAX_OCTAVE_NUMBER            (8)                    // >> le nb d'octaves n'a pas d'incidence ?
#define SEMITONE_PER_OCTAVE          (12)
#define COMMA_PER_SEMITONE           (36)

#define VOLUME_INCREMENT             (1)
#define VOLUME_DECREMENT             (1)

#define PIXELS_PER_NOTE              (1)
#define NUMBER_OF_NOTES              (CIS_MAX_PIXELS_NB / PIXELS_PER_NOTE)

/**************************************************************************************
 * Phase-Aware Gap Limiter Configuration
 **************************************************************************************/
#define GAP_LIMITER

 // Enable phase-aware gap limiter for improved audio quality
#define ENABLE_PHASE_AWARE_GAP_LIMITER 0                    // Set to 1 to enable, 0 to use classic Gap Limiter

// Phase-aware gap limiter mode enumeration (cleaner than #define constants)
typedef enum {
  PHASE_AWARE_MODE_CONTINUOUS = 0,                          // Continuous phase-weighted changes
  PHASE_AWARE_MODE_ZERO_CROSS = 1                           // Changes only at zero crossings
} phase_aware_mode_t;

// Default phase-aware mode selection
// #define PHASE_AWARE_MODE PHASE_AWARE_MODE_CONTINUOUS
#define PHASE_AWARE_MODE             PHASE_AWARE_MODE_ZERO_CROSS

// Phase-aware gap limiter parameters
#define MIN_PHASE_FACTOR             0.1f                   // Minimum phase factor (prevents complete blocking)
#define SMALL_CHANGE_THRESHOLD       1000.0f                // Threshold between small/large volume changes
#define PHASE_SENSITIVITY            1.0f                   // Phase sensitivity factor (0.5-2.0 range)

// Zero crossing detection parameters
#define ZERO_CROSSING_THRESHOLD      0.05f                  // Threshold for zero crossing detection (relative to max amplitude)

// Hysteresis anti-oscillation parameters
#define ENABLE_HYSTERESIS_ANTI_OSCILLATION 0                // Enable hysteresis to prevent oscillation
#define HYSTERESIS_HIGH_THRESHOLD    0.6f                   // Upper threshold for enabling changes
#define HYSTERESIS_LOW_THRESHOLD     0.4f                   // Lower threshold for disabling changes

// Debug mode for phase-aware gap limiter
// #define DEBUG_PHASE_AWARE_GAP_LIMITER
#define DEBUG_NOTE_INDEX             100                    // Note index to debug (if debug enabled)

/**************************************************************************************
 * Additive Oscillator Debug Configuration
 **************************************************************************************/
// Enable debug traces for additive oscillators (compile-time flag)
#define DEBUG_OSC

#ifdef DEBUG_OSC
#pragma message "🔧 DEBUG_OSC is enabled - additive oscillator debug features compiled"
#endif

// Debug configuration structure (runtime)
typedef struct {
    int enabled;                    // Runtime enable/disable flag
    int single_osc;                 // -1 if range, otherwise single oscillator number
    int start_osc;                  // Start of range (for ranges)
    int end_osc;                    // End of range (for ranges)
} debug_additive_osc_config_t;

// Logging Parameters
#define LOG_FREQUENCY                (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE) // Approximate logging frequency in Hz

/**************************************************************************************
 * Auto-volume
 **************************************************************************************/
#define IMU_ACTIVE_THRESHOLD_X       (0.01f)                /* Threshold on accel X to consider active (sensor units) */
#define IMU_FILTER_ALPHA_X           (0.25f)                /* Exponential smoothing alpha for acc X (0..1) */
#define IMU_INACTIVITY_TIMEOUT_S     (5)                    /* Seconds of no activity before dimming */
#define AUTO_VOLUME_INACTIVE_LEVEL   (0.01f)                /* Target volume when inactive (0.0..1.0) */
#define AUTO_VOLUME_ACTIVE_LEVEL     (1.0f)                 /* Target volume when active (0.0..1.0) */
#define AUTO_VOLUME_FADE_MS          (600)                  /* Fade duration in milliseconds */
#define AUTO_VOLUME_POLL_MS          (10)                   /* How often auto-volume updates (ms) */
#define AUTO_VOLUME_DISABLE_WITH_MIDI 1                     /* If 1, disable auto-dim when MIDI controller connected */

#endif // __CONFIG_SYNTH_ADDITIVE_H__
