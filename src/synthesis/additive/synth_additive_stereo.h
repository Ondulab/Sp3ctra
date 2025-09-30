/*
 * synth_additive_stereo.h
 *
 * Stereo processing and color temperature analysis for additive synthesis
 * Contains functions for panoramization, contrast calculation, and color analysis
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_STEREO_H__
#define __SYNTH_ADDITIVE_STEREO_H__

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stddef.h>

/* Exported function prototypes ----------------------------------------------*/

/* Contrast and image analysis */
float calculate_contrast(float *imageData, size_t size);

/* Color temperature and stereo panning */
float calculate_color_temperature(uint8_t r, uint8_t g, uint8_t b);
void calculate_pan_gains(float pan_position, float *left_gain, float *right_gain);

#endif /* __SYNTH_ADDITIVE_STEREO_H__ */
