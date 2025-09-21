/* image_debug.h */

#ifndef __IMAGE_DEBUG_H__
#define __IMAGE_DEBUG_H__

#include "config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************
 * Image Debug Visualization Functions
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
 * @brief Save RGB image data to PNG file for debugging
 * @param buffer_R Red channel data (8-bit)
 * @param buffer_G Green channel data (8-bit)
 * @param buffer_B Blue channel data (8-bit)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param filename Output filename (without extension)
 * @param stage_name Description of processing stage
 * @retval 0 on success, -1 on error
 */
int image_debug_save_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                        int width, int height, const char *filename, const char *stage_name);

/**
 * @brief Save grayscale image data to PNG file for debugging
 * @param buffer Grayscale data (16-bit or 32-bit)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param filename Output filename (without extension)
 * @param stage_name Description of processing stage
 * @param is_32bit Set to 1 for 32-bit data, 0 for 16-bit data
 * @retval 0 on success, -1 on error
 */
int image_debug_save_grayscale(void *buffer, int width, int height,
                              const char *filename, const char *stage_name, int is_32bit);

/**
 * @brief Save stereo channel visualization (warm/cold side by side)
 * @param warm_channel Warm channel data (32-bit)
 * @param cold_channel Cold channel data (32-bit)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param filename Output filename (without extension)
 * @param stage_name Description of processing stage
 * @retval 0 on success, -1 on error
 */
int image_debug_save_stereo_channels(int32_t *warm_channel, int32_t *cold_channel,
                                    int width, int height, const char *filename, const char *stage_name);

/**
 * @brief Create histogram visualization of pixel values
 * @param buffer Data buffer (32-bit)
 * @param size Number of pixels
 * @param filename Output filename (without extension)
 * @param stage_name Description of processing stage
 * @param max_value Maximum expected value for normalization
 * @retval 0 on success, -1 on error
 */
int image_debug_save_histogram(int32_t *buffer, int size, const char *filename,
                              const char *stage_name, int32_t max_value);

/**
 * @brief Capture and save all transformation stages for mono mode
 * @param buffer_R Original red channel
 * @param buffer_G Original green channel
 * @param buffer_B Original blue channel
 * @param grayscale_data Converted grayscale data
 * @param processed_data Final processed data for synthesis
 * @param frame_number Current frame number
 * @retval 0 on success, -1 on error
 */
int image_debug_capture_mono_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                     int32_t *grayscale_data, int32_t *processed_data, int frame_number);

/**
 * @brief Capture and save all transformation stages for stereo mode
 * @param buffer_R Original red channel
 * @param buffer_G Original green channel
 * @param buffer_B Original blue channel
 * @param warm_raw Raw warm channel data
 * @param cold_raw Raw cold channel data
 * @param warm_processed Final processed warm channel data
 * @param cold_processed Final processed cold channel data
 * @param frame_number Current frame number
 * @retval 0 on success, -1 on error
 */
int image_debug_capture_stereo_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                       int32_t *warm_raw, int32_t *cold_raw,
                                       int32_t *warm_processed, int32_t *cold_processed, int frame_number);

/**
 * @brief Check if debug capture should occur for this frame
 * @param frame_number Current frame number
 * @retval 1 if should capture, 0 if should skip
 */
int image_debug_should_capture(int frame_number);

/**
 * @brief Clean up old debug files to prevent disk space issues
 * @retval 0 on success, -1 on error
 */
int image_debug_cleanup_old_files(void);

/**
 * @brief Add a line to the temporal scan image (accumulates over time)
 * @param buffer_data Line data to add (32-bit)
 * @param width Width of the line (number of pixels)
 * @param scan_type Type of scan ("original", "grayscale", "processed")
 * @retval 0 on success, -1 on error
 */
int image_debug_add_scan_line(int32_t *buffer_data, int width, const char *scan_type);

/**
 * @brief Add RGB line to the temporal scan image (for original color data)
 * @param buffer_R Red channel data (8-bit)
 * @param buffer_G Green channel data (8-bit)
 * @param buffer_B Blue channel data (8-bit)
 * @param width Width of the line (number of pixels)
 * @param scan_type Type of scan ("original", "grayscale", "processed")
 * @retval 0 on success, -1 on error
 */
int image_debug_add_scan_line_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B, int width, const char *scan_type);

/**
 * @brief Save accumulated temporal scan image to file
 * @param scan_type Type of scan to save
 * @retval 0 on success, -1 on error
 */
int image_debug_save_scan(const char *scan_type);

/**
 * @brief Reset temporal scan buffer (start new scan)
 * @param scan_type Type of scan to reset
 * @retval 0 on success, -1 on error
 */
int image_debug_reset_scan(const char *scan_type);

/**
 * @brief Capture oscillator volumes for current audio sample
 * @retval 0 on success, -1 on error
 */
int image_debug_capture_oscillator_sample(void);

/**
 * @brief Add oscillator volume line to temporal scan
 * @param volumes Array of oscillator volumes (float)
 * @param count Number of oscillators
 * @retval 0 on success, -1 on error
 */
int image_debug_add_oscillator_volume_line(float *volumes, int count);

/**
 * @brief Save oscillator volume scan to PNG file
 * @retval 0 on success, -1 on error
 */
int image_debug_save_oscillator_volume_scan(void);

/**
 * @brief Reset oscillator volume scan buffer
 * @retval 0 on success, -1 on error
 */
int image_debug_reset_oscillator_volume_scan(void);

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

#ifdef __cplusplus
}
#endif

#endif // __IMAGE_DEBUG_H__
