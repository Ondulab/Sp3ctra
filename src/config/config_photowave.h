/* config_photowave.h */

#ifndef __CONFIG_PHOTOWAVE_H__
#define __CONFIG_PHOTOWAVE_H__

/**************************************************************************************
 * Photowave Synthesis Configuration
 * 
 * Photowave synthesis transforms image lines into audio waveforms through
 * spatial→temporal transduction. Each pixel becomes a sample in the audio buffer.
 **************************************************************************************/

/**************************************************************************************
 * Scanning Mode Configuration
 **************************************************************************************/
// Default scanning mode (can be overridden by MIDI CC1 or config file)
// 0 = Left to Right, 1 = Right to Left, 2 = Dual (ping-pong)
#define PHOTOWAVE_DEFAULT_SCAN_MODE     0

/**************************************************************************************
 * Continuous Mode Configuration
 **************************************************************************************/
// Default continuous mode (can be overridden by config file)
// 0 = Only generate on MIDI notes, 1 = Always generating
#define PHOTOWAVE_DEFAULT_CONTINUOUS_MODE   0

/**************************************************************************************
 * Interpolation Configuration
 **************************************************************************************/
// Default interpolation mode (can be overridden by MIDI CC74 or config file)
// 0 = Linear interpolation, 1 = Cubic interpolation
#define PHOTOWAVE_DEFAULT_INTERP_MODE   0

/**************************************************************************************
 * Amplitude Configuration
 **************************************************************************************/
// Default amplitude (0.0 to 1.0, can be overridden by MIDI CC7 or config file)
#define PHOTOWAVE_DEFAULT_AMPLITUDE     0.5f

// Amplitude range limits
#define PHOTOWAVE_MIN_AMPLITUDE         0.0f
#define PHOTOWAVE_MAX_AMPLITUDE         1.0f

/**************************************************************************************
 * Frequency Range Configuration
 **************************************************************************************/
// Maximum frequency limit (Hz) - prevents aliasing and excessive high frequencies
#define PHOTOWAVE_MAX_FREQUENCY         12000.0f

// Minimum frequency is calculated dynamically as: sample_rate / pixel_count
// Example: 48000 Hz / 3456 pixels (400 DPI) = 13.89 Hz
// Example: 48000 Hz / 1728 pixels (200 DPI) = 27.78 Hz

/**************************************************************************************
 * MIDI Control Configuration
 **************************************************************************************/
// MIDI CC mappings (standard MIDI controller numbers)
#define PHOTOWAVE_CC_SCAN_MODE          1       // CC1: Modulation wheel → Scan mode
#define PHOTOWAVE_CC_AMPLITUDE          7       // CC7: Volume → Amplitude
#define PHOTOWAVE_CC_INTERPOLATION      74      // CC74: Brightness → Interpolation mode

/**************************************************************************************
 * Buffer Configuration
 **************************************************************************************/
// Internal buffer size for blur processing (must be >= max pixel count)
#define PHOTOWAVE_INTERNAL_BUFFER_SIZE  4096

/**************************************************************************************
 * Thread Configuration
 **************************************************************************************/
// Thread sleep time in microseconds (balance between CPU usage and responsiveness)
#define PHOTOWAVE_THREAD_SLEEP_US       1000    // 1ms sleep between buffer fills

/**************************************************************************************
 * Debug Configuration
 **************************************************************************************/
// Enable debug traces for photowave synthesis (compile-time flag)
// #define DEBUG_PHOTOWAVE

#endif // __CONFIG_PHOTOWAVE_H__
