/* config_loader.c - Refactored with improved error handling and table-driven parsing */

#include "config_loader.h"
#include "config_parser_table.h"
#include "config_synth_additive.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

/**************************************************************************************
 * Constants
 **************************************************************************************/
#define MAX_LINE_LENGTH 1024
#define MAX_SECTION_LENGTH 128

/**************************************************************************************
 * Global Configuration Instance
 **************************************************************************************/
sp3ctra_config_t g_sp3ctra_config;

/**************************************************************************************
 * Default Values
 **************************************************************************************/
static const sp3ctra_config_t DEFAULT_CONFIG = {
    // Logging configuration
    .log_level = LOG_LEVEL_INFO,
    
    // Audio system parameters
    .sampling_frequency = 48000,
    .audio_buffer_size = 80,
    
    // Auto-volume parameters
    .auto_volume_enabled = 0,
    .imu_inactivity_timeout_s = 5,
    .auto_volume_inactive_level = 0.01f,
    .auto_volume_fade_ms = 600,
    
    // Anti-vibrations acoustiques
    .imu_sensitivity = 1.0f,
    .vibration_protection_factor = 3.0f,
    .contrast_change_threshold = 0.05f,
    
    // Synthesis parameters
    // User-configurable synthesis parameters
    .low_frequency = 65.41f,      // C2
    .high_frequency = 16744.04f,  // ~8 octaves above (65.41 * 2^8)
    .sensor_dpi = 400,
    .invert_intensity = 1,
    
    // Auto-calculated synthesis parameters (will be computed at runtime)
    .start_frequency = 65.41f,
    .semitone_per_octave = 12,
    .comma_per_semitone = 36,
    .pixels_per_note = 1,

    // Envelope slew parameters
    .tau_up_base_ms = 0.5f,
    .tau_down_base_ms = 0.5f,
    .decay_freq_ref_hz = 440.0f,
    .decay_freq_beta = -1.2f,
    
    // Stereo processing parameters
    .stereo_mode_enabled = 0,
    .stereo_temperature_amplification = 2.5f,
    .stereo_blue_red_weight = 0.8f,
    .stereo_cyan_yellow_weight = 0.2f,
    .stereo_temperature_curve_exponent = 1.0f,
    
    // Summation normalization parameters
    .volume_weighting_exponent = 0.1f,
    .summation_response_exponent = 2.0f,
    
    // Noise gate and soft limiter parameters
    .noise_gate_threshold = NOISE_GATE_THRESHOLD_DEFAULT,
    .soft_limit_threshold = SOFT_LIMIT_THRESHOLD_DEFAULT,
    .soft_limit_knee = SOFT_LIMIT_KNEE_DEFAULT,
    
    // Image processing and contrast parameters
    .contrast_min = CONTRAST_MIN,
    .contrast_stride = CONTRAST_STRIDE,
    .contrast_adjustment_power = CONTRAST_ADJUSTMENT_POWER,
    .enable_non_linear_mapping = ENABLE_NON_LINEAR_MAPPING,
    .gamma_value = GAMMA_VALUE,
    
    // Photowave synthesis parameters
    .photowave_continuous_mode = 0,     // 0 = Only on MIDI notes
    .photowave_scan_mode = 0,           // 0 = Left to Right
    .photowave_interp_mode = 0,         // 0 = Linear interpolation
    .photowave_amplitude = 0.5f,        // 50% amplitude
    
    // Polyphonic synthesis parameters
    .poly_num_voices = 8,               // 8 polyphonic voices
    .poly_max_oscillators = 128,        // 128 oscillators per voice
    
    // Polyphonic ADSR Volume parameters
    .poly_volume_adsr_attack_s = 0.01f,
    .poly_volume_adsr_decay_s = 0.1f,
    .poly_volume_adsr_sustain_level = 0.8f,
    .poly_volume_adsr_release_s = 0.2f,
    
    // Polyphonic ADSR Filter parameters
    .poly_filter_adsr_attack_s = 0.02f,
    .poly_filter_adsr_decay_s = 0.2f,
    .poly_filter_adsr_sustain_level = 0.1f,
    .poly_filter_adsr_release_s = 0.3f,
    
    // Polyphonic LFO parameters
    .poly_lfo_rate_hz = 5.0f,
    .poly_lfo_depth_semitones = 0.25f,
    
    // Polyphonic spectral filter parameters
    .poly_filter_cutoff_hz = 8000.0f,
    .poly_filter_env_depth_hz = -7800.0f,
    
    // Polyphonic performance parameters
    .poly_master_volume = 0.20f,
    .poly_amplitude_gamma = 2.0f,
    .poly_min_audible_amplitude = 0.001f,
    .poly_max_harmonics_per_voice = 32,
    .poly_high_freq_harmonic_limit_hz = 8000.0f,
    
    // Polyphonic advanced parameters
    .poly_amplitude_smoothing_alpha = 0.1f,
    .poly_norm_factor_bin0 = 881280.0f * 1.1f,
    .poly_norm_factor_harmonics = 220320.0f * 2.0f
};

/**************************************************************************************
 * Helper Functions
 **************************************************************************************/

/**
 * Trim whitespace from string in-place
 */
static void trim_whitespace_inplace(char* str) {
    if (!str || *str == '\0') return;
    
    // Trim leading space
    char* start = str;
    while (*start == ' ' || *start == '\t') start++;
    
    // Trim trailing space
    char* end = str + strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    
    // Move trimmed content to start of buffer if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * Parse a float value with error checking
 */
static int parse_float(const char* value_str, float* result, const char* param_name) {
    char* endptr;
    errno = 0;
    *result = strtof(value_str, &endptr);
    
    if (errno != 0 || endptr == value_str || *endptr != '\0') {
        log_error("CONFIG", "Invalid float value '%s' for parameter '%s'", 
                  value_str, param_name);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    return CONFIG_SUCCESS;
}

/**
 * Parse an integer value with error checking
 */
static int parse_int(const char* value_str, int* result, const char* param_name) {
    char* endptr;
    errno = 0;
    *result = (int)strtol(value_str, &endptr, 10);
    
    if (errno != 0 || endptr == value_str || *endptr != '\0') {
        log_error("CONFIG", "Invalid integer value '%s' for parameter '%s'", 
                  value_str, param_name);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    return CONFIG_SUCCESS;
}

/**
 * Check if parameter is deprecated
 */
static int is_deprecated_param(const char* section, const char* key, const char** replacement) {
    for (size_t i = 0; i < DEPRECATED_PARAMS_COUNT; i++) {
        if (strcmp(DEPRECATED_PARAMS[i].section, section) == 0 &&
            strcmp(DEPRECATED_PARAMS[i].key, key) == 0) {
            *replacement = DEPRECATED_PARAMS[i].replacement;
            return 1;
        }
    }
    return 0;
}

/**
 * Parse and set parameter using table-driven approach
 */
static int parse_and_set_param(const config_param_def_t* param, 
                               const char* value_str,
                               sp3ctra_config_t* config,
                               int line_number) {
    void* target = (char*)config + param->offset;
    
    switch (param->type) {
        case PARAM_TYPE_INT:
        case PARAM_TYPE_BOOL: {
            int value;
            if (parse_int(value_str, &value, param->key) != CONFIG_SUCCESS) {
                return CONFIG_ERROR_INVALID_VALUE;
            }
            if (value < (int)param->min_value || value > (int)param->max_value) {
                config_log_error(line_number, 
                    "%s out of range [%d, %d]: %d",
                    param->key, (int)param->min_value, (int)param->max_value, value);
                return CONFIG_ERROR_INVALID_VALUE;
            }
            *(int*)target = value;
            break;
        }
        
        case PARAM_TYPE_FLOAT: {
            float value;
            if (parse_float(value_str, &value, param->key) != CONFIG_SUCCESS) {
                return CONFIG_ERROR_INVALID_VALUE;
            }
            if (value < param->min_value || value > param->max_value) {
                config_log_error(line_number,
                    "%s out of range [%.3f, %.3f]: %.3f",
                    param->key, param->min_value, param->max_value, value);
                return CONFIG_ERROR_INVALID_VALUE;
            }
            *(float*)target = value;
            break;
        }
    }
    
    return CONFIG_SUCCESS;
}

/**
 * Case-insensitive string comparison helper
 */
static int str_equals_ignore_case(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'a' && *s1 <= 'z') ? *s1 - 32 : *s1;
        char c2 = (*s2 >= 'a' && *s2 <= 'z') ? *s2 - 32 : *s2;
        if (c1 != c2) return 0;
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

/**
 * Parse log level from string
 */
static int parse_log_level(const char* value_str, log_level_t* result) {
    if (str_equals_ignore_case(value_str, "ERROR")) {
        *result = LOG_LEVEL_ERROR;
        return CONFIG_SUCCESS;
    } else if (str_equals_ignore_case(value_str, "WARNING")) {
        *result = LOG_LEVEL_WARNING;
        return CONFIG_SUCCESS;
    } else if (str_equals_ignore_case(value_str, "INFO")) {
        *result = LOG_LEVEL_INFO;
        return CONFIG_SUCCESS;
    } else if (str_equals_ignore_case(value_str, "DEBUG")) {
        *result = LOG_LEVEL_DEBUG;
        return CONFIG_SUCCESS;
    }
    return CONFIG_ERROR_INVALID_VALUE;
}

/**
 * Check if a parameter is MIDI-controllable (handled by midi_mapping.c)
 * These parameters should be ignored by config_loader.c
 */
static int is_midi_parameter(const char* section, const char* key) {
    // First check for MIDI suffixes
    size_t len = strlen(key);
    if (len > 4 && strcmp(key + len - 4, "_min") == 0) return 1;
    if (len > 4 && strcmp(key + len - 4, "_max") == 0) return 1;
    if (len > 8 && strcmp(key + len - 8, "_scaling") == 0) return 1;
    if (len > 5 && strcmp(key + len - 5, "_type") == 0) return 1;
    
    // Check for MIDI-controllable parameters by section
    if (strcmp(section, "audio_global") == 0) {
        // All audio_global parameters are MIDI-controllable
        return 1;
    }
    
    if (strcmp(section, "synth_additive") == 0) {
        // MIDI-controllable additive synth parameters
        if (strcmp(key, "volume") == 0) return 1;
        if (strcmp(key, "reverb_send") == 0) return 1;
        if (strstr(key, "envelope_") == key) return 1;  // envelope_*
        if (strcmp(key, "stereo_mode_enabled") == 0) return 1;
    }
    
    if (strcmp(section, "synth_photowave") == 0) {
        // All synth_photowave parameters are MIDI-controllable
        return 1;
    }
    
    if (strcmp(section, "synth_polyphonic") == 0) {
        // All synth_polyphonic parameters are MIDI-controllable
        return 1;
    }
    
    if (strcmp(section, "sequencer_global") == 0) {
        // All sequencer_global parameters are MIDI-controllable
        return 1;
    }
    
    if (strcmp(section, "sequencer_player_defaults") == 0) {
        // All sequencer player parameters are MIDI-controllable
        return 1;
    }
    
    if (strcmp(section, "system") == 0) {
        // System control parameters are MIDI-controllable
        if (strcmp(key, "freeze") == 0) return 1;
        if (strcmp(key, "resume") == 0) return 1;
    }
    
    return 0;
}

/**
 * Parse key-value pair using parameter table
 */
static int parse_key_value(const char* section, const char* key, const char* value,
                          sp3ctra_config_t* config, int line_number) {
    // Silently ignore MIDI-controllable parameters (handled by midi_mapping.c)
    if (is_midi_parameter(section, key)) {
        return CONFIG_SUCCESS;
    }
    
    // Handle Log_level as a global parameter (empty section) or in [system] section
    if ((strcmp(section, "") == 0 || strcmp(section, "system") == 0) && 
        (strcmp(key, "Log_level") == 0 || strcmp(key, "log_level") == 0)) {
        log_level_t level;
        if (parse_log_level(value, &level) != CONFIG_SUCCESS) {
            config_log_error(line_number, 
                "Invalid log level '%s' (valid: ERROR, WARNING, INFO, DEBUG)", value);
            return CONFIG_ERROR_INVALID_VALUE;
        }
        config->log_level = level;
        return CONFIG_SUCCESS;
    }
    
    // Handle [LOGGING] section specially (backward compatibility)
    if (strcmp(section, "LOGGING") == 0) {
        if (strcmp(key, "log_level") == 0) {
            log_level_t level;
            if (parse_log_level(value, &level) != CONFIG_SUCCESS) {
                config_log_error(line_number, 
                    "Invalid log level '%s' (valid: ERROR, WARNING, INFO, DEBUG)", value);
                return CONFIG_ERROR_INVALID_VALUE;
            }
            config->log_level = level;
            return CONFIG_SUCCESS;
        } else {
            config_log_warning(line_number, "Unknown parameter '%s' in section 'LOGGING'", key);
            return CONFIG_SUCCESS;
        }
    }
    
    // Search in parameter table
    for (size_t i = 0; i < CONFIG_PARAMS_COUNT; i++) {
        const config_param_def_t* param = &CONFIG_PARAMS[i];
        
        if (strcmp(param->section, section) == 0 && strcmp(param->key, key) == 0) {
            return parse_and_set_param(param, value, config, line_number);
        }
    }
    
    // Check for deprecated parameters
    const char* replacement = NULL;
    if (is_deprecated_param(section, key, &replacement)) {
        config_log_info(line_number, 
            "Parameter '%s' in section '%s' is deprecated (replaced by: %s), ignoring",
            key, section, replacement);
        return CONFIG_SUCCESS;
    }
    
    config_log_warning(line_number, "Unknown parameter '%s' in section '%s'",
                      key, section);
    return CONFIG_SUCCESS; // Don't fail on unknown params
}

/**************************************************************************************
 * Automatic Parameter Calculation
 **************************************************************************************/

/**
 * Calculate synthesis parameters automatically from frequency range and DPI
 * This function computes:
 * - comma_per_semitone: based on available pixels and frequency range
 * - pixels_per_note: always 1 for maximum resolution
 * - semitone_per_octave: always 12 (standard musical scale)
 * - start_frequency: same as low_frequency
 */
static int calculate_synthesis_params(sp3ctra_config_t* config) {
    // Determine number of pixels based on DPI
    int nb_pixels;
    if (config->sensor_dpi == 400) {
        nb_pixels = CIS_400DPI_PIXELS_NB;
    } else if (config->sensor_dpi == 200) {
        nb_pixels = CIS_200DPI_PIXELS_NB;
    } else {
        config_log_error(0, "Invalid sensor_dpi: %d (must be 200 or 400)", config->sensor_dpi);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    
    // Calculate number of octaves
    if (config->low_frequency <= 0.0f || config->high_frequency <= 0.0f) {
        config_log_error(0, "Invalid frequency range: low=%.2f Hz, high=%.2f Hz",
                        config->low_frequency, config->high_frequency);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    
    if (config->high_frequency <= config->low_frequency) {
        config_log_error(0, "high_frequency (%.2f Hz) must be greater than low_frequency (%.2f Hz)",
                        config->high_frequency, config->low_frequency);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    
    float nb_octaves = log2f(config->high_frequency / config->low_frequency);
    
    // Calculate number of semitones (always 12 per octave)
    config->semitone_per_octave = 12;
    float nb_semitones = nb_octaves * config->semitone_per_octave;
    
    // Maximum resolution: 1 pixel = 1 comma
    config->pixels_per_note = 1;
    
    // Calculate commas per semitone
    config->comma_per_semitone = (int)(nb_pixels / nb_semitones);
    
    // Ensure we have at least 1 comma per semitone
    if (config->comma_per_semitone < 1) {
        config_log_error(0, 
            "Frequency range too wide for sensor resolution: %.2f octaves requires %d commas/semitone (minimum 1)",
            nb_octaves, config->comma_per_semitone);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    
    // Set start_frequency (backward compatibility)
    config->start_frequency = config->low_frequency;
    
    // Log the calculated parameters
    config_log_info(0, "Calculated synthesis parameters:");
    config_log_info(0, "  - Frequency range: %.2f Hz to %.2f Hz (%.2f octaves)",
                   config->low_frequency, config->high_frequency, nb_octaves);
    config_log_info(0, "  - Sensor DPI: %d (%d pixels)", config->sensor_dpi, nb_pixels);
    config_log_info(0, "  - Resolution: %d commas per semitone", config->comma_per_semitone);
    config_log_info(0, "  - Total notes: %d", nb_pixels / config->pixels_per_note);
    
    return CONFIG_SUCCESS;
}

/**************************************************************************************
 * Cross-Parameter Validation
 **************************************************************************************/

static int validate_cross_param_constraints(const sp3ctra_config_t* config) {
    int errors = 0;
    
    // Check pixels_per_note divides CIS_MAX_PIXELS_NB
    if ((CIS_MAX_PIXELS_NB % config->pixels_per_note) != 0) {
        config_log_error(0, 
            "pixels_per_note (%d) must divide evenly into CIS_MAX_PIXELS_NB (%d)",
            config->pixels_per_note, CIS_MAX_PIXELS_NB);
        errors++;
    }
    
    // Warn if tau_up is much larger than tau_down (unusual)
    if (config->tau_up_base_ms > config->tau_down_base_ms * 2.0f) {
        config_log_warning(0,
            "tau_up_base_ms (%.3f) is much larger than tau_down_base_ms (%.3f) - unusual configuration",
            config->tau_up_base_ms, config->tau_down_base_ms);
    }
    
    return errors > 0 ? CONFIG_ERROR_VALIDATION_FAILED : CONFIG_SUCCESS;
}

/**************************************************************************************
 * Configuration Validation
 **************************************************************************************/

int validate_config(const sp3ctra_config_t* config) {
    // Individual parameter validation is done during parsing
    // Here we only check cross-parameter constraints
    int result = validate_cross_param_constraints(config);
    
    if (result == CONFIG_SUCCESS) {
        config_log_info(0, "Configuration validation passed");
    } else {
        config_log_error(0, "Configuration validation failed");
    }
    
    return result;
}

/**************************************************************************************
 * Configuration File Creation
 **************************************************************************************/

int create_default_config_file(const char* config_file_path) {
    FILE* file = fopen(config_file_path, "w");
    if (!file) {
        config_log_error(0, "Cannot create config file '%s': %s", 
                  config_file_path, strerror(errno));
        return CONFIG_ERROR_FILE_NOT_FOUND;
    }
    
    fprintf(file, "# Sp3ctra Configuration\n");
    fprintf(file, "# This file was automatically generated with default values\n");
    fprintf(file, "# Modify these values as needed - the program will validate them on startup\n\n");
    
    fprintf(file, "[audio]\n");
    fprintf(file, "# Audio system configuration - optimized for real-time synthesis\n");
    fprintf(file, "# Sampling frequency in Hz (48000, 96000)\n");
    fprintf(file, "sampling_frequency = %d\n", DEFAULT_CONFIG.sampling_frequency);
    fprintf(file, "\n");
    fprintf(file, "# Audio buffer size in frames - affects latency and stability\n");
    fprintf(file, "# Smaller values = lower latency, larger values = more stable\n");
    fprintf(file, "audio_buffer_size = %d\n", DEFAULT_CONFIG.audio_buffer_size);
    fprintf(file, "\n");
    
    fprintf(file, "[auto_volume]\n");
    fprintf(file, "# Enable/disable IMU-based auto-volume (0=disabled, 1=enabled)\n");
    fprintf(file, "auto_volume_enabled = %d\n", DEFAULT_CONFIG.auto_volume_enabled);
    fprintf(file, "\n");
    fprintf(file, "# Timeout before switching to inactive mode (seconds)\n");
    fprintf(file, "imu_inactivity_timeout_s = %d\n", DEFAULT_CONFIG.imu_inactivity_timeout_s);
    fprintf(file, "\n");
    fprintf(file, "# Volume level when inactive (0.0-1.0)\n");
    fprintf(file, "auto_volume_inactive_level = %.3f\n", DEFAULT_CONFIG.auto_volume_inactive_level);
    fprintf(file, "\n");
    fprintf(file, "# Fade duration for volume transitions (ms)\n");
    fprintf(file, "auto_volume_fade_ms = %d\n", DEFAULT_CONFIG.auto_volume_fade_ms);
    fprintf(file, "\n");
    
    fprintf(file, "[synthesis]\n");
    fprintf(file, "# Frequency range configuration\n");
    fprintf(file, "# These values define the musical range that will be mapped across the sensor\n");
    fprintf(file, "# Starting frequency in Hz (C2)\n");
    fprintf(file, "low_frequency = %.2f\n", DEFAULT_CONFIG.low_frequency);
    fprintf(file, "\n");
    fprintf(file, "# Ending frequency in Hz (~8 octaves above)\n");
    fprintf(file, "high_frequency = %.2f\n", DEFAULT_CONFIG.high_frequency);
    fprintf(file, "\n");
    fprintf(file, "# Sensor DPI configuration (200 or 400)\n");
    fprintf(file, "# The system will automatically calculate the optimal resolution\n");
    fprintf(file, "sensor_dpi = %d\n", DEFAULT_CONFIG.sensor_dpi);
    fprintf(file, "\n");
    fprintf(file, "# Pixel to note mapping and intensity behavior\n");
    fprintf(file, "# 0=bright pixels louder, 1=dark pixels louder\n");
    fprintf(file, "invert_intensity = %d\n", DEFAULT_CONFIG.invert_intensity);
    fprintf(file, "\n");

    fprintf(file, "[envelope_slew]\n");
    fprintf(file, "# Base time constants for envelope slew (multiplied by runtime divisors)\n");
    fprintf(file, "# These values control how quickly volume changes occur\n");
    fprintf(file, "tau_up_base_ms = %.3f\n", DEFAULT_CONFIG.tau_up_base_ms);
    fprintf(file, "tau_down_base_ms = %.3f\n", DEFAULT_CONFIG.tau_down_base_ms);
    fprintf(file, "\n");
    fprintf(file, "# Frequency-dependent release weighting (stabilizes highs vs lows)\n");
    fprintf(file, "# Helps balance volume transitions across different frequency ranges\n");
    fprintf(file, "decay_freq_ref_hz = %.1f\n", DEFAULT_CONFIG.decay_freq_ref_hz);
    fprintf(file, "decay_freq_beta = %.1f\n", DEFAULT_CONFIG.decay_freq_beta);
    fprintf(file, "\n");
    
    fprintf(file, "[stereo_processing]\n");
    fprintf(file, "# Enable/disable stereo mode (0=mono, 1=stereo)\n");
    fprintf(file, "stereo_mode_enabled = %d\n", DEFAULT_CONFIG.stereo_mode_enabled);
    fprintf(file, "\n");
    fprintf(file, "# Controls global stereo intensity\n");
    fprintf(file, "stereo_temperature_amplification = %.1f\n", DEFAULT_CONFIG.stereo_temperature_amplification);
    fprintf(file, "\n");
    fprintf(file, "# Balance between color axes\n");
    fprintf(file, "stereo_blue_red_weight = %.1f\n", DEFAULT_CONFIG.stereo_blue_red_weight);
    fprintf(file, "stereo_cyan_yellow_weight = %.1f\n", DEFAULT_CONFIG.stereo_cyan_yellow_weight);
    fprintf(file, "\n");
    fprintf(file, "# Shape of response curve\n");
    fprintf(file, "stereo_temperature_curve_exponent = %.1f\n", DEFAULT_CONFIG.stereo_temperature_curve_exponent);
    fprintf(file, "\n");
    
    fprintf(file, "[summation_normalization]\n");
    fprintf(file, "# Volume weighting exponent - LOWER values make strong oscillators dominate more\n");
    fprintf(file, "# Valid range: 0.01 to 10.0 (0.1 = strong domination, 1.0 = linear, 2.0+ = flattened)\n");
    fprintf(file, "volume_weighting_exponent = %.1f\n", DEFAULT_CONFIG.volume_weighting_exponent);
    fprintf(file, "\n");
    fprintf(file, "# Final response curve exponent - HIGHER values = MORE compression\n");
    fprintf(file, "# Valid range: 0.1 to 3.0 (0.5 = expand, 1.0 = linear, 2.0+ = compress)\n");
    fprintf(file, "summation_response_exponent = %.1f\n", DEFAULT_CONFIG.summation_response_exponent);
    fprintf(file, "\n");
    
    fprintf(file, "[photowave]\n");
    fprintf(file, "# Photowave synthesis - transforms image lines into audio waveforms\n");
    fprintf(file, "# Continuous mode: 0=only on MIDI notes, 1=always generating\n");
    fprintf(file, "continuous_mode = %d\n", DEFAULT_CONFIG.photowave_continuous_mode);
    fprintf(file, "\n");
    fprintf(file, "# Scanning mode: 0=Left to Right, 1=Right to Left, 2=Dual (ping-pong)\n");
    fprintf(file, "scan_mode = %d\n", DEFAULT_CONFIG.photowave_scan_mode);
    fprintf(file, "\n");
    fprintf(file, "# Interpolation mode: 0=Linear, 1=Cubic\n");
    fprintf(file, "interp_mode = %d\n", DEFAULT_CONFIG.photowave_interp_mode);
    fprintf(file, "\n");
    fprintf(file, "# Amplitude (0.0-1.0)\n");
    fprintf(file, "amplitude = %.2f\n", DEFAULT_CONFIG.photowave_amplitude);
    fprintf(file, "\n");
    
    fprintf(file, "[polyphonic]\n");
    fprintf(file, "# Polyphonic synthesis configuration\n");
    fprintf(file, "# Number of simultaneous polyphonic voices (1-32)\n");
    fprintf(file, "# Higher values = more notes but higher CPU load\n");
    fprintf(file, "# Total oscillators = num_voices × max_oscillators\n");
    fprintf(file, "num_voices = %d\n", DEFAULT_CONFIG.poly_num_voices);
    fprintf(file, "\n");
    fprintf(file, "# Maximum oscillators per voice (1-256)\n");
    fprintf(file, "# Higher values = richer timbre but higher CPU load\n");
    fprintf(file, "# Each oscillator represents one harmonic/FFT bin\n");
    fprintf(file, "max_oscillators = %d\n", DEFAULT_CONFIG.poly_max_oscillators);
    fprintf(file, "\n");
    
    fprintf(file, "# Volume ADSR envelope (controls note amplitude over time)\n");
    fprintf(file, "# Attack: time to reach full volume after note on (seconds)\n");
    fprintf(file, "volume_adsr_attack_s = %.3f\n", DEFAULT_CONFIG.poly_volume_adsr_attack_s);
    fprintf(file, "# Decay: time to fall from peak to sustain level (seconds)\n");
    fprintf(file, "volume_adsr_decay_s = %.3f\n", DEFAULT_CONFIG.poly_volume_adsr_decay_s);
    fprintf(file, "# Sustain: level maintained while note is held (0.0-1.0)\n");
    fprintf(file, "volume_adsr_sustain_level = %.2f\n", DEFAULT_CONFIG.poly_volume_adsr_sustain_level);
    fprintf(file, "# Release: time to fade to silence after note off (seconds)\n");
    fprintf(file, "volume_adsr_release_s = %.3f\n", DEFAULT_CONFIG.poly_volume_adsr_release_s);
    fprintf(file, "\n");
    
    fprintf(file, "# Filter ADSR envelope (controls spectral brightness over time)\n");
    fprintf(file, "# Attack: time for filter to open after note on (seconds)\n");
    fprintf(file, "filter_adsr_attack_s = %.3f\n", DEFAULT_CONFIG.poly_filter_adsr_attack_s);
    fprintf(file, "# Decay: time for filter to close to sustain level (seconds)\n");
    fprintf(file, "filter_adsr_decay_s = %.3f\n", DEFAULT_CONFIG.poly_filter_adsr_decay_s);
    fprintf(file, "# Sustain: filter level maintained while note is held (0.0-1.0)\n");
    fprintf(file, "filter_adsr_sustain_level = %.2f\n", DEFAULT_CONFIG.poly_filter_adsr_sustain_level);
    fprintf(file, "# Release: time for filter to close after note off (seconds)\n");
    fprintf(file, "filter_adsr_release_s = %.3f\n", DEFAULT_CONFIG.poly_filter_adsr_release_s);
    fprintf(file, "\n");
    
    fprintf(file, "# LFO (Low Frequency Oscillator) for vibrato effect\n");
    fprintf(file, "# Rate: vibrato speed in Hz (0.0-30.0)\n");
    fprintf(file, "lfo_rate_hz = %.2f\n", DEFAULT_CONFIG.poly_lfo_rate_hz);
    fprintf(file, "# Depth: vibrato intensity in semitones (-12.0 to 12.0)\n");
    fprintf(file, "lfo_depth_semitones = %.2f\n", DEFAULT_CONFIG.poly_lfo_depth_semitones);
    fprintf(file, "\n");
    
    fprintf(file, "# Spectral filter parameters\n");
    fprintf(file, "# Base cutoff frequency in Hz (20.0-20000.0)\n");
    fprintf(file, "filter_cutoff_hz = %.1f\n", DEFAULT_CONFIG.poly_filter_cutoff_hz);
    fprintf(file, "# Filter envelope depth in Hz (-20000.0 to 20000.0)\n");
    fprintf(file, "# Negative values close the filter, positive values open it\n");
    fprintf(file, "filter_env_depth_hz = %.1f\n", DEFAULT_CONFIG.poly_filter_env_depth_hz);
    fprintf(file, "\n");
    
    fprintf(file, "# Performance and sound shaping parameters\n");
    fprintf(file, "# Master volume (0.0-1.0)\n");
    fprintf(file, "master_volume = %.2f\n", DEFAULT_CONFIG.poly_master_volume);
    fprintf(file, "# Amplitude gamma curve for harmonic shaping (0.1-5.0)\n");
    fprintf(file, "# Higher values = more emphasis on strong harmonics\n");
    fprintf(file, "amplitude_gamma = %.1f\n", DEFAULT_CONFIG.poly_amplitude_gamma);
    fprintf(file, "# Minimum audible amplitude threshold (0.0-0.1)\n");
    fprintf(file, "# Harmonics below this are skipped for CPU optimization\n");
    fprintf(file, "min_audible_amplitude = %.4f\n", DEFAULT_CONFIG.poly_min_audible_amplitude);
    fprintf(file, "# Maximum harmonics per voice for CPU optimization (1-256)\n");
    fprintf(file, "max_harmonics_per_voice = %d\n", DEFAULT_CONFIG.poly_max_harmonics_per_voice);
    fprintf(file, "# High frequency harmonic limit in Hz (1000.0-20000.0)\n");
    fprintf(file, "# Reduces harmonics above this frequency to save CPU\n");
    fprintf(file, "high_freq_harmonic_limit_hz = %.1f\n", DEFAULT_CONFIG.poly_high_freq_harmonic_limit_hz);
    fprintf(file, "\n");
    
    fprintf(file, "# Advanced parameters (fine-tuning)\n");
    fprintf(file, "# Amplitude smoothing factor (0.0-1.0)\n");
    fprintf(file, "# Higher values = more smoothing, lower values = more responsive\n");
    fprintf(file, "amplitude_smoothing_alpha = %.2f\n", DEFAULT_CONFIG.poly_amplitude_smoothing_alpha);
    fprintf(file, "# Normalization factor for fundamental frequency bin\n");
    fprintf(file, "norm_factor_bin0 = %.1f\n", DEFAULT_CONFIG.poly_norm_factor_bin0);
    fprintf(file, "# Normalization factor for harmonic bins\n");
    fprintf(file, "norm_factor_harmonics = %.1f\n", DEFAULT_CONFIG.poly_norm_factor_harmonics);
    fprintf(file, "\n");
    
    fclose(file);
    
    config_log_info(0, "Created default configuration file: %s", config_file_path);
    return CONFIG_SUCCESS;
}

/**************************************************************************************
 * INI File Parser
 **************************************************************************************/

int load_additive_config(const char* config_file_path) {
    FILE* file = fopen(config_file_path, "r");
    
    // If file doesn't exist, create it with default values
    if (!file) {
        config_log_info(0, "Configuration file '%s' not found, creating with default values", 
                 config_file_path);
        int result = create_default_config_file(config_file_path);
        if (result != CONFIG_SUCCESS) {
            return result;
        }
        // Load the default values
        g_sp3ctra_config = DEFAULT_CONFIG;
        return validate_config(&g_sp3ctra_config);
    }
    
    // Initialize with default values
    g_sp3ctra_config = DEFAULT_CONFIG;
    
    char line[MAX_LINE_LENGTH];
    char current_section[MAX_SECTION_LENGTH] = "";
    int line_number = 0;
    int parse_errors = 0;
    
    config_log_info(0, "Loading configuration from: %s", config_file_path);
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        
        // Check for line overflow
        size_t len = strlen(line);
        if (len == sizeof(line)-1 && line[len-1] != '\n') {
            config_log_error(line_number, 
                "Line exceeds maximum length of %d characters", MAX_LINE_LENGTH-1);
            fclose(file);
            return CONFIG_ERROR_INVALID_SYNTAX;
        }
        
        trim_whitespace_inplace(line);
        
        // Skip empty lines and comments
        if (*line == '\0' || *line == '#') {
            continue;
        }
        
        // Parse section headers
        if (*line == '[') {
            char* end = strchr(line, ']');
            if (!end) {
                config_log_error(line_number, "Invalid section header");
                fclose(file);
                return CONFIG_ERROR_INVALID_SYNTAX;
            }
            *end = '\0';
            
            size_t section_len = strlen(line + 1);
            if (section_len >= sizeof(current_section)) {
                config_log_error(line_number, 
                    "Section name too long (max %zu chars)", sizeof(current_section)-1);
                fclose(file);
                return CONFIG_ERROR_INVALID_SYNTAX;
            }
            
            strncpy(current_section, line + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            continue;
        }
        
        // Parse key=value pairs
        char* equals = strchr(line, '=');
        if (!equals) {
            config_log_error(line_number, "Invalid key=value format: '%s'", line);
            fclose(file);
            return CONFIG_ERROR_INVALID_SYNTAX;
        }
        
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        trim_whitespace_inplace(key);
        trim_whitespace_inplace(value);
        
        // Parse parameter
        int result = parse_key_value(current_section, key, value, &g_sp3ctra_config, line_number);
        if (result != CONFIG_SUCCESS) {
            parse_errors++;
            // Continue parsing to report all errors
        }
    }
    
    fclose(file);
    
    if (parse_errors > 0) {
        config_log_error(0, "Configuration loading failed with %d error(s)", parse_errors);
        return CONFIG_ERROR_INVALID_VALUE;
    }
    
    // Calculate synthesis parameters from frequency range and DPI
    int calc_result = calculate_synthesis_params(&g_sp3ctra_config);
    if (calc_result != CONFIG_SUCCESS) {
        return calc_result;
    }
    
    // Validate the loaded configuration
    int validation_result = validate_config(&g_sp3ctra_config);
    if (validation_result != CONFIG_SUCCESS) {
        return validation_result;
    }
    
    config_log_info(0, "Configuration loaded successfully");
    return CONFIG_SUCCESS;
}
