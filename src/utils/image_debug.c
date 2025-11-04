/* image_debug.c */

#include "image_debug.h"
#include "logger.h"
#include "config_debug.h"
#include "config_audio.h"
#include "../synthesis/additive/wave_generation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

// Include stb_image_write for PNG output (header-only library)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/**************************************************************************************
 * Internal State and Definitions
 **************************************************************************************/

static int debug_initialized = 0;
static int debug_image_runtime_enabled = 0; // Runtime control for image debug
static int raw_scanner_runtime_enabled = 0; // Runtime control for raw scanner capture
static int raw_scanner_capture_lines = 1000; // Default number of lines to capture
static int oscillator_runtime_enabled = 0; // Runtime control for oscillator capture
static int oscillator_capture_samples = MAX_SAMPLING_FREQUENCY; // Default number of samples to capture (1 second at max freq)
static int oscillator_markers_enabled = 0; // Runtime control for oscillator markers
static char output_dir[256];

// Temporal scan buffer structure
typedef struct {
    uint8_t *buffer;          // Scan image buffer (8-bit RGB)
    int width;                // Width of each line
    int current_height;       // Current number of lines
    int max_height;           // Maximum height
    char name[32];            // Scan type name
    int initialized;          // Is this scan initialized?
} temporal_scan_t;

/**************************************************************************************
 * Internal Helper Functions
 **************************************************************************************/

/**
 * @brief Generate timestamp string for filenames
 * @param buffer Output buffer for timestamp
 * @param buffer_size Size of output buffer
 * @retval None
 */
static void get_timestamp_string(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, buffer_size, "%Y%m%d_%H%M%S", tm_info);
}

/**
 * @brief Convert HSL to RGB color space
 * @param h Hue (0.0 to 360.0)
 * @param s Saturation (0.0 to 1.0)
 * @param l Lightness (0.0 to 1.0)
 * @param r Output red component (0-255)
 * @param g Output green component (0-255)
 * @param b Output blue component (0-255)
 * @retval None
 */
static void hsl_to_rgb(float h, float s, float l, uint8_t *r, uint8_t *g, uint8_t *b) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    
    float r_prime, g_prime, b_prime;
    
    if (h >= 0.0f && h < 60.0f) {
        r_prime = c; g_prime = x; b_prime = 0.0f;
    } else if (h >= 60.0f && h < 120.0f) {
        r_prime = x; g_prime = c; b_prime = 0.0f;
    } else if (h >= 120.0f && h < 180.0f) {
        r_prime = 0.0f; g_prime = c; b_prime = x;
    } else if (h >= 180.0f && h < 240.0f) {
        r_prime = 0.0f; g_prime = x; b_prime = c;
    } else if (h >= 240.0f && h < 300.0f) {
        r_prime = x; g_prime = 0.0f; b_prime = c;
    } else {
        r_prime = c; g_prime = 0.0f; b_prime = x;
    }
    
    *r = (uint8_t)((r_prime + m) * 255.0f);
    *g = (uint8_t)((g_prime + m) * 255.0f);
    *b = (uint8_t)((b_prime + m) * 255.0f);
}

/**
 * @brief Calculate color based on volume difference and absolute level
 * @param current_volume Current oscillator volume
 * @param target_volume Target oscillator volume
 * @param max_volume Maximum volume for normalization
 * @param r Output red component (0-255)
 * @param g Output green component (0-255)
 * @param b Output blue component (0-255)
 * @retval None
 */
static void calculate_oscillator_color(float current_volume, float target_volume, float max_volume,
                                     uint8_t *r, uint8_t *g, uint8_t *b) {
    // Calculate absolute difference between current and target
    float volume_diff = fabsf(current_volume - target_volume);
    float max_diff = max_volume; // Maximum possible difference
    
    // Normalize difference (0.0 = close, 1.0 = far)
    float diff_normalized = (max_diff > 0.0f) ? (volume_diff / max_diff) : 0.0f;
    if (diff_normalized > 1.0f) diff_normalized = 1.0f;
    
    // Calculate hue: blue (240°) when far, yellow/orange (60°) when close
    float hue = 180.0f - (diff_normalized * 240.0f); // 240° -> 0°
    
    // High saturation for vivid colors
    float saturation = 1.0f;
    
    // Lightness based on absolute current volume level
    // Low volume = high lightness (white), high volume = low lightness (black)
    float volume_normalized = (max_volume > 0.0f) ? (current_volume / max_volume) : 0.0f;
    if (volume_normalized > 1.0f) volume_normalized = 1.0f;
    
    // Invert: low volume = bright (0.8), high volume = dark (0.2)
    float lightness = 1.0f - (volume_normalized * 1.0f);
    
    // Convert HSL to RGB
    hsl_to_rgb(hue, saturation, lightness, r, g, b);
}

/**************************************************************************************
 * Public API Implementation
 **************************************************************************************/

int image_debug_init(void) {
    if (debug_initialized) {
        return 0; // Already initialized
    }
    
    // Set up output directory (always use "./debug_images")
    strncpy(output_dir, "./debug_images", sizeof(output_dir) - 1);
    output_dir[sizeof(output_dir) - 1] = '\0';
    
    // Create output directory (simple implementation)
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) {
        if (mkdir(output_dir, 0755) != 0) {
            log_error("IMG_DEBUG", "Failed to create debug image directory: %s", output_dir);
            return -1;
        }
    }
    
    debug_initialized = 1;
    
    log_info("IMG_DEBUG", "Initialized, output directory: %s", output_dir);
    return 0;
}

void image_debug_cleanup(void) {
    debug_initialized = 0;
    log_info("IMG_DEBUG", "Cleanup completed");
}

void image_debug_enable_runtime(int enable) {
    debug_image_runtime_enabled = enable;
    if (enable) {
        log_info("IMG_DEBUG", "Runtime debug enabled");
        // Initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
    } else {
        log_info("IMG_DEBUG", "Runtime debug disabled");
    }
}

int image_debug_is_enabled(void) {
    return debug_image_runtime_enabled;
}

/**************************************************************************************
 * Raw Scanner Capture Functions
 **************************************************************************************/

// Global raw scanner capture structure
static temporal_scan_t raw_scanner_capture = {0};
static int raw_scanner_initialized = 0;

/**
 * @brief Initialize raw scanner capture buffer
 * @retval 0 on success, -1 on error
 */
static int init_raw_scanner_capture(void) {
    if (raw_scanner_initialized) {
        return 0;
    }
    
    raw_scanner_capture.width = CIS_MAX_PIXELS_NB;
    raw_scanner_capture.max_height = raw_scanner_capture_lines;
    raw_scanner_capture.current_height = 0;
    raw_scanner_capture.initialized = 0;
    strncpy(raw_scanner_capture.name, "raw_scanner", sizeof(raw_scanner_capture.name) - 1);
    raw_scanner_capture.name[sizeof(raw_scanner_capture.name) - 1] = '\0';
    
    // Allocate buffer (RGB format for raw scanner data)
    raw_scanner_capture.buffer = calloc(raw_scanner_capture.width * raw_scanner_capture.max_height * 3, sizeof(uint8_t));
    if (!raw_scanner_capture.buffer) {
        log_error("IMG_DEBUG", "Failed to allocate raw scanner capture buffer");
        return -1;
    }
    
    raw_scanner_initialized = 1;
    
    log_info("IMG_DEBUG", "RAW_SCANNER: Initialized buffer (%dx%d lines)", 
             CIS_MAX_PIXELS_NB, raw_scanner_capture_lines);
    return 0;
}

int image_debug_capture_raw_scanner_line(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B) {
    // Check if raw scanner capture is enabled at runtime
    if (!raw_scanner_runtime_enabled || !debug_initialized || !debug_image_runtime_enabled) {
        return 0; // Not enabled, return success without doing anything
    }
    
    if (!buffer_R || !buffer_G || !buffer_B) {
        return -1;
    }
    
    // Initialize raw scanner capture if needed
    if (init_raw_scanner_capture() != 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &raw_scanner_capture;
    
    // Check if buffer is full
    if (scan->current_height >= raw_scanner_capture_lines) {
        log_info("IMG_DEBUG", "RAW_SCANNER: Auto-saving after %d lines", scan->current_height);
        image_debug_save_raw_scanner_capture();
        image_debug_reset_raw_scanner_capture();
    }
    
    // Add RGB line to scan buffer (raw, unprocessed data)
    for (int x = 0; x < CIS_MAX_PIXELS_NB && x < scan->width; x++) {
        int idx = (scan->current_height * scan->width + x) * 3;
        scan->buffer[idx + 0] = buffer_R[x];
        scan->buffer[idx + 1] = buffer_G[x];
        scan->buffer[idx + 2] = buffer_B[x];
    }
    
    scan->current_height++;
    scan->initialized = 1;
    
    return 0;
}

int image_debug_save_raw_scanner_capture(void) {
    if (!debug_initialized || !raw_scanner_initialized) {
        return -1;
    }
    
    temporal_scan_t *scan = &raw_scanner_capture;
    if (!scan->initialized || scan->current_height == 0) {
        return 0; // Nothing to save
    }
    
    // Generate filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_raw_scanner_capture.png", 
             output_dir, timestamp);
    
    // Save PNG file as RGB (raw scanner data, no processing)
    int result = stbi_write_png(full_path, scan->width, scan->current_height, 3, 
                               scan->buffer, scan->width * 3);
    
    if (result) {
        log_info("IMG_DEBUG", "RAW_SCANNER: Saved raw scanner capture (%dx%d): %s", 
                 scan->width, scan->current_height, full_path);
        return 0;
    } else {
        log_error("IMG_DEBUG", "Failed to save raw scanner capture: %s", full_path);
        return -1;
    }
}

int image_debug_reset_raw_scanner_capture(void) {
    if (!raw_scanner_initialized) {
        return -1;
    }
    
    temporal_scan_t *scan = &raw_scanner_capture;
    scan->current_height = 0;
    
    // Clear buffer (fill with black)
    if (scan->buffer) {
        memset(scan->buffer, 0, scan->width * scan->max_height * 3);
    }
    
    log_info("IMG_DEBUG", "RAW_SCANNER: Reset raw scanner capture buffer");
    return 0;
}

void image_debug_configure_raw_scanner(int enable, int capture_lines) {
    raw_scanner_runtime_enabled = enable;
    
    if (capture_lines > 0) {
        raw_scanner_capture_lines = capture_lines;
    }
    
    if (enable) {
        log_info("IMG_DEBUG", "RAW_SCANNER: Runtime capture enabled (%d lines)", raw_scanner_capture_lines);
        // Auto-enable general image debug if raw scanner is enabled
        if (!debug_image_runtime_enabled) {
            image_debug_enable_runtime(1);
        }
        // Auto-initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
    } else {
        log_info("IMG_DEBUG", "RAW_SCANNER: Runtime capture disabled");
    }
}

int image_debug_is_raw_scanner_enabled(void) {
    return raw_scanner_runtime_enabled;
}

int image_debug_get_raw_scanner_lines(void) {
    return raw_scanner_capture_lines;
}

/**************************************************************************************
 * Oscillator Volume Capture Functions
 **************************************************************************************/

// Ultra-fast capture system - dynamic buffers to avoid huge BSS on macOS dyld
#define MAX_CAPTURE_SAMPLES 96000  // 2 seconds at 48kHz
#define MAX_CAPTURE_NOTES 3456     // Maximum number of notes
static float *static_volume_buffer = NULL;   // size = MAX_CAPTURE_NOTES * MAX_CAPTURE_SAMPLES
static float *static_target_buffer = NULL;   // size = MAX_CAPTURE_NOTES * MAX_CAPTURE_SAMPLES
static uint8_t *static_marker_buffer = NULL; // size = MAX_CAPTURE_SAMPLES
static volatile int static_capture_write_index = 0;
static volatile int static_capture_samples_captured = 0;
static int static_capture_initialized = 0;

// Structure to store both current and target volumes for colorization
typedef struct {
    float current_volume;
    float target_volume;
} oscillator_volume_data_t;

// Buffer to store volume data for colorization
static oscillator_volume_data_t *oscillator_volume_buffer = NULL;
static temporal_scan_t oscillator_volume_scan = {0};
static int oscillator_scan_initialized = 0;

/**
 * @brief Initialize static capture buffer
 * @retval 0 on success, -1 on error
 */
static int init_static_capture_buffer(void) {
    if (static_capture_initialized) {
        return 0;
    }

    size_t total = (size_t)MAX_CAPTURE_NOTES * (size_t)MAX_CAPTURE_SAMPLES;

    static_volume_buffer = (float *)calloc(total, sizeof(float));
    static_target_buffer = (float *)calloc(total, sizeof(float));
    static_marker_buffer = (uint8_t *)calloc(MAX_CAPTURE_SAMPLES, sizeof(uint8_t));
    if (!static_volume_buffer || !static_target_buffer || !static_marker_buffer) {
        log_error("IMG_DEBUG", "Failed to allocate static capture buffers (%d notes x %d samples)",
                  MAX_CAPTURE_NOTES, MAX_CAPTURE_SAMPLES);
        free(static_volume_buffer); static_volume_buffer = NULL;
        free(static_target_buffer); static_target_buffer = NULL;
        free(static_marker_buffer); static_marker_buffer = NULL;
        return -1;
    }

    static_capture_write_index = 0;
    static_capture_samples_captured = 0;
    static_capture_initialized = 1;

    log_info("IMG_DEBUG", "STATIC_CAPTURE: Allocated buffers (%d notes x %d samples) with markers",
             MAX_CAPTURE_NOTES, MAX_CAPTURE_SAMPLES);

    return 0;
}

/**
 * @brief Free static capture buffer
 * @retval None
 */
static void free_static_capture_buffer(void) {
    if (static_volume_buffer) { free(static_volume_buffer); static_volume_buffer = NULL; }
    if (static_target_buffer) { free(static_target_buffer); static_target_buffer = NULL; }
    if (static_marker_buffer) { free(static_marker_buffer); static_marker_buffer = NULL; }
    static_capture_write_index = 0;
    static_capture_samples_captured = 0;
    static_capture_initialized = 0;
}

/**
 * @brief Initialize oscillator volume scan buffer
 * @retval 0 on success, -1 on error
 */
static int init_oscillator_volume_scan(void) {
    if (oscillator_scan_initialized) {
        return 0;
    }
    
    oscillator_volume_scan.width = get_current_number_of_notes();
    oscillator_volume_scan.max_height = oscillator_capture_samples;
    oscillator_volume_scan.current_height = 0;
    oscillator_volume_scan.initialized = 0;
    strncpy(oscillator_volume_scan.name, "oscillator_volumes", sizeof(oscillator_volume_scan.name) - 1);
    oscillator_volume_scan.name[sizeof(oscillator_volume_scan.name) - 1] = '\0';
    
    // Allocate buffer for volume data (stores both current and target volumes)
    oscillator_volume_buffer = calloc(oscillator_volume_scan.width * oscillator_volume_scan.max_height, sizeof(oscillator_volume_data_t));
    if (!oscillator_volume_buffer) {
        log_error("IMG_DEBUG", "Failed to allocate oscillator volume data buffer");
        return -1;
    }
    
    oscillator_scan_initialized = 1;
    
    log_info("IMG_DEBUG", "OSCILLATOR_SCAN: Initialized buffer (%dx%d samples)", 
             get_current_number_of_notes(), oscillator_capture_samples);
    return 0;
}

/**
 * @brief Copy data from static buffer to oscillator buffer for PNG generation
 * @retval 0 on success, -1 on error
 */
static int copy_static_buffer_to_oscillator_buffer(void) {
    if (!static_capture_initialized) {
        return -1;
    }
    
    // Initialize oscillator scan if needed
    if (init_oscillator_volume_scan() != 0) {
        return -1;
    }
    
    int current_notes = get_current_number_of_notes();
    int samples_to_copy = (static_capture_samples_captured < oscillator_capture_samples) ? 
                         static_capture_samples_captured : oscillator_capture_samples;
    
    log_debug("IMG_DEBUG", "STATIC_CAPTURE: Copying %d samples from static buffer", samples_to_copy);
    
    // Copy data from static buffer to oscillator buffer
    for (int sample = 0; sample < samples_to_copy; sample++) {
        for (int note = 0; note < current_notes; note++) {
            int osc_idx = (sample * current_notes) + note;
            
            // Copy volume from static buffers (current and target)
            float volume = static_volume_buffer[(size_t)note * MAX_CAPTURE_SAMPLES + sample];
            oscillator_volume_buffer[osc_idx].current_volume = volume;
            oscillator_volume_buffer[osc_idx].target_volume = static_target_buffer[(size_t)note * MAX_CAPTURE_SAMPLES + sample];
        }
    }
    
    // Update oscillator scan height
    oscillator_volume_scan.current_height = samples_to_copy;
    oscillator_volume_scan.initialized = 1;
    
    log_debug("IMG_DEBUG", "STATIC_CAPTURE: Copied %d samples to oscillator buffer for PNG generation", samples_to_copy);
    return 0;
}

/**
 * @brief Save oscillator volume scan to PNG file
 * @retval 0 on success, -1 on error
 */
static int image_debug_save_oscillator_volume_scan(void) {
    if (!debug_initialized || !oscillator_scan_initialized || !oscillator_volume_buffer) {
        return -1;
    }
    
    temporal_scan_t *scan = &oscillator_volume_scan;
    if (!scan->initialized || scan->current_height == 0) {
        return 0; // Nothing to save
    }
    
    // Create an RGB buffer for the output image
    uint8_t *rgb_8bit = malloc(scan->width * scan->current_height * 3);
    if (!rgb_8bit) {
        log_error("IMG_DEBUG", "Failed to allocate RGB conversion buffer");
        return -1;
    }
    
    // Find global min/max volumes for normalization
    float global_min_current = oscillator_volume_buffer[0].current_volume;
    float global_max_current = oscillator_volume_buffer[0].current_volume;
    float global_min_target = oscillator_volume_buffer[0].target_volume;
    float global_max_target = oscillator_volume_buffer[0].target_volume;
    
    for (int i = 0; i < scan->width * scan->current_height; i++) {
        float current = oscillator_volume_buffer[i].current_volume;
        float target = oscillator_volume_buffer[i].target_volume;
        
        if (current < global_min_current) global_min_current = current;
        if (current > global_max_current) global_max_current = current;
        if (target < global_min_target) global_min_target = target;
        if (target > global_max_target) global_max_target = target;
    }
    
    // Calculate maximum possible volume for normalization
    float max_volume = (global_max_current > global_max_target) ? global_max_current : global_max_target;
    
    int marker_count = 0; // Counter for markers
    
    // Generate colorized image using new algorithm
    for (int y = 0; y < scan->current_height; y++) {
        for (int x = 0; x < scan->width; x++) {
            int idx = y * scan->width + x;
            int idx_8_rgb = idx * 3;
            
            // Get volume data for this pixel
            float current_volume = oscillator_volume_buffer[idx].current_volume;
            float target_volume = oscillator_volume_buffer[idx].target_volume;
            
            // Calculate color based on volume difference and absolute level
            uint8_t r, g, b;
            calculate_oscillator_color(current_volume, target_volume, max_volume, &r, &g, &b);
            
            // Apply the calculated color
            rgb_8bit[idx_8_rgb + 0] = r;
            rgb_8bit[idx_8_rgb + 1] = g;
            rgb_8bit[idx_8_rgb + 2] = b;
        }
        
        // Add yellow separator lines using integrated marker system
        if (oscillator_markers_enabled && static_capture_initialized) {
            // Check if this line has a marker in the static marker buffer
            if (y < MAX_CAPTURE_SAMPLES && static_marker_buffer[y] == 1) {
                marker_count++;
                // Draw a full-width yellow line marker at the exact boundary position
                for (int x = 0; x < scan->width; x++) {
                    int idx_8_rgb = (y * scan->width + x) * 3;
                    rgb_8bit[idx_8_rgb + 0] = 255; // R (Yellow)
                    rgb_8bit[idx_8_rgb + 1] = 255; // G (Yellow)
                    rgb_8bit[idx_8_rgb + 2] = 0;   // B (Yellow)
                }
            }
        }
    }
    
    if (oscillator_markers_enabled) {
        log_debug("IMG_DEBUG", "OSCILLATOR_SCAN: Drew %d yellow separator lines", marker_count);
    } else {
        log_debug("IMG_DEBUG", "OSCILLATOR_SCAN: Markers disabled (no separator lines drawn)");
    }
    
    // Generate filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_oscillator_volumes.png", 
             output_dir, timestamp);
    
    // Save PNG file as RGB
    int result = stbi_write_png(full_path, scan->width, scan->current_height, 3, 
                               rgb_8bit, scan->width * 3);
    
    free(rgb_8bit);
    
    if (result) {
        log_info("IMG_DEBUG", "OSCILLATOR_SCAN: Saved colorized volume scan (%dx%d): %s", 
                 scan->width, scan->current_height, full_path);
        return 0;
    } else {
        log_error("IMG_DEBUG", "Failed to save colorized oscillator volume scan: %s", full_path);
        return -1;
    }
}

/**
 * @brief Reset oscillator volume scan buffer
 * @retval 0 on success, -1 on error
 */
static int image_debug_reset_oscillator_volume_scan(void) {
    if (!oscillator_scan_initialized) {
        return -1;
    }
    
    temporal_scan_t *scan = &oscillator_volume_scan;
    scan->current_height = 0;
    
    log_info("IMG_DEBUG", "OSCILLATOR_SCAN: Reset volume scan buffer");
    return 0;
}

void image_debug_configure_oscillator_capture(int enable, int capture_samples, int enable_markers) {
    oscillator_runtime_enabled = enable;
    
    if (capture_samples > 0) {
        oscillator_capture_samples = capture_samples;
    }
    
    // Store markers setting in the global static variable
    oscillator_markers_enabled = enable_markers;
    
    if (enable) {
        log_info("IMG_DEBUG", "OSCILLATOR: Runtime capture enabled (%d samples%s)", 
                 oscillator_capture_samples, enable_markers ? ", markers enabled" : "");
        // Auto-enable general image debug if oscillator capture is enabled
        if (!debug_image_runtime_enabled) {
            image_debug_enable_runtime(1);
        }
        // Auto-initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
        // Pre-allocate capture buffers outside RT path
        if (init_static_capture_buffer() != 0) {
            log_error("IMG_DEBUG", "Unable to allocate static capture buffers, disabling oscillator capture");
            oscillator_runtime_enabled = 0;
        }
    } else {
        log_info("IMG_DEBUG", "OSCILLATOR: Runtime capture disabled");
        // Free capture buffers when disabling to release memory
        free_static_capture_buffer();
    }
}

int image_debug_is_oscillator_capture_enabled(void) {
    return oscillator_runtime_enabled;
}

int image_debug_get_oscillator_capture_samples(void) {
    return oscillator_capture_samples;
}

void image_debug_capture_volume_sample_fast(int note, float current_volume, float target_volume) {
    // ULTRA-FAST PATH: Only execute if oscillator capture is enabled
    if (!oscillator_runtime_enabled) {
        return; // Exit immediately if not enabled
    }
    
    // Do not allocate in RT path; require pre-allocation via configuration
    if (!static_capture_initialized || !static_volume_buffer || !static_target_buffer || !static_marker_buffer) {
        return; // Not initialized; ignore to keep RT path safe
    }
    
    // CRITICAL: Bounds checking to prevent buffer overflow
    if (note < 0 || note >= MAX_CAPTURE_NOTES) {
        return; // Invalid note index, abort
    }
    
    if (static_capture_write_index >= MAX_CAPTURE_SAMPLES) {
        return; // Buffer full, abort
    }
    
    // ULTRA-FAST STORE: Just copy the current volume to static buffer
    static_volume_buffer[(size_t)note * MAX_CAPTURE_SAMPLES + static_capture_write_index] = current_volume;
    // Store target volume aligned with current sample
    static_target_buffer[(size_t)note * MAX_CAPTURE_SAMPLES + static_capture_write_index] = target_volume;
    
    // Only increment counters for the last note to avoid race conditions
    int current_notes = get_current_number_of_notes();
    if (note == (current_notes - 1)) {
        static_capture_samples_captured++;
        static_capture_write_index++;
        
        // Auto-process when we have enough samples (non-blocking check)
        if (static_capture_samples_captured >= oscillator_capture_samples) {
            // SOLUTION: Copy static buffer to oscillator buffer and generate PNG
            // Use a static flag to prevent multiple concurrent processing
            static volatile int processing_in_progress = 0;
            
            if (__sync_bool_compare_and_swap(&processing_in_progress, 0, 1)) {
                log_info("IMG_DEBUG", "STATIC_CAPTURE: Processing %d samples for PNG generation", static_capture_samples_captured);
                
                if (copy_static_buffer_to_oscillator_buffer() == 0) {
                    // Generate and save the PNG image
                    image_debug_save_oscillator_volume_scan();
                    image_debug_reset_oscillator_volume_scan();
                }
                
                // Reset the counter for next capture cycle
                static_capture_samples_captured = 0;
                static_capture_write_index = 0;
                
                // Release the processing flag
                processing_in_progress = 0;
            }
        }
    }
}

void image_debug_mark_new_image_boundary(void) {
    // Only mark if oscillator capture is enabled
    if (oscillator_runtime_enabled && oscillator_markers_enabled && static_capture_initialized) {
        // Mark directly in the static marker buffer at current write position
        if (static_capture_write_index < MAX_CAPTURE_SAMPLES) {
            static_marker_buffer[static_capture_write_index] = 1; // Mark this line as boundary
        }
    }
}
