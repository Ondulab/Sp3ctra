/*
 * synth.h
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SYNTH_H
#define __SYNTH_H

/* Includes ------------------------------------------------------------------*/
#include "stdint.h"
#include "wave_generation.h"

/* Private includes ----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
int32_t synth_IfftInit(void);
int32_t synth_GetImageData(uint32_t index);
int32_t synth_SetImageData(uint32_t index, int32_t value);
void synth_AudioProcess(int32_t *imageData, float *audio_samples);

/* Private defines -----------------------------------------------------------*/

#endif /* __SYNTH_H */
