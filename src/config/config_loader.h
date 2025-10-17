/* config_loader.h */

#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <stdint.h>

/**************************************************************************************
 * Sp3ctra Runtime Configuration Structure
 **************************************************************************************/
typedef struct {
    // Audio system parameters (runtime configurable)
    int sampling_frequency;              // Sampling frequency in Hz (22050, 44100, 48000, 96000)
    int audio_buffer_size;               // Audio buffer size in frames
    
    // Auto-volume parameters (runtime configurable)
    int auto_volume_enabled;             // Enable/disable IMU-based auto-volume (0/1)
    int imu_inactivity_timeout_s;        // Timeout before switching to inactive mode (seconds)
    float auto_volume_inactive_level;    // Volume level when inactive (0.0-1.0)
    int auto_volume_fade_ms;             // Fade duration for volume transitions (ms)
    
    // Anti-vibrations acoustiques (runtime configurable)
    float imu_sensitivity;               // IMU sensitivity (0.1-10.0, default 1.0)
    float vibration_protection_factor;   // Threshold hardening factor when audio loud (1.0-5.0, default 3.0)
    
    // Synthesis parameters
    float start_frequency;
    int semitone_per_octave;
    int comma_per_semitone;
    int pixels_per_note;
    int invert_intensity;         // 0 = white brightest gives loudest sound, 1 = dark pixels give more energy

    // Envelope slew parameters (runtime configurable; defaults from compile-time defines)
    float tau_up_base_ms;             // Base attack time in milliseconds
    float tau_down_base_ms;           // Base release time in milliseconds
    float decay_freq_ref_hz;          // Reference frequency in Hz for frequency weighting
    float decay_freq_beta;            // >0 slows highs, <0 speeds highs

    // Stereo processing parameters
    int stereo_mode_enabled;                   // Enable/disable stereo mode (0/1)
    float stereo_temperature_amplification;    // Global stereo intensity control
    float stereo_blue_red_weight;              // Weight for blue-red opponent axis
    float stereo_cyan_yellow_weight;           // Weight for cyan-yellow opponent axis
    float stereo_temperature_curve_exponent;   // Exponent for response curve shaping
    
    // Summation normalization parameters
    float volume_weighting_exponent;           // Volume weighting exponent (1.0=linear, 2.0=quadratic, 3.0=cubic)
    float summation_response_exponent;         // Final response curve exponent (0.5=anti-compress, 1.0=linear, 1.5+=compress)
    
    // Noise gate and soft limiter parameters
    float noise_gate_threshold;                // Noise gate threshold (0.0-1.0, relative to VOLUME_AMP_RESOLUTION)
    float soft_limit_threshold;                // Soft limiter threshold (0.0-1.0)
    float soft_limit_knee;                     // Soft limiter knee width (0.0-1.0)
    
    // Image processing and contrast parameters
    float contrast_min;                        // Minimum volume for blurred images (0.0-1.0)
    float contrast_stride;                     // Pixel sampling stride for optimization
    float contrast_adjustment_power;           // Exponent for adjusting the contrast curve
    int enable_non_linear_mapping;             // Enable/disable non-linear gamma mapping (0/1)
    float gamma_value;                         // Gamma value for non-linear intensity correction
} sp3ctra_config_t;

/**************************************************************************************
 * Global Configuration Instance
 **************************************************************************************/
extern sp3ctra_config_t g_sp3ctra_config;

/**************************************************************************************
 * Configuration Loading Functions
 **************************************************************************************/

/**
 * Load additive synthesis configuration from INI file
 * Creates the file with default values if it doesn't exist
 * Exits the program with error message if validation fails
 * 
 * @param config_file_path Path to the configuration file
 * @return 0 on success, -1 on error
 */
int load_additive_config(const char* config_file_path);

/**
 * Create default configuration file with current default values
 * 
 * @param config_file_path Path where to create the configuration file
 * @return 0 on success, -1 on error
 */
int create_default_config_file(const char* config_file_path);

/**
 * Validate configuration parameters
 * Exits the program if any parameter is out of valid range
 * 
 * @param config Configuration structure to validate
 */
void validate_config(const sp3ctra_config_t* config);

#endif // CONFIG_LOADER_H
