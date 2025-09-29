/* config_audio.h */

#ifndef __CONFIG_AUDIO_H__
#define __CONFIG_AUDIO_H__

#include "config_loader.h"  // For g_additive_config access

/**************************************************************************************
 * Audio Effects Definitions
 **************************************************************************************/
// Reverb Configuration - Optimized for Sp3ctra Synthesis
#define ENABLE_REVERB                0       // Set to 1 to enable reverb, 0 to disable
#define DEFAULT_REVERB_MIX           0.8f    // Default dry/wet mix (0.0 - 1.0) - 0 = no reverb, 0.3 = 30% wet + 70% dry
#define DEFAULT_REVERB_ROOM_SIZE     0.75f   // Default room size (0.0 - 1.0) - Optimized for ~1.6s reverb time
#define DEFAULT_REVERB_DAMPING       0.65f   // Default damping (0.0 - 1.0) - Enhanced high-frequency absorption
#define DEFAULT_REVERB_WIDTH         0.8f    // Default stereo width (0.0 - 1.0)
#define DEFAULT_REVERB_PREDELAY      0.08f   // Default pre-delay in seconds (0.0 - 0.1)

// Reverb Send Levels - Default values when no MIDI controller is connected
#define DEFAULT_REVERB_SEND_ADDITIVE 0.9f    // Default reverb send for additive synthesis (0.0 - 1.0)
#define DEFAULT_REVERB_SEND_POLYPHONIC 0.3f  // Default reverb send for polyphonic synthesis (0.0 - 1.0)

// Note: Advanced Zita-Rev1 parameters below are not implemented in current algorithm
// They are kept for potential future implementation but currently unused
#define DEFAULT_REVERB_RT_LOW        3.0f    // Low frequency reverb time (seconds) [UNUSED]
#define DEFAULT_REVERB_RT_MID        2.0f    // Mid frequency reverb time (seconds) [UNUSED]
#define DEFAULT_REVERB_FDAMP         3000.0f // High frequency damping frequency (Hz) [UNUSED]
#define DEFAULT_REVERB_XOVER         200.0f  // Crossover frequency (Hz) [UNUSED]
#define DEFAULT_REVERB_OPMIX         0.0f    // Output mix parameter (0.0 - 1.0) [UNUSED]
#define DEFAULT_REVERB_RGXYZ         0.0f    // Ambisonic parameter (-1.0 - 1.0) [UNUSED]

/**************************************************************************************
 * Audio Buffer Configuration - Hybrid Approach
 **************************************************************************************/
#define MAX_SAMPLING_FREQUENCY       96000   // Maximum compile-time sampling frequency

/**************************************************************************************
 * DAC Definitions - Optimized for Raspberry Pi Module 5
 **************************************************************************************/
#define AUDIO_CHANNEL                (2)

// Automatic cache sizing for smooth volume transitions in audio callback
// Target: ~2% of buffer size for imperceptible volume steps
// This ensures smooth volume changes regardless of buffer size
#define AUDIO_CACHE_UPDATE_FREQUENCY_BASE 2  // 2% of buffer size base

// Ensure minimum of 4 and maximum of 32 for performance and stability
// Note: This is calculated at runtime in audio callback using g_sp3ctra_config.audio_buffer_size
#define AUDIO_CACHE_UPDATE_FREQUENCY_MIN  4
#define AUDIO_CACHE_UPDATE_FREQUENCY_MAX  32

/**************************************************************************************
 * RtAudio Format Configuration - Optimized FLOAT32 Pipeline
 **************************************************************************************/
// Use RTAUDIO_FLOAT32 exclusively for optimal real-time performance
// FLOAT32 is natively compatible with internal synthesis calculations
// Provides best latency and performance on all supported devices (USB, Default)
#define RTAUDIO_FORMAT_TYPE        RTAUDIO_FLOAT32  
#define AUDIO_SAMPLE_FORMAT        "FLOAT32"

#endif // __CONFIG_AUDIO_H__
