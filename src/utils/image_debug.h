/* image_debug.h */

#ifndef __IMAGE_DEBUG_H__
#define __IMAGE_DEBUG_H__

#include "config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************
 * Image Debug Functions - Simplified
 * 
 * Supports only the 3 debug options:
 * --debug-image[=LINES]                    Raw scanner capture
 * --debug-additive-osc-image[=SAMPLES[,m]] Oscillator volume capture
 * --debug-additive-osc=<N|N-M>            Oscillator debug (handled in main.c)
 **************************************************************************************/

/**
 * @brief Initialize image debug system
 * Creates output directory and sets up internal state
 * @retval 0 on success, -1 on error
 */
int image_debug_init(void);

/**
 * @brief Cleanup image debug system
 * Frees resources and closes files
 * @retval None
 */
void image_debug_cleanup(void);

/**
 * @brief Enable or disable image debug at runtime
 * @param enable 1 to enable, 0 to disable
 * @retval None
 */
void image_debug_enable_runtime(int enable);

/**
 * @brief Check if image debug is enabled at runtime
 * @retval 1 if enabled, 0 if disabled
 */
int image_debug_is_enabled(void);

/**************************************************************************************
 * Raw Scanner Capture Functions (--debug-image)
 **************************************************************************************/

/**
 * @brief Capture raw scanner line (unprocessed RGB data)
 * @param buffer_R Red channel data (8-bit)
 * @param buffer_G Green channel data (8-bit)
 * @param buffer_B Blue channel data (8-bit)
 * @retval 0 on success, -1 on error
 */
int image_debug_capture_raw_scanner_line(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B);

/**
 * @brief Save raw scanner capture to PNG file
 * @retval 0 on success, -1 on error
 */
int image_debug_save_raw_scanner_capture(void);

/**
 * @brief Reset raw scanner capture buffer
 * @retval 0 on success, -1 on error
 */
int image_debug_reset_raw_scanner_capture(void);

/**
 * @brief Configure raw scanner capture at runtime
 * @param enable 1 to enable raw scanner capture, 0 to disable
 * @param capture_lines Number of lines to capture before auto-saving (0 = use default)
 * @retval None
 */
void image_debug_configure_raw_scanner(int enable, int capture_lines);

/**
 * @brief Check if raw scanner capture is enabled at runtime
 * @retval 1 if enabled, 0 if disabled
 */
int image_debug_is_raw_scanner_enabled(void);

/**
 * @brief Get current raw scanner capture lines setting
 * @retval Number of lines configured for capture
 */
int image_debug_get_raw_scanner_lines(void);

/**************************************************************************************
 * Oscillator Volume Capture Functions (--debug-additive-osc-image)
 **************************************************************************************/

/**
 * @brief Configure oscillator volume capture at runtime
 * @param enable 1 to enable oscillator capture, 0 to disable
 * @param capture_samples Number of samples to capture before auto-saving (0 = use default)
 * @param enable_markers 1 to enable visual markers, 0 to disable
 * @retval None
 */
void image_debug_configure_oscillator_capture(int enable, int capture_samples, int enable_markers);

/**
 * @brief Check if oscillator volume capture is enabled at runtime
 * @retval 1 if enabled, 0 if disabled
 */
int image_debug_is_oscillator_capture_enabled(void);

/**
 * @brief Get current oscillator capture samples setting
 * @retval Number of samples configured for capture
 */
int image_debug_get_oscillator_capture_samples(void);

/**
 * @brief Ultra-fast volume capture for real-time processing
 * This function performs minimal work - just stores values in a buffer
 * Only active when --debug-additive-osc-image is enabled
 * @param note Note index
 * @param current_volume Current volume value
 * @param target_volume Target volume value
 * @retval None (inline for maximum performance)
 */
void image_debug_capture_volume_sample_fast(int note, float current_volume, float target_volume);

/**
 * @brief Mark current line as a new image boundary for visual debugging
 * This function marks the current capture line as the start of a new scanner image
 * Used to draw yellow separator lines in the oscillator volume visualization
 * @retval None
 */
void image_debug_mark_new_image_boundary(void);

#ifdef __cplusplus
}
#endif

#endif // __IMAGE_DEBUG_H__
