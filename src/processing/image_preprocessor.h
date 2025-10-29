/*
 * image_preprocessor.h
 *
 * Image preprocessing module for Sp3ctra
 * Responsible for transforming raw RGB data into synthesis-ready data
 * 
 * This module is called by the UDP reception thread to precompute all
 * necessary data before passing it to the audio synthesis thread.
 *
 * Author: zhonx
 * Created: 2025-10-29
 */

#ifndef IMAGE_PREPROCESSOR_H
#define IMAGE_PREPROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include "../../core/config.h"
#include "../../config/config_instrument.h"
#include "../../config/config_synth_additive.h"
#include "../../config/config_dmx.h"

/* Maximum number of notes (conservative estimate for static allocation) */
/* Based on CIS_MAX_PIXELS_NB / minimum_pixels_per_note */
/* Actual runtime value is obtained via get_current_number_of_notes() */
#define PREPROCESS_MAX_NOTES (CIS_MAX_PIXELS_NB / 2)  /* Conservative: assumes min 2 pixels per note */

/* Preprocessed image data structure */
typedef struct {
    /* Grayscale data normalized for additive synthesis [0.0, 1.0] */
    float grayscale[CIS_MAX_PIXELS_NB];
    
    /* Computed contrast factor [0.0, 1.0] */
    float contrast_factor;
    
    /* Stereo panning data (only used if stereo mode enabled) */
    struct {
        float pan_positions[PREPROCESS_MAX_NOTES];  /* -1.0 (left) to +1.0 (right) */
        float left_gains[PREPROCESS_MAX_NOTES];     /* 0.0 to 1.0 */
        float right_gains[PREPROCESS_MAX_NOTES];    /* 0.0 to 1.0 */
    } stereo;
    
    /* DMX zone average colors (only used if DMX enabled) */
    struct {
        uint8_t zone_r[DMX_NUM_SPOTS];
        uint8_t zone_g[DMX_NUM_SPOTS];
        uint8_t zone_b[DMX_NUM_SPOTS];
    } dmx;
    
    /* Timestamp for synchronization (microseconds) */
    uint64_t timestamp_us;
    
} PreprocessedImageData;

/* Module initialization and cleanup */
void image_preprocess_init(void);
void image_preprocess_cleanup(void);

/* Main preprocessing function
 * Transforms raw RGB image data into synthesis-ready preprocessed data
 * 
 * Parameters:
 *   raw_r, raw_g, raw_b: Raw RGB buffers (0-255)
 *   out: Output structure to fill with preprocessed data
 * 
 * Returns:
 *   0 on success, -1 on error
 */
int image_preprocess_frame(
    const uint8_t *raw_r,
    const uint8_t *raw_g,
    const uint8_t *raw_b,
    PreprocessedImageData *out
);

#endif /* IMAGE_PREPROCESSOR_H */
