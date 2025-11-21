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
 * Refactored: 2025-11-21 - Separated buffers per synthesis engine
 */

#ifndef IMAGE_PREPROCESSOR_H
#define IMAGE_PREPROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include "../../core/config.h"
#include "../../config/config_instrument.h"
#include "../../config/config_synth_additive.h"
#include "../../config/config_dmx.h"

/* Maximum number of notes (static allocation) */
/* Must handle worst case: 1 pixel per note = CIS_MAX_PIXELS_NB notes */
/* Actual runtime value is obtained via get_current_number_of_notes() */
#define PREPROCESS_MAX_NOTES CIS_MAX_PIXELS_NB  /* Maximum: 1 pixel per note */

/* Preprocessed image data structure - REFACTORED with separated buffers per synthesis */
typedef struct {
    /* ADDITIVE SYNTHESIS - Complete pipeline with gamma correction */
    struct {
        float grayscale[CIS_MAX_PIXELS_NB];     /* Per-pixel grayscale [0.0, 1.0] after all processing */
        float notes[PREPROCESS_MAX_NOTES];      /* Per-note averaged values [0.0, 1.0] */
        float contrast_factor;                  /* Computed contrast factor [0.0, 1.0] */
    } additive;
    
    /* POLYPHONIC SYNTHESIS - Linear response for FFT (no gamma) */
    struct {
        float grayscale[CIS_MAX_PIXELS_NB];     /* Per-pixel grayscale [0.0, 1.0] for FFT input */
        float magnitudes[128];                  /* Pre-computed smoothed FFT magnitudes (MAX_MAPPED_OSCILLATORS) */
        int valid;                              /* 1 if FFT data is valid, 0 otherwise */
    } polyphonic;
    
    /* PHOTOWAVE SYNTHESIS - Native RGB for waveform sampling */
    struct {
        uint8_t r[CIS_MAX_PIXELS_NB];          /* Red channel [0-255] */
        uint8_t g[CIS_MAX_PIXELS_NB];          /* Green channel [0-255] */
        uint8_t b[CIS_MAX_PIXELS_NB];          /* Blue channel [0-255] */
    } photowave;
    
    /* Stereo panning data (shared, only used if stereo mode enabled) */
    struct {
        float pan_positions[PREPROCESS_MAX_NOTES];  /* -1.0 (left) to +1.0 (right) */
        float left_gains[PREPROCESS_MAX_NOTES];     /* 0.0 to 1.0 */
        float right_gains[PREPROCESS_MAX_NOTES];    /* 0.0 to 1.0 */
    } stereo;
    
    /* DMX zone average colors (shared, only used if DMX enabled) */
#ifdef USE_DMX
    struct {
        uint8_t zone_r[DMX_NUM_SPOTS];
        uint8_t zone_g[DMX_NUM_SPOTS];
        uint8_t zone_b[DMX_NUM_SPOTS];
    } dmx;
#endif
    
    /* Timestamp for synchronization (microseconds) */
    uint64_t timestamp_us;
    
} PreprocessedImageData;

/* Module initialization and cleanup */
void image_preprocess_init(void);
void image_preprocess_cleanup(void);

/* Main preprocessing function
 * Transforms raw RGB image data into synthesis-ready preprocessed data
 * Calls all specialized preprocessing functions for each synthesis engine
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

/* Specialized preprocessing functions (called internally by image_preprocess_frame) */

/* Additive synthesis preprocessing
 * Pipeline: RGB → Grayscale → Inversion (optional) → Gamma → Averaging → Contrast
 */
void preprocess_additive(
    const uint8_t *raw_r,
    const uint8_t *raw_g,
    const uint8_t *raw_b,
    PreprocessedImageData *out
);

/* Polyphonic synthesis preprocessing
 * Pipeline: RGB → Grayscale → Inversion (optional) → FFT (no gamma for linear response)
 */
void preprocess_polyphonic(
    const uint8_t *raw_r,
    const uint8_t *raw_g,
    const uint8_t *raw_b,
    PreprocessedImageData *out
);

/* Photowave synthesis preprocessing
 * Pipeline: Direct RGB copy (native sampling, no conversion)
 */
void preprocess_photowave(
    const uint8_t *raw_r,
    const uint8_t *raw_g,
    const uint8_t *raw_b,
    PreprocessedImageData *out
);

/* FFT preprocessing function for polyphonic synthesis
 * Computes FFT magnitudes from grayscale data with temporal smoothing
 * 
 * Parameters:
 *   data: PreprocessedImageData structure with polyphonic.grayscale already computed
 * 
 * Returns:
 *   0 on success, -1 on error
 * 
 * Note: This function maintains internal state for temporal smoothing
 */
#ifndef DISABLE_POLYPHONIC
int image_preprocess_fft(PreprocessedImageData *data);
#endif

#endif /* IMAGE_PREPROCESSOR_H */
