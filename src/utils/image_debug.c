/* image_debug.c */

#include "image_debug.h"
#include "config_debug.h"
#include "config_audio.h"
#include "../synthesis/additive/wave_generation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
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
static int oscillator_capture_samples = 48000; // Default number of samples to capture (1 second at 48kHz)
static int oscillator_markers_enabled = 0; // Runtime control for oscillator markers
#ifdef DEBUG_IMAGE_FRAME_COUNTER
static int frame_counter = 0;
#endif
static char output_dir[256];

// Temporal scan buffers
#define MAX_SCAN_HEIGHT 48000  // Maximum number of lines in scan (increased for 48k lines)
#define MAX_SCAN_TYPES 3       // "grayscale", "processed", "original"

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
    (void)target_volume; // Unused parameter
    
    // CORRECTED ALGORITHM: Low volume = WHITE, High volume = COLORED
    // This matches the expected behavior where silence should be white
    
    // Normalize current volume (0.0 = no volume, 1.0 = max volume)
    float volume_normalized = (max_volume > 0.0f) ? (current_volume / max_volume) : 0.0f;
    if (volume_normalized > 1.0f) volume_normalized = 1.0f;
    
    // Special case: if volume is extremely low (less than 0.01%), make it WHITE
    if (volume_normalized < 0.0001f) {
        *r = 255; *g = 255; *b = 255; // Pure white for silence
        return;
    }
    
    // Calculate hue based on volume level: 
    // Low volume = blue (240°), High volume = red (0°)
    float hue = 240.0f * (1.0f - volume_normalized); // 240° -> 0°
    
    // Saturation: lower for low volumes (more white), higher for high volumes
    float saturation = volume_normalized * 0.9f;
    
    // Lightness: always bright to avoid dark colors
    // Low volume = very bright (0.9), High volume = medium bright (0.6)
    float lightness = 0.9f - (volume_normalized * 0.3f);
    
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
            printf("ERROR: Failed to create debug image directory: %s\n", output_dir);
            return -1;
        }
    }
    
    debug_initialized = 1;
    
    printf("🔧 IMAGE_DEBUG: Initialized, output directory: %s\n", output_dir);
    return 0;
}

void image_debug_cleanup(void) {
    debug_initialized = 0;
    printf("🔧 IMAGE_DEBUG: Cleanup completed\n");
}

int image_debug_save_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                        int width, int height, const char *filename, const char *stage_name) {
    (void)buffer_R; (void)buffer_G; (void)buffer_B;
    (void)width; (void)height; (void)filename; (void)stage_name;
    return 0; // Debug disabled
}

/**************************************************************************************
 * Runtime Control Functions
 **************************************************************************************/

void image_debug_enable_runtime(int enable) {
    debug_image_runtime_enabled = enable;
    if (enable) {
        printf("🔧 IMAGE_DEBUG: Runtime debug enabled\n");
        // Initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
    } else {
        printf("🔧 IMAGE_DEBUG: Runtime debug disabled\n");
    }
}

int image_debug_is_enabled(void) {
    return debug_image_runtime_enabled;
}

/**************************************************************************************
 * Oscillator Volume Debug Functions
 **************************************************************************************/

// Global oscillator volume scan structure
static temporal_scan_t oscillator_volume_scan = {0};
static int oscillator_scan_initialized = 0;
static int oscillator_sample_counter = 0;

// Structure to store both current and target volumes for colorization
typedef struct {
    float current_volume;
    float target_volume;
} oscillator_volume_data_t;

// Buffer to store volume data for colorization (16-bit resolution)
static oscillator_volume_data_t *oscillator_volume_buffer = NULL;

// Ultra-fast capture system - STATIC buffer for reliable capture
#define MAX_CAPTURE_SAMPLES 96000  // 2 seconds at 48kHz
#define MAX_CAPTURE_NOTES 3456     // Maximum number of notes
static float static_volume_buffer[MAX_CAPTURE_NOTES][MAX_CAPTURE_SAMPLES];
static volatile int static_capture_write_index = 0;
static volatile int static_capture_samples_captured = 0;
static int static_capture_initialized = 0;

// Global raw scanner capture structure (now always available for runtime configuration)
static temporal_scan_t raw_scanner_capture = {0};
static int raw_scanner_initialized = 0;

/**
 * @brief Initialize oscillator volume scan buffer
 * @retval 0 on success, -1 on error
 */
static int init_oscillator_volume_scan(void) {
    if (oscillator_scan_initialized) {
        return 0;
    }
    
    oscillator_volume_scan.width = get_current_number_of_notes();
    oscillator_volume_scan.max_height = oscillator_capture_samples; // Use runtime-configured samples
    oscillator_volume_scan.current_height = 0;
    oscillator_volume_scan.initialized = 0;
    strncpy(oscillator_volume_scan.name, "oscillator_volumes", sizeof(oscillator_volume_scan.name) - 1);
    oscillator_volume_scan.name[sizeof(oscillator_volume_scan.name) - 1] = '\0';
    
    // Allocate buffer for volume data (stores both current and target volumes)
    oscillator_volume_buffer = calloc(oscillator_volume_scan.width * oscillator_volume_scan.max_height, sizeof(oscillator_volume_data_t));
    if (!oscillator_volume_buffer) {
        printf("ERROR: Failed to allocate oscillator volume data buffer\n");
        return -1;
    }
    
    // Keep the old buffer for compatibility (not used in new colorized version)
    oscillator_volume_scan.buffer = calloc(oscillator_volume_scan.width * oscillator_volume_scan.max_height, sizeof(uint16_t));
    if (!oscillator_volume_scan.buffer) {
        printf("ERROR: Failed to allocate oscillator volume scan buffer\n");
        free(oscillator_volume_buffer);
        oscillator_volume_buffer = NULL;
        return -1;
    }
    
    oscillator_scan_initialized = 1;
    oscillator_sample_counter = 0;
    
    printf("🔧 OSCILLATOR_SCAN: Initialized buffer (%dx%d samples)\n", 
           get_current_number_of_notes(), oscillator_capture_samples);
    return 0;
}

int image_debug_capture_oscillator_sample(void) {
    // Check if oscillator capture is enabled at runtime
    if (!oscillator_runtime_enabled || !debug_initialized || !debug_image_runtime_enabled) {
        return 0; // Not enabled, return success without doing anything
    }
    
    // Initialize oscillator scan if needed
    if (init_oscillator_volume_scan() != 0) {
        return -1;
    }
    
    // Capture current oscillator volumes
    int current_notes = get_current_number_of_notes();
    float volumes[MAX_NUMBER_OF_NOTES];
    for (int i = 0; i < current_notes; i++) {
        volumes[i] = waves[i].current_volume;
    }
    
    // Add to scan buffer
    int result = image_debug_add_oscillator_volume_line(volumes, current_notes);
    
    oscillator_sample_counter++;
    
    // Auto-save when we reach the target number of samples
    if (oscillator_sample_counter >= oscillator_capture_samples) {
        printf("🔧 OSCILLATOR_SCAN: Auto-saving after %d samples\n", oscillator_sample_counter);
        image_debug_save_oscillator_volume_scan();
        image_debug_reset_oscillator_volume_scan();
        oscillator_sample_counter = 0;
    }
    
    return result;
}

int image_debug_add_oscillator_volume_line(float *volumes, int count) {
    if (!debug_initialized || !volumes || !oscillator_scan_initialized || !oscillator_volume_buffer) {
        return -1;
    }
    
    temporal_scan_t *scan = &oscillator_volume_scan;
    
    // Check if buffer is full
    if (scan->current_height >= scan->max_height) {
        printf("🚨 OSCILLATOR_SCAN: Buffer full, auto-saving\n");
        image_debug_save_oscillator_volume_scan();
        image_debug_reset_oscillator_volume_scan();
    }
    
    // Store both current and target volumes in the new buffer
    for (int x = 0; x < count && x < scan->width; x++) {
        int idx = scan->current_height * scan->width + x;
        oscillator_volume_buffer[idx].current_volume = waves[x].current_volume;
        oscillator_volume_buffer[idx].target_volume = waves[x].target_volume;
    }
    
    // Keep old logic for compatibility (16-bit buffer)
    float min_vol = volumes[0];
    float max_vol = volumes[0];
    for (int i = 1; i < count && i < get_current_number_of_notes(); i++) {
        if (volumes[i] < min_vol) min_vol = volumes[i];
        if (volumes[i] > max_vol) max_vol = volumes[i];
    }
    
    // Avoid division by zero
    if (max_vol == min_vol) {
        max_vol = min_vol + 1.0f;
    }
    
    // Add line to scan buffer using 16-bit resolution (same as oscillator volumes)
    uint16_t *buffer_16 = (uint16_t*)scan->buffer;
    
    for (int x = 0; x < count && x < scan->width; x++) {
        // Normalize volume to 0-65535 range (16-bit, same as VOLUME_AMP_RESOLUTION)
        // Volume max = pixel black (0), Volume min = pixel white (65535)
        float normalized = (volumes[x] - min_vol) / (max_vol - min_vol);
        uint16_t pixel_value = (uint16_t)(VOLUME_AMP_RESOLUTION - (normalized * VOLUME_AMP_RESOLUTION)); // Invert: max vol = black
        buffer_16[scan->current_height * scan->width + x] = pixel_value;
    }
    
    scan->current_height++;
    scan->initialized = 1;
    
    return 0;
}

int image_debug_save_oscillator_volume_scan(void) {
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
        printf("ERROR: Failed to allocate RGB conversion buffer\n");
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
    
    // SPECIAL CASE: If all volumes are very small or zero, use a default scale
    // This happens when scanner is on image but volumes are very low
    if (max_volume < 0.01f) {
        printf("🔧 OSCILLATOR_SCAN: Very low max_volume (%.6f), using default scale\n", max_volume);
        max_volume = 1.0f; // Use default scale for better visualization
    }
    
    
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
        
        // Add yellow separator lines if markers enabled at runtime
        if (oscillator_markers_enabled && y > 0 && y % AUDIO_BUFFER_SIZE == 0) {
            marker_count++;
            // Draw a full-width yellow line markers
            for (int x = 0; x < scan->width; x++) {
                int idx_8_rgb = (y * scan->width + x) * 3;
                rgb_8bit[idx_8_rgb + 0] = 255; // R (Yellow)
                rgb_8bit[idx_8_rgb + 1] = 255; // G (Yellow)
                rgb_8bit[idx_8_rgb + 2] = 0; // B (Yellow)
            }
        }
    }
    
    if (oscillator_markers_enabled) {
        printf("🔧 OSCILLATOR_SCAN: Drew %d yellow separator lines.\n", marker_count);
    } else {
        printf("🔧 OSCILLATOR_SCAN: Markers disabled (no separator lines drawn).\n");
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
        printf("🔧 OSCILLATOR_SCAN: Saved colorized volume scan (%dx%d): %s\n", 
               scan->width, scan->current_height, full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save colorized oscillator volume scan: %s\n", full_path);
        return -1;
    }
}

int image_debug_reset_oscillator_volume_scan(void) {
    if (!oscillator_scan_initialized) {
        return -1;
    }
    
    temporal_scan_t *scan = &oscillator_volume_scan;
    scan->current_height = 0;
    
    // Clear buffer (16-bit format, fill with white = max value = 65535)
    if (scan->buffer) {
        uint16_t *buffer_16 = (uint16_t*)scan->buffer;
        for (int i = 0; i < scan->width * scan->max_height; i++) {
            buffer_16[i] = VOLUME_AMP_RESOLUTION; // Fill with white (min volume = max pixel value)
        }
    }
    
    printf("🔧 OSCILLATOR_SCAN: Reset volume scan buffer (16-bit)\n");
    return 0;
}

int image_debug_capture_mono_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                     int32_t *grayscale_data, int32_t *processed_data, int frame_number) {

    if (!image_debug_should_capture(frame_number)) {
        return 0; // Skip this frame
    }
    
    // Add lines to temporal scans instead of saving individual images
    
    // For original: use RGB data directly (we'll handle the RGB->scan conversion in add_scan_line_rgb)
    image_debug_add_scan_line_rgb(buffer_R, buffer_G, buffer_B, CIS_MAX_PIXELS_NB, "original");
    
    // Add grayscale conversion as scan line
    image_debug_add_scan_line(grayscale_data, CIS_MAX_PIXELS_NB, "grayscale");
    
    // Add final processed data as scan line
    image_debug_add_scan_line(processed_data, CIS_MAX_PIXELS_NB, "processed");
    
    return 0;

    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)grayscale_data; 
    (void)processed_data; (void)frame_number;
    return 0; // Debug disabled
}

int image_debug_capture_stereo_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                       int32_t *warm_raw __attribute__((unused)), int32_t *cold_raw __attribute__((unused)),
                                       int32_t *warm_processed, int32_t *cold_processed, int frame_number) {
    if (!image_debug_should_capture(frame_number)) {
        return 0; // Skip this frame
    }
    
    // Add lines to temporal scans instead of saving individual images
    // Note: For stereo mode, we'll create combined scans showing both channels
    
    // Convert RGB to grayscale for original data scan
    int32_t *original_grayscale = malloc(CIS_MAX_PIXELS_NB * sizeof(int32_t));
    if (original_grayscale) {
        // Convert RGB to grayscale using perceptual weights
        for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
            float gray = (buffer_R[i] * PERCEPTUAL_WEIGHT_R + 
                         buffer_G[i] * PERCEPTUAL_WEIGHT_G + 
                         buffer_B[i] * PERCEPTUAL_WEIGHT_B);
            original_grayscale[i] = (int32_t)(gray * (VOLUME_AMP_RESOLUTION / 255.0f));
        }
        
        // Add original data as scan line
        image_debug_add_scan_line(original_grayscale, CIS_MAX_PIXELS_NB, "original");
        free(original_grayscale);
    }
    
    // For stereo, we'll combine warm and cold channels into a single grayscale representation
    // This gives us a view of the overall processing result
    int32_t *combined_processed = malloc(CIS_MAX_PIXELS_NB * sizeof(int32_t));
    if (combined_processed) {
        for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
            // Combine warm and cold channels (simple average for visualization)
            combined_processed[i] = (warm_processed[i] + cold_processed[i]) / 2;
        }
        
        // Add combined processed data as scan line
        image_debug_add_scan_line(combined_processed, CIS_MAX_PIXELS_NB, "processed");
        free(combined_processed);
    }
    
    return 0;
}

int image_debug_should_capture(int frame_number) {
    return (frame_number % 1) == 0; // Capture every frame by default
}

/**************************************************************************************
 * Raw Scanner Capture Functions
 **************************************************************************************/

/**
 * @brief Initialize raw scanner capture buffer (now uses runtime configuration)
 * @retval 0 on success, -1 on error
 */
static int init_raw_scanner_capture(void) {
    if (raw_scanner_initialized) {
        return 0;
    }
    
    raw_scanner_capture.width = CIS_MAX_PIXELS_NB;
    raw_scanner_capture.max_height = raw_scanner_capture_lines; // Use runtime-configured lines
    raw_scanner_capture.current_height = 0;
    raw_scanner_capture.initialized = 0;
    strncpy(raw_scanner_capture.name, "raw_scanner", sizeof(raw_scanner_capture.name) - 1);
    raw_scanner_capture.name[sizeof(raw_scanner_capture.name) - 1] = '\0';
    
    // Allocate buffer (RGB format for raw scanner data)
    raw_scanner_capture.buffer = calloc(raw_scanner_capture.width * raw_scanner_capture.max_height * 3, sizeof(uint8_t));
    if (!raw_scanner_capture.buffer) {
        printf("ERROR: Failed to allocate raw scanner capture buffer\n");
        return -1;
    }
    
    raw_scanner_initialized = 1;
    
    printf("🔧 RAW_SCANNER: Initialized buffer (%dx%d lines)\n", 
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
    
    // Initialize raw scanner capture if needed (now uses runtime configuration)
    if (init_raw_scanner_capture() != 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &raw_scanner_capture;
    
    // Check if buffer is full (using runtime-configured lines)
    if (scan->current_height >= raw_scanner_capture_lines) {
        printf("🔧 RAW_SCANNER: Auto-saving after %d lines\n", scan->current_height);
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
        printf("🔧 RAW_SCANNER: Saved raw scanner capture (%dx%d): %s\n", 
               scan->width, scan->current_height, full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save raw scanner capture: %s\n", full_path);
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
    
    printf("🔧 RAW_SCANNER: Reset raw scanner capture buffer\n");
    return 0;
}

/**************************************************************************************
 * Temporal Scan Functions
 **************************************************************************************/

int image_debug_add_scan_line_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B, int width, const char *scan_type) {
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)width; (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
}

int image_debug_add_scan_line(int32_t *buffer_data, int width, const char *scan_type) {
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)buffer_data; (void)width; (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
}

int image_debug_save_scan(const char *scan_type) {
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
}

int image_debug_reset_scan(const char *scan_type) {
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
}

/**************************************************************************************
 * Raw Scanner Runtime Configuration Functions
 **************************************************************************************/

void image_debug_configure_raw_scanner(int enable, int capture_lines) {
    raw_scanner_runtime_enabled = enable;
    
    if (capture_lines > 0) {
        raw_scanner_capture_lines = capture_lines;
    }
    
    if (enable) {
        printf("🔧 RAW_SCANNER: Runtime capture enabled (%d lines)\n", raw_scanner_capture_lines);
        // Auto-enable general image debug if raw scanner is enabled
        if (!debug_image_runtime_enabled) {
            image_debug_enable_runtime(1);
        }
        // Auto-initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
    } else {
        printf("🔧 RAW_SCANNER: Runtime capture disabled\n");
    }
}

int image_debug_is_raw_scanner_enabled(void) {
    return raw_scanner_runtime_enabled;
}

int image_debug_get_raw_scanner_lines(void) {
    return raw_scanner_capture_lines;
}

/**************************************************************************************
 * Oscillator Runtime Configuration Functions
 **************************************************************************************/

void image_debug_configure_oscillator_capture(int enable, int capture_samples, int enable_markers) {
    oscillator_runtime_enabled = enable;
    
    if (capture_samples > 0) {
        oscillator_capture_samples = capture_samples;
    }
    
    // Store markers setting in the global static variable
    oscillator_markers_enabled = enable_markers;
    
    if (enable) {
        printf("🔧 OSCILLATOR: Runtime capture enabled (%d samples%s)\n", 
               oscillator_capture_samples, enable_markers ? ", markers enabled" : "");
        // Auto-enable general image debug if oscillator capture is enabled
        if (!debug_image_runtime_enabled) {
            image_debug_enable_runtime(1);
        }
        // Auto-initialize debug system if not already done
        if (!debug_initialized) {
            image_debug_init();
        }
    } else {
        printf("🔧 OSCILLATOR: Runtime capture disabled\n");
    }
}

int image_debug_is_oscillator_capture_enabled(void) {
    return oscillator_runtime_enabled;
}

int image_debug_get_oscillator_capture_samples(void) {
    return oscillator_capture_samples;
}

/**************************************************************************************
 * Ultra-Fast Volume Capture Functions
 **************************************************************************************/

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
    
    printf("🔧 STATIC_CAPTURE: Copying %d samples from static buffer\n", samples_to_copy);
    
    // Copy data from static buffer to oscillator buffer
    for (int sample = 0; sample < samples_to_copy; sample++) {
        for (int note = 0; note < current_notes; note++) {
            int osc_idx = (sample * current_notes) + note;
            
            // Copy volume from static buffer (same value for current and target)
            float volume = static_volume_buffer[note][sample];
            oscillator_volume_buffer[osc_idx].current_volume = volume;
            oscillator_volume_buffer[osc_idx].target_volume = volume;
        }
    }
    
    // Update oscillator scan height
    oscillator_volume_scan.current_height = samples_to_copy;
    oscillator_volume_scan.initialized = 1;
    
    printf("🔧 STATIC_CAPTURE: Copied %d samples to oscillator buffer for PNG generation\n", samples_to_copy);
    return 0;
}

/**
 * @brief Initialize static capture buffer
 * @retval 0 on success, -1 on error
 */
static int init_static_capture_buffer(void) {
    if (static_capture_initialized) {
        return 0;
    }
    
    // Clear the static buffer
    memset(static_volume_buffer, 0, sizeof(static_volume_buffer));
    
    static_capture_write_index = 0;
    static_capture_samples_captured = 0;
    static_capture_initialized = 1;
    
    printf("🔧 STATIC_CAPTURE: Initialized static buffer (%d notes x %d samples)\n", 
           MAX_CAPTURE_NOTES, MAX_CAPTURE_SAMPLES);
    
    return 0;
}

/**
 * @brief Ultra-fast volume capture for real-time processing - SIMPLIFIED STATIC VERSION
 * This function performs minimal work - just stores values in a static buffer
 * Only active when --debug-additive-osc-image is enabled
 * @param note Note index
 * @param current_volume Current volume value
 * @param target_volume Target volume value (unused but kept for compatibility)
 * @retval None (inline for maximum performance)
 */
void image_debug_capture_volume_sample_fast(int note, float current_volume, float target_volume) {
    (void)target_volume; // Unused parameter
    
    // DEBUG: Log first few calls to verify function is called
    static int debug_call_count = 0;
    if (debug_call_count < 10) {
        printf("DEBUG_CAPTURE: Call #%d - note=%d, volume=%.3f, enabled=%d\n", 
               debug_call_count, note, current_volume, oscillator_runtime_enabled);
        debug_call_count++;
    }
    
    // ULTRA-FAST PATH: Only execute if oscillator capture is enabled
    if (!oscillator_runtime_enabled) {
        return; // Exit immediately if not enabled
    }
    
    // Initialize buffer on first call (lazy initialization)
    if (!static_capture_initialized) {
        if (init_static_capture_buffer() != 0) {
            return; // Failed to initialize, abort
        }
    }
    
    // CRITICAL: Bounds checking to prevent buffer overflow
    if (note < 0 || note >= MAX_CAPTURE_NOTES) {
        return; // Invalid note index, abort
    }
    
    if (static_capture_write_index >= MAX_CAPTURE_SAMPLES) {
        return; // Buffer full, abort
    }
    
    // ULTRA-FAST STORE: Just copy the current volume to static buffer
    static_volume_buffer[note][static_capture_write_index] = current_volume;
    
    // Only increment counters for the last note to avoid race conditions
    int current_notes = get_current_number_of_notes();
    if (note == (current_notes - 1)) {
        static_capture_samples_captured++;
        static_capture_write_index++;
        
        // Auto-process when we have enough samples (non-blocking check)
        if (static_capture_samples_captured >= oscillator_capture_samples) {
            // SOLUTION: Copy static buffer to oscillator buffer and generate PNG
            printf("🔧 STATIC_CAPTURE: Processing %d samples for PNG generation\n", static_capture_samples_captured);
            
            if (copy_static_buffer_to_oscillator_buffer() == 0) {
                // Generate and save the PNG image
                image_debug_save_oscillator_volume_scan();
                image_debug_reset_oscillator_volume_scan();
            }
            
            // Reset the counter for next capture cycle
            static_capture_samples_captured = 0;
            static_capture_write_index = 0;
        }
    }
}
