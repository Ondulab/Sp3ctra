/*
 * image_preprocessor.c
 *
 * Implementation of image preprocessing module for Sp3ctra
 *
 * Author: zhonx
 * Created: 2025-10-29
 */

#include "image_preprocessor.h"
#include "../../synthesis/additive/synth_additive_stereo.h"
#include "../../synthesis/additive/synth_additive_math.h"
#include "../../communication/dmx/dmx.h"
#include "../../config/config_loader.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

/* Private variables */
static int module_initialized = 0;

/* Private helper function prototypes */
static uint64_t get_timestamp_us(void);
static void preprocess_grayscale(const uint8_t *raw_r, const uint8_t *raw_g, 
                                  const uint8_t *raw_b, float *out_grayscale);
static void preprocess_stereo(const uint8_t *raw_r, const uint8_t *raw_g,
                               const uint8_t *raw_b, PreprocessedImageData *out);
static void preprocess_dmx(const uint8_t *raw_r, const uint8_t *raw_g,
                            const uint8_t *raw_b, PreprocessedImageData *out);

/* Module initialization */
void image_preprocess_init(void) {
    if (module_initialized) {
        return;
    }
    
    printf("[PREPROCESS] Image preprocessor module initialized\n");
    module_initialized = 1;
}

/* Module cleanup */
void image_preprocess_cleanup(void) {
    if (!module_initialized) {
        return;
    }
    
    printf("[PREPROCESS] Image preprocessor module cleaned up\n");
    module_initialized = 0;
}

/* Main preprocessing function */
int image_preprocess_frame(
    const uint8_t *raw_r,
    const uint8_t *raw_g,
    const uint8_t *raw_b,
    PreprocessedImageData *out
) {
    if (!module_initialized) {
        fprintf(stderr, "[PREPROCESS] ERROR: Module not initialized\n");
        return -1;
    }
    
    if (!raw_r || !raw_g || !raw_b || !out) {
        fprintf(stderr, "[PREPROCESS] ERROR: NULL pointer passed\n");
        return -1;
    }
    
    /* Get timestamp */
    out->timestamp_us = get_timestamp_us();
    
    /* 1. Convert RGB to grayscale (always needed) */
    preprocess_grayscale(raw_r, raw_g, raw_b, out->grayscale);
    
    /* 2. Calculate contrast factor (always needed) */
    out->contrast_factor = calculate_contrast(out->grayscale, CIS_MAX_PIXELS_NB);
    
    /* 3. Calculate stereo panning data (only if stereo enabled) */
    if (g_sp3ctra_config.stereo_mode_enabled) {
        preprocess_stereo(raw_r, raw_g, raw_b, out);
    }
    
    /* 4. Calculate DMX zone averages (only if DMX enabled) */
    #ifdef USE_DMX
    preprocess_dmx(raw_r, raw_g, raw_b, out);
    #endif
    
    return 0;
}

/* Private helper functions */

/**
 * @brief Get current timestamp in microseconds
 */
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/**
 * @brief Convert RGB to grayscale with normalization
 * This is the greyScale() function moved here from synthesis
 */
static void preprocess_grayscale(const uint8_t *raw_r, const uint8_t *raw_g, 
                                  const uint8_t *raw_b, float *out_grayscale) {
    for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
        // Standard grayscale conversion: 0.299*R + 0.587*G + 0.114*B
        float gray = (0.299f * raw_r[i] + 0.587f * raw_g[i] + 0.114f * raw_b[i]);
        
        // Normalize to [0.0, 1.0] range
        out_grayscale[i] = gray / 255.0f;
    }
}

/**
 * @brief Preprocess stereo panning data
 * Calculates color temperature and pan gains for each note
 */
static void preprocess_stereo(const uint8_t *raw_r, const uint8_t *raw_g,
                               const uint8_t *raw_b, PreprocessedImageData *out) {
    int num_notes = CIS_MAX_PIXELS_NB / g_sp3ctra_config.pixels_per_note;
    int pixels_per_note = g_sp3ctra_config.pixels_per_note;
    
    // Ensure we don't exceed array bounds
    if (num_notes > PREPROCESS_MAX_NOTES) {
        num_notes = PREPROCESS_MAX_NOTES;
    }
    
    for (int note = 0; note < num_notes; note++) {
        // Calculate average RGB for this note's pixels
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        uint32_t pixel_count = 0;
        
        for (int pix = 0; pix < pixels_per_note; pix++) {
            uint32_t pixel_idx = note * pixels_per_note + pix;
            if (pixel_idx < CIS_MAX_PIXELS_NB) {
                r_sum += raw_r[pixel_idx];
                g_sum += raw_g[pixel_idx];
                b_sum += raw_b[pixel_idx];
                pixel_count++;
            }
        }
        
        if (pixel_count > 0) {
            // Calculate average RGB values
            uint8_t r_avg = r_sum / pixel_count;
            uint8_t g_avg = g_sum / pixel_count;
            uint8_t b_avg = b_sum / pixel_count;
            
            // Calculate color temperature and pan position
            float temperature = calculate_color_temperature(r_avg, g_avg, b_avg);
            out->stereo.pan_positions[note] = temperature;
            
            // Calculate pan gains using constant power law
            calculate_pan_gains(temperature, 
                              &out->stereo.left_gains[note],
                              &out->stereo.right_gains[note]);
        } else {
            // Default to center if no pixels
            out->stereo.pan_positions[note] = 0.0f;
            out->stereo.left_gains[note] = 0.707f;
            out->stereo.right_gains[note] = 0.707f;
        }
    }
}

/**
 * @brief Preprocess DMX zone average colors
 * Calculates average RGB values for each DMX zone
 */
static void preprocess_dmx(const uint8_t *raw_r, const uint8_t *raw_g,
                            const uint8_t *raw_b, PreprocessedImageData *out) {
    #ifdef USE_DMX
    // Calculate pixels per zone
    int pixels_per_zone = CIS_MAX_PIXELS_NB / DMX_NUM_SPOTS;
    
    for (int zone = 0; zone < DMX_NUM_SPOTS; zone++) {
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        uint32_t pixel_count = 0;
        
        // Calculate start and end pixel indices for this zone
        int start_pixel = zone * pixels_per_zone;
        int end_pixel = (zone == DMX_NUM_SPOTS - 1) ? CIS_MAX_PIXELS_NB : start_pixel + pixels_per_zone;
        
        // Sum RGB values across zone
        for (int pix = start_pixel; pix < end_pixel && pix < CIS_MAX_PIXELS_NB; pix++) {
            r_sum += raw_r[pix];
            g_sum += raw_g[pix];
            b_sum += raw_b[pix];
            pixel_count++;
        }
        
        // Calculate average
        if (pixel_count > 0) {
            out->dmx.zone_r[zone] = r_sum / pixel_count;
            out->dmx.zone_g[zone] = g_sum / pixel_count;
            out->dmx.zone_b[zone] = b_sum / pixel_count;
        } else {
            out->dmx.zone_r[zone] = 0;
            out->dmx.zone_g[zone] = 0;
            out->dmx.zone_b[zone] = 0;
        }
    }
    #endif
}
