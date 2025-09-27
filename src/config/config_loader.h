/* config_loader.h */

#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <stdint.h>

/**************************************************************************************
 * Additive Synthesis Runtime Configuration Structure
 **************************************************************************************/
typedef struct {
    // Auto-volume parameters
    float imu_active_threshold_x;
    float imu_filter_alpha_x;
    int imu_inactivity_timeout_s;
    float auto_volume_inactive_level;
    float auto_volume_active_level;
    int auto_volume_fade_ms;
    int auto_volume_poll_ms;
    
    // Synthesis parameters
    float start_frequency;
    int semitone_per_octave;
    int comma_per_semitone;
    int volume_ramp_up_divisor;    // Higher value = slower volume increase
    int volume_ramp_down_divisor;  // Higher value = slower volume decrease
    int pixels_per_note;
    int invert_intensity;         // 0 = white brightest gives loudest sound, 1 = dark pixels give more energy
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
