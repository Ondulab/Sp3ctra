/* config_audio.h */

#ifndef __CONFIG_AUDIO_H__
#define __CONFIG_AUDIO_H__

/**************************************************************************************
 * Audio Effects Definitions
 **************************************************************************************/
// Reverb Configuration
#define ENABLE_REVERB                0       // Set to 1 to enable reverb, 0 to disable
#define DEFAULT_REVERB_MIX           0.0f    // Default dry/wet mix (0.0 - 1.0) - 0 = no reverb
#define DEFAULT_REVERB_ROOM_SIZE     0.7f    // Default room size (0.0 - 1.0)
#define DEFAULT_REVERB_DAMPING       0.5f    // Default damping (0.0 - 1.0)
#define DEFAULT_REVERB_WIDTH         1.0f    // Default stereo width (0.0 - 1.0)
#define DEFAULT_REVERB_PREDELAY      0.02f   // Default pre-delay in seconds (0.0 - 0.1)

// Advanced reverb parameters for Zita-Rev1 algorithm
#define DEFAULT_REVERB_RT_LOW        3.0f    // Low frequency reverb time (seconds)
#define DEFAULT_REVERB_RT_MID        2.0f    // Mid frequency reverb time (seconds)
#define DEFAULT_REVERB_FDAMP         3000.0f // High frequency damping frequency (Hz)
#define DEFAULT_REVERB_XOVER         200.0f  // Crossover frequency (Hz)
#define DEFAULT_REVERB_OPMIX         0.0f    // Output mix parameter (0.0 - 1.0)
#define DEFAULT_REVERB_RGXYZ         0.0f    // Ambisonic parameter (-1.0 - 1.0)

/**************************************************************************************
 * DAC Definitions - Optimized for Raspberry Pi Module 5
 **************************************************************************************/
#define SAMPLING_FREQUENCY           (48000)
#define AUDIO_CHANNEL                (2)

// Buffer size optimized for Pi Module 5 with real-time synthesis
// Larger buffer reduces audio dropouts during intensive FFT processing
// 48kHz: 150 frames = 3.125ms latency (optimal for real-time)
// 96kHz: 600 frames = 6.25ms latency (double latency for synthesis headroom)
#if SAMPLING_FREQUENCY >= 96000
#define AUDIO_BUFFER_SIZE            (250)
#elif SAMPLING_FREQUENCY >= 48000
#define AUDIO_BUFFER_SIZE            (400)   // 150
#else
#define AUDIO_BUFFER_SIZE            (128)
#endif

// Automatic cache sizing for smooth volume transitions in audio callback
// Target: ~2% of buffer size for imperceptible volume steps
// This ensures smooth volume changes regardless of buffer size
#define AUDIO_CACHE_UPDATE_FREQUENCY ((AUDIO_BUFFER_SIZE * 2) / 100) // 2% of buffer size

// Ensure minimum of 4 and maximum of 32 for performance and stability
#define AUDIO_CACHE_UPDATE_FREQUENCY_CLAMPED \
  ((AUDIO_CACHE_UPDATE_FREQUENCY < 4)    ? 4 \
   : (AUDIO_CACHE_UPDATE_FREQUENCY > 32) ? 32 \
                                         : AUDIO_CACHE_UPDATE_FREQUENCY)

#endif // __CONFIG_AUDIO_H__
