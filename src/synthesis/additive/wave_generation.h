/*
 * wave_generation.h
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __WAVE_GENERATION_H
#define __WAVE_GENERATION_H

/* Includes ------------------------------------------------------------------*/
#include "config.h"
#include "synth_additive.h"
#include <stdint.h>

/* Private includes ----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/
typedef enum {
  MAJOR,
  MINOR,
} harmonizationType;

struct waveParams {
  uint32_t commaPerSemitone;
  uint32_t startFrequency;
  harmonizationType harmonization;
  uint32_t harmonizationLevel;
  uint32_t waveformOrder;
};

/* Wave structure (moved from shared.h) */
struct wave {
    volatile float *start_ptr;
    uint32_t current_idx;
    uint32_t area_size;
    uint32_t octave_coeff;
    uint32_t octave_divider;
    float target_volume;
    float current_volume;
    float volume_increment;
    float max_volume_increment;
    float volume_decrement;
    float max_volume_decrement;
    float frequency;
    
    // Stereo panoramization fields
    float pan_position;      // Pan position: -1.0 (left) to +1.0 (right)
    float left_gain;         // Left channel gain (0.0 to 1.0)
    float right_gain;        // Right channel gain (0.0 to 1.0)
};

/* Exported constants --------------------------------------------------------*/
#define WAVEFORM_TABLE_SIZE        (10000000)

extern volatile struct waveParams wavesGeneratorParams;
extern volatile struct wave waves[MAX_NUMBER_OF_NOTES];
extern volatile float unitary_waveform[WAVEFORM_TABLE_SIZE];

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
uint32_t init_waves(volatile float *unitary_waveform,
                    volatile struct wave *waves,
                    volatile struct waveParams *parameters);

/* Private defines -----------------------------------------------------------*/

#endif /* __WAVE_GENERATION_H */
