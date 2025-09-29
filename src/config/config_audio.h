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

/**************************************************************************************
 * Audio Buffer Configuration - Hybrid Approach
 **************************************************************************************/
#define MAX_SAMPLING_FREQUENCY       96000   // Maximum compile-time sampling frequency

/**************************************************************************************
 * DAC Definitions - Optimized for Raspberry Pi Module 5
 **************************************************************************************/
#define AUDIO_CHANNEL                (2)

/**************************************************************************************
 * RtAudio Format Configuration - Optimized FLOAT32 Pipeline
 **************************************************************************************/
// Use RTAUDIO_FLOAT32 exclusively for optimal real-time performance
// FLOAT32 is natively compatible with internal synthesis calculations
// Provides best latency and performance on all supported devices (USB, Default)
#define RTAUDIO_FORMAT_TYPE        RTAUDIO_FLOAT32  
#define AUDIO_SAMPLE_FORMAT        "FLOAT32"

#endif // __CONFIG_AUDIO_H__
