/*
 * synth_luxstral_stereo.c
 *
 * Stereo processing and color temperature analysis for additive synthesis
 * Contains functions for panoramization, contrast calculation, and color analysis
 *
 * Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "synth_luxstral_stereo.h"
#include "synth_luxstral_math.h"  // For VOLUME_AMP_RESOLUTION
#include "../../config/config_synth_luxstral.h"
#include "../../config/config_audio.h"
#include <math.h>
#include <stdio.h>

/* Private function implementations ------------------------------------------*/

/**
 * NOTE: calculate_contrast() has been moved to image_preprocessor.c
 * for better architectural coherence (preprocessing logic belongs in preprocessor)
 */

/**
 * @brief Calculate color temperature from RGB values (AGGRESSIVE VERSION)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @retval Pan position from -1.0 (warm/left) to +1.0 (cold/right)
 */
float calculate_color_temperature(uint8_t r, uint8_t g, uint8_t b) {
  // Convert RGB to normalized values
  float r_norm = r / 255.0f;
  float g_norm = g / 255.0f;
  float b_norm = b / 255.0f;
  
  // AGGRESSIVE ALGORITHM: Direct blue-red comparison for maximum stereo effect
  // Blue/Cyan = cold (right), Red/Yellow = warm (left)
  
  // Primary cold/warm axis: Blue vs Red (most important) - INVERTED
  float blue_red_diff = b_norm - r_norm;
  
  // Secondary axis: Cyan (G+B) vs Yellow (R+G) - INVERTED
  float cyan_strength = (g_norm + b_norm) * 0.5f;
  float yellow_strength = (r_norm + g_norm) * 0.5f;
  float cyan_yellow_diff = cyan_strength - yellow_strength;
  
  // Combine with configurable weight on blue-red axis
  float temperature = blue_red_diff * g_sp3ctra_config.stereo_blue_red_weight + cyan_yellow_diff * g_sp3ctra_config.stereo_cyan_yellow_weight;
  
  // Configurable amplification: Make the effect adjustable
  temperature *= g_sp3ctra_config.stereo_temperature_amplification;  // Amplify the base signal
  
  // Apply configurable non-linear curve to push values toward extremes
  if (temperature > 0) {
    temperature = powf(temperature, g_sp3ctra_config.stereo_temperature_curve_exponent);  // Configurable curve exponent
  } else {
    temperature = -powf(-temperature, g_sp3ctra_config.stereo_temperature_curve_exponent);
  }
  
  // Hard clamp to [-1, 1] range
  if (temperature > 1.0f) temperature = 1.0f;
  if (temperature < -1.0f) temperature = -1.0f;
  
  return temperature;
}

/**
 * @brief Calculate stereo pan gains using constant power law
 * @param pan_position Pan position from -1.0 (left) to +1.0 (right)
 * @param left_gain Output left channel gain (0.0 to 1.0)
 * @param right_gain Output right channel gain (0.0 to 1.0)
 * @retval None
 */
void calculate_pan_gains(float pan_position, float *left_gain, float *right_gain) {
  // Ensure pan position is in valid range
  if (pan_position < -1.0f) pan_position = -1.0f;
  if (pan_position > 1.0f) pan_position = 1.0f;
  
#if STEREO_PAN_LAW_CONSTANT_POWER
  // Constant power panning law (sin/cos curves)
  // Convert pan position to angle (0 to PI/2)
  float angle = (pan_position + 1.0f) * 0.25f * M_PI;
  
  // Calculate gains using trigonometric functions
  *left_gain = cosf(angle);
  *right_gain = sinf(angle);
  
  // Apply center compensation to maintain perceived loudness
  // At center (pan=0), both gains would be 0.707, boost slightly
  if (fabsf(pan_position) < STEREO_CENTER_COMPENSATION_THRESHOLD) {
    *left_gain *= STEREO_CENTER_BOOST_FACTOR;
    *right_gain *= STEREO_CENTER_BOOST_FACTOR;
  }
#else
  // Linear panning law (simpler but less perceptually uniform)
  *left_gain = (1.0f - pan_position) * 0.5f;
  *right_gain = (1.0f + pan_position) * 0.5f;
#endif
  
  // Ensure gains are in valid range
  if (*left_gain > 1.0f) *left_gain = 1.0f;
  if (*left_gain < 0.0f) *left_gain = 0.0f;
  if (*right_gain > 1.0f) *right_gain = 1.0f;
  if (*right_gain < 0.0f) *right_gain = 0.0f;
}
