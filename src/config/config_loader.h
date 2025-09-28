/* config_loader.h */

#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <stdint.h>

/**************************************************************************************
 * Additive Synthesis Runtime Configuration Structure
 **************************************************************************************/
typedef struct {
    // Auto-volume parameters (runtime configurable)
    int auto_volume_enabled;             // Enable/disable IMU-based auto-volume (0/1)
    int imu_inactivity_timeout_s;        // Timeout before switching to inactive mode (seconds)
    float auto_volume_inactive_level;    // Volume level when inactive (0.0-1.0)
    int auto_volume_fade_ms;             // Fade duration for volume transitions (ms)
    
    // Synthesis parameters
    float start_frequency;
    int semitone_per_octave;
    int comma_per_semitone;
    int volume_ramp_up_divisor;    // Higher value = slower volume increase
    int volume_ramp_down_divisor;  // Higher value = slower volume decrease
    int pixels_per_note;
    int invert_intensity;         // 0 = white brightest gives loudest sound, 1 = dark pixels give more energy
    
    // Stereo processing parameters
    int stereo_mode_enabled;                   // Enable/disable stereo mode (0/1)
    float stereo_temperature_amplification;    // Global stereo intensity control
    float stereo_blue_red_weight;              // Weight for blue-red opponent axis
    float stereo_cyan_yellow_weight;           // Weight for cyan-yellow opponent axis
    float stereo_temperature_curve_exponent;   // Exponent for response curve shaping
} additive_synth_config_t;

/**************************************************************************************
 * Global Configuration Instance
 **************************************************************************************/
extern additive_synth_config_t g_additive_config;

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
void validate_config(const additive_synth_config_t* config);

#endif // CONFIG_LOADER_H
