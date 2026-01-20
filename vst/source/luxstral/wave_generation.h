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
#include "vst_adapters_c.h"
#include "synth_luxstral.h"
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
    
    // Index and size parameters (shared between float and Q24)
    uint32_t current_idx;
    uint32_t area_size;
    uint32_t octave_coeff;
    uint32_t octave_divider;
    
    // Volume parameters - Float32
    float target_volume;
    float current_volume;
    
    // GAP_LIMITER: Precomputed envelope coefficients (RT-optimized)
    float alpha_up;                         // Attack coefficient (precomputed)
    float alpha_down_weighted;              // Release coefficient with frequency weighting (precomputed)
    
    
    // Frequency (keep as float for initialization calculations)
    float frequency;
    
    // Stereo panoramization fields - Float32 (legacy)
    float pan_position;                     // Pan position: -1.0 (left) to +1.0 (right)
    float left_gain;                        // Left channel gain (0.0 to 1.0)
    float right_gain;                       // Right channel gain (0.0 to 1.0)
    
};

/* Exported constants --------------------------------------------------------*/
#define WAVEFORM_TABLE_SIZE        (10000000)

extern volatile struct waveParams wavesGeneratorParams;
extern volatile struct wave *waves;  // Now a pointer to dynamically allocated array
extern volatile float *unitary_waveform;  // Now a pointer to dynamically allocated array

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
uint32_t init_waves(volatile float *unitary_waveform,
                    volatile struct wave *waves,
                    volatile struct waveParams *parameters);

/**************************************************************************************
 * Hot-reload frequency range API
 * Thread-safe mechanism for changing frequency range at runtime
 **************************************************************************************/

/**
 * @brief Request frequency range reinit from UI thread
 * Sets atomic flag that will be processed by synth thread
 * @note Thread-safe: can be called from JUCE message thread
 */
void request_frequency_reinit(void);

/**
 * @brief Check and process pending frequency reinit
 * Should be called at the beginning of synth_IfftMode() BEFORE workers start
 * @return 1 if reinit was performed, 0 otherwise
 * @note Thread-safe: called from synth thread while workers are waiting
 */
int check_and_process_frequency_reinit(void);

/**
 * @brief Check if frequency reinit is currently in progress (fading out)
 * @return 1 if fading out, 0 otherwise
 */
int is_frequency_reinit_fading_out(void);

/**************************************************************************************
 * Global fade API for smooth transitions
 * Applied to entire output signal, not individual oscillators
 **************************************************************************************/

/**
 * @brief Get and update global fade factor for a single sample
 * Call this for each output sample to apply smooth exponential fade
 * @return Current fade factor (0.0 to 1.0)
 * @note Call once per sample for smooth per-sample fade
 */
float get_global_fade_factor_and_update(void);

/**
 * @brief Get current global fade factor without updating
 * @return Current fade factor (0.0 to 1.0)
 */
float get_global_fade_factor(void);

/* Private defines -----------------------------------------------------------*/

#endif /* __WAVE_GENERATION_H */
