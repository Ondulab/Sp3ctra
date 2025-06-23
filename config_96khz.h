/* config_96khz.h - Configuration optimisée pour 96kHz */

#ifndef __CONFIG_96KHZ_H__
#define __CONFIG_96KHZ_H__

// Include the base config
#include "src/core/config.h"

// Override audio settings for 96kHz testing
#undef SAMPLING_FREQUENCY
#undef AUDIO_BUFFER_SIZE

#define SAMPLING_FREQUENCY (96000)
#define AUDIO_BUFFER_SIZE (300) // 300 frames = 3.125ms latency at 96kHz

// Recalculate LOG_FREQUENCY for 96kHz
#undef LOG_FREQUENCY
#define LOG_FREQUENCY (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE)

#endif // __CONFIG_96KHZ_H__
