/*
 * synth_additive_stereo.h
 *
 * Stereo processing and color temperature analysis for additive synthesis
 * Contains functions for color temperature analysis and stereo panning
 *
 * Note: calculate_contrast() has been moved to image_preprocessor.c
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_STEREO_H__
#define __SYNTH_ADDITIVE_STEREO_H__

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stddef.h>

/* Exported function prototypes ----------------------------------------------*/

/* Color temperature and stereo panning */
float calculate_color_temperature(uint8_t r, uint8_t g, uint8_t b);
void calculate_pan_gains(float pan_position, float *left_gain, float *right_gain);

#endif /* __SYNTH_ADDITIVE_STEREO_H__ */
