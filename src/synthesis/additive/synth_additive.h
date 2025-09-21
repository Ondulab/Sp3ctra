/*
 * synth_additive.h
 *
 * Main header for additive synthesis engine
 * Includes all modular components of the refactored additive synthesis system
 *
 * Author: zhonx
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SYNTH_ADDITIVE_H
#define __SYNTH_ADDITIVE_H

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_math.h"
#include "synth_additive_stereo.h"
#include "synth_additive_state.h"
#include "synth_additive_threading.h"
#include "synth_additive_algorithms.h"

/* Core API function declarations -------------------------------------------*/

/**
 * @brief Initialize the additive synthesis engine
 * @return 0 on success, -1 on failure
 */
int32_t synth_IfftInit(void);

/**
 * @brief Process audio buffers for RGB channels
 * @param buffer_R Red channel buffer
 * @param buffer_G Green channel buffer  
 * @param buffer_B Blue channel buffer
 */
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B);

/**
 * @brief Main synthesis processing function
 * @param imageData Input image data array
 * @param audioLeft Output left channel audio buffer
 * @param audioRight Output right channel audio buffer
 * @param contrast Contrast adjustment parameter
 */
void synth_IfftMode(int32_t *imageData, float *audioLeft, float *audioRight, float contrast);

#endif /* __SYNTH_ADDITIVE_H */
