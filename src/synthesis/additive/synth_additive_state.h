/*
 * synth_additive_state.h
 *
 * State management for additive synthesis
 * Contains freeze/fade functionality and display buffer management
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_STATE_H__
#define __SYNTH_ADDITIVE_STATE_H__

/* Includes ------------------------------------------------------------------*/
#include "../../core/config.h"
#include <stdint.h>
#include <pthread.h>

/* Exported variables --------------------------------------------------------*/

/* Synth Data Freeze Feature */
extern volatile int g_is_synth_data_frozen;
extern float g_frozen_grayscale_buffer[CIS_MAX_PIXELS_NB];
extern volatile int g_is_synth_data_fading_out;
extern double g_synth_data_fade_start_time;
extern const double G_SYNTH_DATA_FADE_DURATION_SECONDS;
extern pthread_mutex_t g_synth_data_freeze_mutex;

/* Buffers for display to reflect synth data (grayscale converted to RGB) */
extern uint8_t g_displayable_synth_R[CIS_MAX_PIXELS_NB];
extern uint8_t g_displayable_synth_G[CIS_MAX_PIXELS_NB];
extern uint8_t g_displayable_synth_B[CIS_MAX_PIXELS_NB];
extern pthread_mutex_t g_displayable_synth_mutex;

/* Exported function prototypes ----------------------------------------------*/

/* Synth data freeze/fade management */
void synth_data_freeze_init(void);
void synth_data_freeze_cleanup(void);
double synth_getCurrentTimeInSeconds(void);

/* Display buffer management */
void displayable_synth_buffers_init(void);
void displayable_synth_buffers_cleanup(void);

#endif /* __SYNTH_ADDITIVE_STATE_H__ */
