/*
 * synth_luxstral_state.h
 *
 * State management for additive synthesis
 * Contains freeze/fade functionality and display buffer management
 *
 * Author: zhonx
 */

#ifndef __SYNTH_LUXSTRAL_STATE_H__
#define __SYNTH_LUXSTRAL_STATE_H__

/* Includes ------------------------------------------------------------------*/
#include "vst_adapters_c.h"
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported variables --------------------------------------------------------*/

/* Synth Data Freeze Feature */
extern volatile int g_is_synth_data_frozen;
extern float *g_frozen_grayscale_buffer;  // Dynamic allocation
extern volatile int g_is_synth_data_fading_out;
extern double g_synth_data_fade_start_time;
extern const double G_SYNTH_DATA_FADE_DURATION_SECONDS;
extern pthread_mutex_t g_synth_data_freeze_mutex;

/* Buffers for display to reflect synth data (grayscale converted to RGB) */
extern uint8_t *g_displayable_synth_R;  // Dynamic allocation
extern uint8_t *g_displayable_synth_G;  // Dynamic allocation
extern uint8_t *g_displayable_synth_B;  // Dynamic allocation
extern pthread_mutex_t g_displayable_synth_mutex;

/* Exported function prototypes ----------------------------------------------*/

/* Synth data freeze/fade management */
void synth_data_freeze_init(void);
void synth_data_freeze_cleanup(void);
double synth_getCurrentTimeInSeconds(void);

/* Display buffer management */
void displayable_synth_buffers_init(void);
void displayable_synth_buffers_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYNTH_LUXSTRAL_STATE_H__ */
