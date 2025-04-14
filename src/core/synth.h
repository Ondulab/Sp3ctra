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
void synth_AudioProcess(uint8_t *buffer_R,
                        uint8_t *buffer_G,
                        uint8_t *buffer_B);
/* Private defines -----------------------------------------------------------*/

#endif /* __SYNTH_H */
