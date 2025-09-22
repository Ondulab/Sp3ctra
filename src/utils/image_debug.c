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

// Color palettes for visualization
#ifdef DEBUG_IMAGE_STEREO_CHANNELS
static const uint8_t warm_palette[3] = {255, 100, 50};  // Orange-red for warm channel
static const uint8_t cold_palette[3] = {50, 150, 255}; // Blue for cold channel
#endif
#ifdef DEBUG_IMAGE_SHOW_HISTOGRAMS
static const uint8_t gray_palette[3] = {128, 128, 128}; // Gray for mono
#endif

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

#ifdef DEBUG_TEMPORAL_SCAN
static temporal_scan_t scans[MAX_SCAN_TYPES];
static int scans_initialized = 0;
#endif

/**************************************************************************************
 * Internal Helper Functions
 **************************************************************************************/

/**
 * @brief Create directory if it doesn't exist
 * @param path Directory path to create
 * @retval 0 on success, -1 on error
 */
#ifdef ENABLE_IMAGE_DEBUG
static int create_directory(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) != 0) {
            perror("mkdir");
            return -1;
        }
    }
    return 0;
}
#endif

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
 * @brief Convert 32-bit data to 8-bit for PNG output
 * @param input_32 Input 32-bit data
 * @param output_8 Output 8-bit data
 * @param size Number of pixels
 * @param max_value Maximum value for normalization
 * @retval None
 */
#if defined(DEBUG_IMAGE_SAVE_TO_FILES) || defined(DEBUG_IMAGE_STEREO_CHANNELS) || defined(DEBUG_TEMPORAL_SCAN)
static void convert_32bit_to_8bit(int32_t *input_32, uint8_t *output_8, int size, int32_t max_value) {
    if (max_value == 0) max_value = 1; // Avoid division by zero
    
    for (int i = 0; i < size; i++) {
        int32_t value = input_32[i];
        if (value < 0) value = 0;
        if (value > max_value) value = max_value;
        output_8[i] = (uint8_t)((value * 255) / max_value);
    }
}
#endif

/**
 * @brief Apply color palette to grayscale data
 * @param grayscale_8 Input grayscale data (8-bit)
 * @param rgb_output Output RGB data
 * @param size Number of pixels
 * @param palette Color palette to apply (RGB)
 * @retval None
 */
#ifdef DEBUG_IMAGE_SHOW_HISTOGRAMS
static void apply_color_palette(uint8_t *grayscale_8, uint8_t *rgb_output, int size, const uint8_t *palette) {
    for (int i = 0; i < size; i++) {
        float intensity = grayscale_8[i] / 255.0f;
        rgb_output[i * 3 + 0] = (uint8_t)(palette[0] * intensity);
        rgb_output[i * 3 + 1] = (uint8_t)(palette[1] * intensity);
        rgb_output[i * 3 + 2] = (uint8_t)(palette[2] * intensity);
    }
}
#endif

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
#ifdef ENABLE_IMAGE_DEBUG
    debug_initialized = 0;
    printf("🔧 IMAGE_DEBUG: Cleanup completed\n");
#endif
}

int image_debug_save_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                        int width, int height, const char *filename, const char *stage_name) {
#ifdef DEBUG_IMAGE_SAVE_TO_FILES
    if (!debug_initialized || !buffer_R || !buffer_G || !buffer_B) {
        return -1;
    }
    
    // Allocate RGB interleaved buffer
    uint8_t *rgb_data = malloc(width * height * 3);
    if (!rgb_data) {
        printf("ERROR: Failed to allocate RGB buffer for debug image\n");
        return -1;
    }
    
    // Interleave RGB data
    for (int i = 0; i < width * height; i++) {
        rgb_data[i * 3 + 0] = buffer_R[i];
        rgb_data[i * 3 + 1] = buffer_G[i];
        rgb_data[i * 3 + 2] = buffer_B[i];
    }
    
    // Generate full filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_%s_%s.png", 
             output_dir, timestamp, filename, stage_name);
    
    // Save PNG file
    int result = stbi_write_png(full_path, width, height, 3, rgb_data, width * 3);
    
    free(rgb_data);
    
    if (result) {
        printf("🔧 IMAGE_DEBUG: Saved RGB image: %s\n", full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save RGB debug image: %s\n", full_path);
        return -1;
    }
#else
    (void)buffer_R; (void)buffer_G; (void)buffer_B;
    (void)width; (void)height; (void)filename; (void)stage_name;
    return 0; // Debug disabled
#endif
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
    
    // Allocate buffer (16-bit grayscale format for oscillator volumes, same resolution as VOLUME_AMP_RESOLUTION)
    oscillator_volume_scan.buffer = calloc(oscillator_volume_scan.width * oscillator_volume_scan.max_height, sizeof(uint16_t));
    if (!oscillator_volume_scan.buffer) {
        printf("ERROR: Failed to allocate oscillator volume scan buffer\n");
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
    if (!debug_initialized || !volumes || !oscillator_scan_initialized) {
        return -1;
    }
    
    temporal_scan_t *scan = &oscillator_volume_scan;
    
    // Check if buffer is full
    if (scan->current_height >= scan->max_height) {
        printf("🚨 OSCILLATOR_SCAN: Buffer full, auto-saving\n");
        image_debug_save_oscillator_volume_scan();
        image_debug_reset_oscillator_volume_scan();
    }
    
    // Find min/max volumes for normalization
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
    if (!debug_initialized || !oscillator_scan_initialized) {
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
    
    uint16_t *buffer_16 = (uint16_t*)scan->buffer;
    int marker_count = 0; // Counter for markers
    
    for (int y = 0; y < scan->current_height; y++) {
        for (int x = 0; x < scan->width; x++) {
            int idx_16 = y * scan->width + x;
            int idx_8_rgb = idx_16 * 3;
            
            // Convert 16-bit grayscale to 8-bit
            uint8_t gray_value = (uint8_t)(buffer_16[idx_16] >> 8);
            
            // Default to grayscale
            rgb_8bit[idx_8_rgb + 0] = gray_value;
            rgb_8bit[idx_8_rgb + 1] = gray_value;
            rgb_8bit[idx_8_rgb + 2] = gray_value;
        }
        
        // Add markers if enabled at runtime
        if (oscillator_markers_enabled && y > 0 && y % AUDIO_BUFFER_SIZE == 0) {
            marker_count++;
            for (int marker_y = y; marker_y < y + 5 && marker_y < scan->current_height; marker_y++) {
                for (int x = 0; x < 20 && x < scan->width; x++) { // 20 pixels wide marker
                    int idx_8_rgb = (marker_y * scan->width + x) * 3;
                    rgb_8bit[idx_8_rgb + 0] = 0;   // R (Dark Blue)
                    rgb_8bit[idx_8_rgb + 1] = 0;   // G (Dark Blue)
                    rgb_8bit[idx_8_rgb + 2] = 128; // B (Dark Blue)
                }
            }
        }
    }
    
    if (oscillator_markers_enabled) {
        printf("🔧 OSCILLATOR_SCAN: Drew %d markers.\n", marker_count);
    } else {
        printf("🔧 OSCILLATOR_SCAN: Markers disabled (no visual markers drawn).\n");
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
        printf("🔧 OSCILLATOR_SCAN: Saved volume scan with markers (%dx%d): %s\n", 
               scan->width, scan->current_height, full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save oscillator volume scan: %s\n", full_path);
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

int image_debug_save_grayscale(void *buffer, int width, int height,
                              const char *filename, const char *stage_name, int is_32bit) {
#ifdef DEBUG_IMAGE_SAVE_TO_FILES
    if (!debug_initialized || !buffer) {
        return -1;
    }
    
    uint8_t *gray_8bit = malloc(width * height);
    if (!gray_8bit) {
        printf("ERROR: Failed to allocate grayscale buffer for debug image\n");
        return -1;
    }
    
    if (is_32bit) {
        // Convert 32-bit to 8-bit
        convert_32bit_to_8bit((int32_t*)buffer, gray_8bit, width * height, VOLUME_AMP_RESOLUTION);
    } else {
        // Convert 16-bit to 8-bit
        uint16_t *buffer_16 = (uint16_t*)buffer;
        for (int i = 0; i < width * height; i++) {
            gray_8bit[i] = (uint8_t)(buffer_16[i] >> 8); // Simple 16->8 bit conversion
        }
    }
    
    // Generate full filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_%s_%s.png", 
             output_dir, timestamp, filename, stage_name);
    
    // Save PNG file (grayscale)
    int result = stbi_write_png(full_path, width, height, 1, gray_8bit, width);
    
    free(gray_8bit);
    
    if (result) {
        printf("🔧 IMAGE_DEBUG: Saved grayscale image: %s\n", full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save grayscale debug image: %s\n", full_path);
        return -1;
    }
#else
    (void)buffer; (void)width; (void)height; (void)filename; (void)stage_name; (void)is_32bit;
    return 0; // Debug disabled
#endif
}

int image_debug_save_stereo_channels(int32_t *warm_channel, int32_t *cold_channel,
                                    int width, int height, const char *filename, const char *stage_name) {
#ifdef DEBUG_IMAGE_STEREO_CHANNELS
    if (!debug_initialized || !warm_channel || !cold_channel) {
        return -1;
    }
    
    // Create side-by-side image (double width)
    int total_width = width * 2;
    uint8_t *rgb_data = malloc(total_width * height * 3);
    if (!rgb_data) {
        printf("ERROR: Failed to allocate stereo buffer for debug image\n");
        return -1;
    }
    
    // Convert channels to 8-bit
    uint8_t *warm_8bit = malloc(width * height);
    uint8_t *cold_8bit = malloc(width * height);
    if (!warm_8bit || !cold_8bit) {
        free(rgb_data);
        free(warm_8bit);
        free(cold_8bit);
        return -1;
    }
    
    convert_32bit_to_8bit(warm_channel, warm_8bit, width * height, VOLUME_AMP_RESOLUTION);
    convert_32bit_to_8bit(cold_channel, cold_8bit, width * height, VOLUME_AMP_RESOLUTION);
    
    // Create side-by-side visualization
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = y * width + x;
            int dst_idx_left = y * total_width + x;
            int dst_idx_right = y * total_width + (width + x);
            
            // Left side: warm channel (orange-red)
            float warm_intensity = warm_8bit[src_idx] / 255.0f;
            rgb_data[dst_idx_left * 3 + 0] = (uint8_t)(warm_palette[0] * warm_intensity);
            rgb_data[dst_idx_left * 3 + 1] = (uint8_t)(warm_palette[1] * warm_intensity);
            rgb_data[dst_idx_left * 3 + 2] = (uint8_t)(warm_palette[2] * warm_intensity);
            
            // Right side: cold channel (blue)
            float cold_intensity = cold_8bit[src_idx] / 255.0f;
            rgb_data[dst_idx_right * 3 + 0] = (uint8_t)(cold_palette[0] * cold_intensity);
            rgb_data[dst_idx_right * 3 + 1] = (uint8_t)(cold_palette[1] * cold_intensity);
            rgb_data[dst_idx_right * 3 + 2] = (uint8_t)(cold_palette[2] * cold_intensity);
        }
    }
    
    // Generate full filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_%s_%s_stereo.png", 
             output_dir, timestamp, filename, stage_name);
    
    // Save PNG file
    int result = stbi_write_png(full_path, total_width, height, 3, rgb_data, total_width * 3);
    
    free(rgb_data);
    free(warm_8bit);
    free(cold_8bit);
    
    if (result) {
        printf("🔧 IMAGE_DEBUG: Saved stereo channels image: %s\n", full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save stereo debug image: %s\n", full_path);
        return -1;
    }
#else
    (void)warm_channel; (void)cold_channel; (void)width; (void)height; 
    (void)filename; (void)stage_name;
    return 0; // Debug disabled
#endif
}

int image_debug_save_histogram(int32_t *buffer, int size, const char *filename,
                              const char *stage_name, int32_t max_value) {
#ifdef DEBUG_IMAGE_SHOW_HISTOGRAMS
    if (!debug_initialized || !buffer) {
        return -1;
    }
    
    // Create histogram (256 bins)
    const int hist_bins = 256;
    const int hist_width = 512;
    const int hist_height = 256;
    int histogram[hist_bins] = {0};
    
    // Calculate histogram
    for (int i = 0; i < size; i++) {
        int32_t value = buffer[i];
        if (value < 0) value = 0;
        if (value > max_value) value = max_value;
        int bin = (value * (hist_bins - 1)) / max_value;
        histogram[bin]++;
    }
    
    // Find max count for normalization
    int max_count = 0;
    for (int i = 0; i < hist_bins; i++) {
        if (histogram[i] > max_count) {
            max_count = histogram[i];
        }
    }
    
    if (max_count == 0) max_count = 1; // Avoid division by zero
    
    // Create histogram image
    uint8_t *hist_image = calloc(hist_width * hist_height * 3, 1);
    if (!hist_image) {
        printf("ERROR: Failed to allocate histogram buffer\n");
        return -1;
    }
    
    // Draw histogram bars
    for (int bin = 0; bin < hist_bins; bin++) {
        int bar_height = (histogram[bin] * hist_height) / max_count;
        int x_start = (bin * hist_width) / hist_bins;
        int x_end = ((bin + 1) * hist_width) / hist_bins;
        
        for (int x = x_start; x < x_end; x++) {
            for (int y = hist_height - bar_height; y < hist_height; y++) {
                int idx = (y * hist_width + x) * 3;
                hist_image[idx + 0] = 255; // White bars
                hist_image[idx + 1] = 255;
                hist_image[idx + 2] = 255;
            }
        }
    }
    
    // Generate full filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_%s_%s_histogram.png", 
             output_dir, timestamp, filename, stage_name);
    
    // Save PNG file
    int result = stbi_write_png(full_path, hist_width, hist_height, 3, hist_image, hist_width * 3);
    
    free(hist_image);
    
    if (result) {
        printf("🔧 IMAGE_DEBUG: Saved histogram: %s\n", full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save histogram: %s\n", full_path);
        return -1;
    }
#else
    (void)buffer; (void)size; (void)filename; (void)stage_name; (void)max_value;
    return 0; // Debug disabled
#endif
}

int image_debug_capture_mono_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                     int32_t *grayscale_data, int32_t *processed_data, int frame_number) {
#ifdef ENABLE_IMAGE_DEBUG
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
#else
    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)grayscale_data; 
    (void)processed_data; (void)frame_number;
    return 0; // Debug disabled
#endif
}

int image_debug_capture_stereo_pipeline(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                                       int32_t *warm_raw __attribute__((unused)), int32_t *cold_raw __attribute__((unused)),
                                       int32_t *warm_processed, int32_t *cold_processed, int frame_number) {
#ifdef ENABLE_IMAGE_DEBUG
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
#else
    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)warm_raw; (void)cold_raw;
    (void)warm_processed; (void)cold_processed; (void)frame_number;
    return 0; // Debug disabled
#endif
}

int image_debug_should_capture(int frame_number) {
#ifdef ENABLE_IMAGE_DEBUG
#ifdef DEBUG_FORCE_CAPTURE_TEST_DATA
    // Force capture even with test data
    return (frame_number % 1) == 0; // Capture every frame when forced
#else
    return (frame_number % 1) == 0; // Capture every frame by default
#endif
#else
    (void)frame_number;
    return 0; // Debug disabled
#endif
}

int image_debug_cleanup_old_files(void) {
#ifdef DEBUG_IMAGE_SAVE_TO_FILES
    // This is a simplified cleanup - in a real implementation,
    // you would scan the directory and remove old files based on
    // DEBUG_IMAGE_MAX_FILES setting
    printf("🔧 IMAGE_DEBUG: Cleanup old files (simplified implementation)\n");
    return 0;
#else
    return 0; // Debug disabled
#endif
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

/**
 * @brief Initialize temporal scan buffers
 * @retval 0 on success, -1 on error
 */
#ifdef DEBUG_TEMPORAL_SCAN
static int init_temporal_scans(void) {
    if (scans_initialized) {
        return 0;
    }
    
    // Initialize scan buffers
    const char *scan_names[] = {"grayscale", "processed", "original"};
    
    for (int i = 0; i < MAX_SCAN_TYPES; i++) {
        scans[i].width = CIS_MAX_PIXELS_NB;
        scans[i].max_height = MAX_SCAN_HEIGHT;
        scans[i].current_height = 0;
        scans[i].initialized = 0;
        strncpy(scans[i].name, scan_names[i], sizeof(scans[i].name) - 1);
        scans[i].name[sizeof(scans[i].name) - 1] = '\0';
        
        // Allocate buffer (RGB format for visualization)
        scans[i].buffer = calloc(scans[i].width * scans[i].max_height * 3, sizeof(uint8_t));
        if (!scans[i].buffer) {
            printf("ERROR: Failed to allocate temporal scan buffer for %s\n", scans[i].name);
            return -1;
        }
    }
    
    scans_initialized = 1;
    printf("🔧 TEMPORAL_SCAN: Initialized %d scan buffers (%dx%d each)\n", 
           MAX_SCAN_TYPES, CIS_MAX_PIXELS_NB, MAX_SCAN_HEIGHT);
    return 0;
}

/**
 * @brief Find scan index by name
 * @param scan_type Name of scan type
 * @retval Index or -1 if not found
 */
static int find_scan_index(const char *scan_type) {
    for (int i = 0; i < MAX_SCAN_TYPES; i++) {
        if (strcmp(scans[i].name, scan_type) == 0) {
            return i;
        }
    }
    return -1;
}
#endif

int image_debug_add_scan_line_rgb(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B, int width, const char *scan_type) {
#ifdef ENABLE_IMAGE_DEBUG
#ifdef DEBUG_TEMPORAL_SCAN
    if (!debug_initialized) {
        return -1;
    }
    
    // Initialize temporal scans if needed
    if (init_temporal_scans() != 0) {
        return -1;
    }
    
    // Find scan by type
    int scan_idx = find_scan_index(scan_type);
    if (scan_idx < 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &scans[scan_idx];
    
    // Check if buffer is full
    if (scan->current_height >= scan->max_height) {
        printf("🚨 TEMPORAL_SCAN: Buffer full for %s, auto-saving\n", scan_type);
        image_debug_save_scan(scan_type);
        image_debug_reset_scan(scan_type);
    }
    
    // Add RGB line to scan buffer
    for (int x = 0; x < width && x < scan->width; x++) {
        int idx = (scan->current_height * scan->width + x) * 3;
        scan->buffer[idx + 0] = buffer_R[x];
        scan->buffer[idx + 1] = buffer_G[x];
        scan->buffer[idx + 2] = buffer_B[x];
    }
    
    scan->current_height++;
    scan->initialized = 1;
    
    return 0;
#else
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)width; (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
#endif
#else
    (void)buffer_R; (void)buffer_G; (void)buffer_B; (void)width; (void)scan_type;
    return 0; // Debug disabled
#endif
}

int image_debug_add_scan_line(int32_t *buffer_data, int width, const char *scan_type) {
#ifdef ENABLE_IMAGE_DEBUG
#ifdef DEBUG_TEMPORAL_SCAN
    if (!debug_initialized || !buffer_data) {
        return -1;
    }
    
    // Initialize temporal scans if needed
    if (init_temporal_scans() != 0) {
        return -1;
    }
    
    // Find scan by type
    int scan_idx = find_scan_index(scan_type);
    if (scan_idx < 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &scans[scan_idx];
    
    // Check if buffer is full
    if (scan->current_height >= scan->max_height) {
        printf("🚨 TEMPORAL_SCAN: Buffer full for %s, auto-saving\n", scan_type);
        image_debug_save_scan(scan_type);
        image_debug_reset_scan(scan_type);
    }
    
    // Convert 32-bit data to 8-bit RGB (grayscale)
    for (int x = 0; x < width && x < scan->width; x++) {
        int32_t value = buffer_data[x];
        if (value < 0) value = 0;
        if (value > VOLUME_AMP_RESOLUTION) value = VOLUME_AMP_RESOLUTION;
        
        uint8_t gray_value = (uint8_t)((value * 255) / VOLUME_AMP_RESOLUTION);
        
        int idx = (scan->current_height * scan->width + x) * 3;
        scan->buffer[idx + 0] = gray_value;
        scan->buffer[idx + 1] = gray_value;
        scan->buffer[idx + 2] = gray_value;
    }
    
    scan->current_height++;
    scan->initialized = 1;
    
    return 0;
#else
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)buffer_data; (void)width; (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
#endif
#else
    (void)buffer_data; (void)width; (void)scan_type;
    return 0; // Debug disabled
#endif
}

int image_debug_save_scan(const char *scan_type) {
#ifdef ENABLE_IMAGE_DEBUG
#ifdef DEBUG_TEMPORAL_SCAN
    if (!debug_initialized) {
        return -1;
    }
    
    // Find scan by type
    int scan_idx = find_scan_index(scan_type);
    if (scan_idx < 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &scans[scan_idx];
    if (!scan->initialized || scan->current_height == 0) {
        return 0; // Nothing to save
    }
    
    // Generate filename with timestamp
    char timestamp[32];
    get_timestamp_string(timestamp, sizeof(timestamp));
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s_temporal_scan_%s.png", 
             output_dir, timestamp, scan_type);
    
    // Save PNG file
    int result = stbi_write_png(full_path, scan->width, scan->current_height, 3, 
                               scan->buffer, scan->width * 3);
    
    if (result) {
        printf("🔧 TEMPORAL_SCAN: Saved %s scan (%dx%d): %s\n", 
               scan_type, scan->width, scan->current_height, full_path);
        return 0;
    } else {
        printf("ERROR: Failed to save temporal scan: %s\n", full_path);
        return -1;
    }
#else
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
#endif
#else
    (void)scan_type;
    return 0; // Debug disabled
#endif
}

int image_debug_reset_scan(const char *scan_type) {
#ifdef ENABLE_IMAGE_DEBUG
#ifdef DEBUG_TEMPORAL_SCAN
    if (!scans_initialized) {
        return -1;
    }
    
    // Find scan by type
    int scan_idx = find_scan_index(scan_type);
    if (scan_idx < 0) {
        return -1;
    }
    
    temporal_scan_t *scan = &scans[scan_idx];
    scan->current_height = 0;
    
    // Clear buffer (fill with black)
    if (scan->buffer) {
        memset(scan->buffer, 0, scan->width * scan->max_height * 3);
    }
    
    printf("🔧 TEMPORAL_SCAN: Reset %s scan buffer\n", scan_type);
    return 0;
#else
    // Function exists for linking compatibility, but temporal scan functionality requires DEBUG_TEMPORAL_SCAN
    (void)scan_type;
    return 0; // Stub implementation - temporal scan not enabled
#endif
#else
    (void)scan_type;
    return 0; // Debug disabled
#endif
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
