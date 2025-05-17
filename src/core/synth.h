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
#include "config.h" // For CIS_MAX_PIXELS_NB
#include "stdint.h"
#include "wave_generation.h"
#include <pthread.h> // For pthread_mutex_t

/* Private includes ----------------------------------------------------------*/

/* Synth Data Freeze Feature */
extern volatile int g_is_synth_data_frozen;
extern int32_t g_frozen_grayscale_buffer[CIS_MAX_PIXELS_NB];
extern volatile int g_is_synth_data_fading_out;
extern double g_synth_data_fade_start_time;
extern const double G_SYNTH_DATA_FADE_DURATION_SECONDS;
extern pthread_mutex_t g_synth_data_freeze_mutex;

void synth_data_freeze_init(void);
void synth_data_freeze_cleanup(void);

/* Buffers for display to reflect synth data (grayscale converted to RGB) */
extern uint8_t g_displayable_synth_R[CIS_MAX_PIXELS_NB];
extern uint8_t g_displayable_synth_G[CIS_MAX_PIXELS_NB];
extern uint8_t g_displayable_synth_B[CIS_MAX_PIXELS_NB];
extern pthread_mutex_t
    g_displayable_synth_mutex; // Mutex to protect these display buffers

void displayable_synth_buffers_init(void);
void displayable_synth_buffers_cleanup(void);
/* End Synth Data Freeze Feature */

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
int32_t synth_IfftInit(void);
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G,
                        uint8_t *buffer_B);
/* Private defines -----------------------------------------------------------*/

#endif /* __SYNTH_H */
