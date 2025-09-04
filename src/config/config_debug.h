/* config_debug.h */

#ifndef __CONFIG_DEBUG_H__
#define __CONFIG_DEBUG_H__

/**************************************************************************************
 * Debug Definitions
 **************************************************************************************/
// #define PRINT_IFFT_FREQUENCY
// #define PRINT_IFFT_FREQUENCY_FULL
// #define DEBUG_MIDI
// #define DEBUG_UDP                // Uncomment to enable verbose UDP logging
// #define DEBUG_BUFFERS            // Uncomment to enable verbose buffer swap logging
// #define DEBUG_AUTO_VOLUME        // Enable auto-volume debug logging
// #define DEBUG_IMU_PACKETS        // Enable IMU packet reception logging
// #define DEBUG_AUDIO_INTERFACE    // Enable audio interface debug logging

/**************************************************************************************
 * Image Processing Debug Configuration
 **************************************************************************************/
// #define ENABLE_IMAGE_DEBUG            // Master switch for image debug system
// #define DEBUG_IMAGE_TRANSFORMATIONS  // Enable image transformation visualization
//#define DEBUG_IMAGE_SAVE_TO_FILES     // Save debug images to PNG files
//#define DEBUG_IMAGE_STEREO_CHANNELS   // Enable stereo channel visualization (warm/cold)
//#define DEBUG_IMAGE_SHOW_HISTOGRAMS   // Show pixel value histograms
//#define DEBUG_IMAGE_FRAME_COUNTER     // Add frame counter to debug images
// #define DEBUG_TEMPORAL_SCAN           // Enable temporal scan functionality (auto-save scan images)

// Debug image output directory (relative to executable)
#ifndef DEBUG_IMAGE_OUTPUT_DIR
#define DEBUG_IMAGE_OUTPUT_DIR "./debug_images/"
#endif

// Debug image capture frequency (capture every N frames to avoid flooding)
#ifndef DEBUG_IMAGE_CAPTURE_FREQUENCY
#define DEBUG_IMAGE_CAPTURE_FREQUENCY 1   // Capture every frame for testing
#endif

// Maximum number of debug images to keep (oldest files will be deleted)
#ifndef DEBUG_IMAGE_MAX_FILES
#define DEBUG_IMAGE_MAX_FILES 100
#endif

// Temporal scan configuration
#ifndef DEBUG_TEMPORAL_SCAN_MAX_LINES
#define DEBUG_TEMPORAL_SCAN_MAX_LINES 5000  // Auto-save every 20000 lines
#endif

// Force capture even with test pattern data
#ifndef DEBUG_FORCE_CAPTURE_TEST_DATA
#define DEBUG_FORCE_CAPTURE_TEST_DATA 1     // Enable capture of test pattern data
#endif

/**************************************************************************************
 * Oscillator Volume Debug Configuration
 **************************************************************************************/
// #define DEBUG_OSCILLATOR_VOLUMES          // Enable oscillator volume capture
// #define DEBUG_OSCILLATOR_VOLUME_SCAN      // Enable temporal scan of oscillator volumes

// Oscillator volume scan configuration
#ifndef OSCILLATOR_VOLUME_SCAN_SAMPLES
#define OSCILLATOR_VOLUME_SCAN_SAMPLES 48000  // 1 second at 48kHz sampling rate
#endif

// Auto-save threshold for oscillator volume scans
#ifndef OSCILLATOR_VOLUME_SCAN_AUTO_SAVE
#define OSCILLATOR_VOLUME_SCAN_AUTO_SAVE 48000  // Auto-save every 1 second
#endif

#endif // __CONFIG_DEBUG_H__
