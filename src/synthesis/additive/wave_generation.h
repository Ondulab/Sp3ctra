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
    // Waveform data pointers
    volatile float *start_ptr;              // Float32 waveform table pointer
    volatile q24_t *start_ptr_q24;          // Q24 waveform table pointer
    
    // Index and size parameters (shared between float and Q24)
    uint32_t current_idx;
    uint32_t area_size;
    uint32_t octave_coeff;
    uint32_t octave_divider;
    
    // Volume parameters - Float32 (legacy)
    float target_volume;
    float current_volume;
    float volume_increment;
    float max_volume_increment;
    float volume_decrement;
    float max_volume_decrement;
    
    // Volume parameters - Q24 (new)
    q24_t target_volume_q24;                // Q24 [0, Q24_ONE]
    q24_t current_volume_q24;               // Q24 [0, Q24_ONE]
    q24_t volume_increment_q24;             // Q24
    q24_t max_volume_increment_q24;         // Q24
    q24_t volume_decrement_q24;             // Q24
    q24_t max_volume_decrement_q24;         // Q24
    
    // Frequency (keep as float for initialization calculations)
    float frequency;
    
    // Stereo panoramization fields - Float32 (legacy)
    float pan_position;                     // Pan position: -1.0 (left) to +1.0 (right)
    float left_gain;                        // Left channel gain (0.0 to 1.0)
    float right_gain;                       // Right channel gain (0.0 to 1.0)
    
    // Stereo panoramization fields - Q24 (new)
    q24_t pan_position_q24;                 // Q24 [-Q24_ONE, +Q24_ONE]
    q24_t left_gain_q24;                    // Q24 [0, Q24_ONE]
    q24_t right_gain_q24;                   // Q24 [0, Q24_ONE]
} Q24_CACHE_ALIGN;

/* Exported constants --------------------------------------------------------*/
#define WAVEFORM_TABLE_SIZE        (10000000)

extern volatile struct waveParams wavesGeneratorParams;
extern volatile struct wave waves[MAX_NUMBER_OF_NOTES];
extern volatile float unitary_waveform[WAVEFORM_TABLE_SIZE];
extern volatile q24_t unitary_waveform_q24[WAVEFORM_TABLE_SIZE];

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
uint32_t init_waves(volatile float *unitary_waveform,
                    volatile struct wave *waves,
                    volatile struct waveParams *parameters);

/* Private defines -----------------------------------------------------------*/

#endif /* __WAVE_GENERATION_H */
