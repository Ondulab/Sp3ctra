/*
 * image_preprocessor.c
 *
 * Implementation of image preprocessing module for Sp3ctra
 *
 * Author: zhonx
 * Created: 2025-10-29
 */

#include "image_preprocessor.h"
#include "../../config/config_instrument.h"
#include "../../synthesis/additive/synth_additive_stereo.h"
#include "../../synthesis/additive/synth_additive_math.h"
#include "../../communication/dmx/dmx.h"
#include "../../config/config_loader.h"
#include "../../utils/logger.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#ifndef DISABLE_POLYPHONIC
#include "../../synthesis/polyphonic/kissfft/kiss_fftr.h"

/* FFT temporal smoothing configuration */
#define FFT_HISTORY_SIZE 5  /* 5ms @ 1kHz - good compromise for bass stability */
#define AMPLITUDE_SMOOTHING_ALPHA 0.1f  /* Exponential smoothing factor */
#define MAX_FFT_BINS 128  /* Must match MAX_MAPPED_OSCILLATORS */

/* Normalization factors from original polyphonic implementation */
#define NORM_FACTOR_BIN0 (881280.0f * 1.1f)
#define NORM_FACTOR_HARMONICS (220320.0f * 2.0f)

/* Private state for FFT temporal smoothing */
static struct {
    float history[FFT_HISTORY_SIZE][MAX_FFT_BINS];  /* Circular buffer for magnitude history */
    int write_index;  /* Current write position in circular buffer */
    int fill_count;   /* Number of valid frames in history (0 to FFT_HISTORY_SIZE) */
    int initialized;  /* 1 if FFT state is initialized, 0 otherwise */
    kiss_fftr_cfg fft_cfg;  /* KissFFT configuration (lazy init) */
    kiss_fft_scalar *fft_input;  /* FFT input buffer (lazy alloc) */
    kiss_fft_cpx *fft_output;    /* FFT output buffer (lazy alloc) */
} fft_history_state = {0};
#endif

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
    
    log_info("PREPROCESS", "Image preprocessor module initialized");
    module_initialized = 1;
}

/* Module cleanup */
void image_preprocess_cleanup(void) {
    if (!module_initialized) {
        return;
    }
    
    log_info("PREPROCESS", "Image preprocessor module cleaned up");
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
        log_error("PREPROCESS", "Module not initialized");
        return -1;
    }
    
    if (!raw_r || !raw_g || !raw_b || !out) {
        log_error("PREPROCESS", "NULL pointer passed");
        return -1;
    }
    
    /* Get timestamp */
    out->timestamp_us = get_timestamp_us();
    
    /* 1. Convert RGB to grayscale (always needed) */
    preprocess_grayscale(raw_r, raw_g, raw_b, out->grayscale);
    
    /* 2. Calculate contrast factor (always needed) */
    out->contrast_factor = calculate_contrast(out->grayscale, get_cis_pixels_nb());
    
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
    int nb_pixels;
    int i;
    
    nb_pixels = get_cis_pixels_nb();
    
    for (i = 0; i < nb_pixels; i++) {
        /* Standard grayscale conversion: 0.299*R + 0.587*G + 0.114*B */
        float gray = (0.299f * raw_r[i] + 0.587f * raw_g[i] + 0.114f * raw_b[i]);
        
        /* Normalize to [0.0, 1.0] range */
        out_grayscale[i] = gray / 255.0f;
    }
}

/**
 * @brief Preprocess stereo panning data
 * Calculates color temperature and pan gains for each note
 */
static void preprocess_stereo(const uint8_t *raw_r, const uint8_t *raw_g,
                               const uint8_t *raw_b, PreprocessedImageData *out) {
    int nb_pixels;
    int num_notes;
    int pixels_per_note;
    int note;
    
    nb_pixels = get_cis_pixels_nb();
    num_notes = nb_pixels / g_sp3ctra_config.pixels_per_note;
    pixels_per_note = g_sp3ctra_config.pixels_per_note;
    
    /* Ensure we don't exceed array bounds */
    if (num_notes > PREPROCESS_MAX_NOTES) {
        num_notes = PREPROCESS_MAX_NOTES;
    }
    
    for (note = 0; note < num_notes; note++) {
        /* Calculate average RGB for this note's pixels */
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        uint32_t pixel_count = 0;
        int pix;
        
        for (pix = 0; pix < pixels_per_note; pix++) {
            uint32_t pixel_idx = note * pixels_per_note + pix;
            if (pixel_idx < (uint32_t)nb_pixels) {
                r_sum += raw_r[pixel_idx];
                g_sum += raw_g[pixel_idx];
                b_sum += raw_b[pixel_idx];
                pixel_count++;
            }
        }
        
        if (pixel_count > 0) {
            /* Calculate average RGB values */
            uint8_t r_avg = r_sum / pixel_count;
            uint8_t g_avg = g_sum / pixel_count;
            uint8_t b_avg = b_sum / pixel_count;
            
            /* Calculate color temperature and pan position */
            float temperature = calculate_color_temperature(r_avg, g_avg, b_avg);
            out->stereo.pan_positions[note] = temperature;
            
            /* Calculate pan gains using constant power law */
            calculate_pan_gains(temperature, 
                              &out->stereo.left_gains[note],
                              &out->stereo.right_gains[note]);
        } else {
            /* Default to center if no pixels */
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
    int nb_pixels;
    int pixels_per_zone;
    int zone;
    
    nb_pixels = get_cis_pixels_nb();
    
    /* Calculate pixels per zone */
    pixels_per_zone = nb_pixels / DMX_NUM_SPOTS;
    
    for (zone = 0; zone < DMX_NUM_SPOTS; zone++) {
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        uint32_t pixel_count = 0;
        int start_pixel;
        int end_pixel;
        int pix;
        
        /* Calculate start and end pixel indices for this zone */
        start_pixel = zone * pixels_per_zone;
        end_pixel = (zone == DMX_NUM_SPOTS - 1) ? nb_pixels : start_pixel + pixels_per_zone;
        
        /* Sum RGB values across zone */
        for (pix = start_pixel; pix < end_pixel && pix < nb_pixels; pix++) {
            r_sum += raw_r[pix];
            g_sum += raw_g[pix];
            b_sum += raw_b[pix];
            pixel_count++;
        }
        
        /* Calculate average */
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

#ifndef DISABLE_POLYPHONIC
/**
 * @brief FFT preprocessing function for polyphonic synthesis
 * Computes FFT magnitudes from grayscale data with temporal smoothing
 * 
 * Architecture:
 * - Lazy initialization of KissFFT on first call
 * - Converts grayscale [0.0-1.0] to FFT input [0-255]
 * - Computes FFT using KissFFT
 * - Calculates and normalizes magnitudes
 * - Applies temporal smoothing via circular buffer (5 frames @ 1kHz = 5ms)
 * - Applies exponential smoothing for additional stability
 * 
 * @param data PreprocessedImageData with grayscale already computed
 * @return 0 on success, -1 on error
 */
int image_preprocess_fft(PreprocessedImageData *data) {
    int nb_pixels;
    int i, h, idx;
    float real, imag, magnitude, target_mag;
    float sum, averaged;
    
    if (!data) {
        log_error("PREPROCESS", "FFT: NULL data pointer");
        return -1;
    }
    
    nb_pixels = get_cis_pixels_nb();
    
    /* Lazy initialization of FFT state */
    if (!fft_history_state.initialized) {
        log_info("PREPROCESS", "FFT: Initializing KissFFT for %d pixels", nb_pixels);
        
        /* Allocate FFT buffers */
        fft_history_state.fft_input = (kiss_fft_scalar *)calloc(nb_pixels, sizeof(kiss_fft_scalar));
        fft_history_state.fft_output = (kiss_fft_cpx *)calloc(nb_pixels / 2 + 1, sizeof(kiss_fft_cpx));
        
        if (!fft_history_state.fft_input || !fft_history_state.fft_output) {
            log_error("PREPROCESS", "FFT: Failed to allocate FFT buffers");
            if (fft_history_state.fft_input) free(fft_history_state.fft_input);
            if (fft_history_state.fft_output) free(fft_history_state.fft_output);
            return -1;
        }
        
        /* Initialize KissFFT configuration */
        fft_history_state.fft_cfg = kiss_fftr_alloc(nb_pixels, 0, NULL, NULL);
        if (!fft_history_state.fft_cfg) {
            log_error("PREPROCESS", "FFT: Failed to initialize KissFFT configuration");
            free(fft_history_state.fft_input);
            free(fft_history_state.fft_output);
            return -1;
        }
        
        /* Pre-fill history buffer with white spectrum (all bins = 1.0) */
        /* This prevents transients at startup */
        for (h = 0; h < FFT_HISTORY_SIZE; h++) {
            for (i = 0; i < MAX_FFT_BINS; i++) {
                fft_history_state.history[h][i] = 1.0f;
            }
        }
        
        fft_history_state.write_index = 0;
        fft_history_state.fill_count = FFT_HISTORY_SIZE;  /* History pre-filled */
        fft_history_state.initialized = 1;
        
        log_info("PREPROCESS", "FFT: Initialized with %d-frame temporal smoothing (%.1fms @ 1kHz)", 
                 FFT_HISTORY_SIZE, FFT_HISTORY_SIZE * 1.0f);
    }
    
    /* Convert grayscale [0.0-1.0] to FFT input [0-255] */
    for (i = 0; i < nb_pixels; i++) {
        fft_history_state.fft_input[i] = data->grayscale[i] * 255.0f;
    }
    
    /* Compute FFT */
    kiss_fftr(fft_history_state.fft_cfg, fft_history_state.fft_input, fft_history_state.fft_output);
    
    /* Calculate raw magnitudes and store in circular buffer */
    /* Bin 0 (DC component) uses different normalization */
    magnitude = fft_history_state.fft_output[0].r / NORM_FACTOR_BIN0;
    fft_history_state.history[fft_history_state.write_index][0] = magnitude;
    
    /* Bins 1 to MAX_FFT_BINS-1 (harmonics) */
    for (i = 1; i < MAX_FFT_BINS && i < (nb_pixels / 2 + 1); i++) {
        real = fft_history_state.fft_output[i].r;
        imag = fft_history_state.fft_output[i].i;
        magnitude = sqrtf(real * real + imag * imag);
        target_mag = fminf(1.0f, magnitude / NORM_FACTOR_HARMONICS);
        fft_history_state.history[fft_history_state.write_index][i] = target_mag;
    }
    
    /* Fill remaining bins with zero if nb_pixels is small */
    for (i = (nb_pixels / 2 + 1); i < MAX_FFT_BINS; i++) {
        fft_history_state.history[fft_history_state.write_index][i] = 0.0f;
    }
    
    /* Update circular buffer indices */
    fft_history_state.write_index = (fft_history_state.write_index + 1) % FFT_HISTORY_SIZE;
    if (fft_history_state.fill_count < FFT_HISTORY_SIZE) {
        fft_history_state.fill_count++;
    }
    
    /* Calculate moving average over history buffer */
    for (i = 0; i < MAX_FFT_BINS; i++) {
        sum = 0.0f;
        for (h = 0; h < fft_history_state.fill_count; h++) {
            idx = (fft_history_state.write_index - 1 - h + FFT_HISTORY_SIZE) % FFT_HISTORY_SIZE;
            sum += fft_history_state.history[idx][i];
        }
        averaged = sum / fft_history_state.fill_count;
        
        /* Apply exponential smoothing on top of moving average */
        /* This provides additional temporal stability */
        data->fft.magnitudes[i] = 
            AMPLITUDE_SMOOTHING_ALPHA * averaged +
            (1.0f - AMPLITUDE_SMOOTHING_ALPHA) * data->fft.magnitudes[i];
    }
    
    /* Mark FFT data as valid */
    data->fft.valid = 1;
    
    return 0;
}
#endif
