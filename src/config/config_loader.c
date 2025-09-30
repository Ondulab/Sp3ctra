/* config_loader.c */

#include "config_loader.h"
#include "config_synth_additive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**************************************************************************************
 * Global Configuration Instance
 **************************************************************************************/
sp3ctra_config_t g_sp3ctra_config;

/**************************************************************************************
 * Default Values (from original #define values)
 **************************************************************************************/
static const sp3ctra_config_t DEFAULT_CONFIG = {
    // Audio system parameters (from config_audio.h)
    .sampling_frequency = 48000,         // Default: 48kHz
    .audio_buffer_size = 80,             // Default: 80 frames (legacy buffer length for 48 kHz)
    
    // Auto-volume parameters (runtime configurable)
    .auto_volume_enabled = 0,            // Default: auto-volume disabled
    .imu_inactivity_timeout_s = 5,
    .auto_volume_inactive_level = 0.01f,
    .auto_volume_fade_ms = 600,
    
    // Synthesis parameters
    .start_frequency = 65.41f,
    .semitone_per_octave = 12,
    .comma_per_semitone = 36,
    .pixels_per_note = 1,
    .invert_intensity = 1,

    // Envelope slew parameters (runtime configurable; defaults)
    .tau_up_base_ms = 0.5f,
    .tau_down_base_ms = 0.5f,
    .decay_freq_ref_hz = 440.0f,
    .decay_freq_beta = -1.2f,
    
    // Stereo processing parameters
    .stereo_mode_enabled = 0,                  // Default: stereo disabled in config
    .stereo_temperature_amplification = 2.5f,
    .stereo_blue_red_weight = 0.8f,
    .stereo_cyan_yellow_weight = 0.2f,
    .stereo_temperature_curve_exponent = 1.0f,
    
    // Summation normalization parameters
    .volume_weighting_exponent = 0.1f,         // Default: strong domination of strong oscillators
    .summation_response_exponent = 2.0f        // Default: compression (good sound quality)
};

/**************************************************************************************
 * Helper Functions
 **************************************************************************************/

/**
 * Trim whitespace from both ends of a string
 */
static char* trim_whitespace(char* str) {
    char* end;
    
    // Trim leading space
    while (*str == ' ' || *str == '\t') str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    
    // Write new null terminator
    *(end + 1) = 0;
    
    return str;
}

/**
 * Parse a float value with error checking
 */
static int parse_float(const char* value_str, float* result, const char* param_name) {
    char* endptr;
    errno = 0;
    *result = strtof(value_str, &endptr);
    
    if (errno != 0 || endptr == value_str || *endptr != '\0') {
        fprintf(stderr, "[CONFIG ERROR] Invalid float value '%s' for parameter '%s'\n", 
                value_str, param_name);
        return -1;
    }
    return 0;
}

/**
 * Parse an integer value with error checking
 */
static int parse_int(const char* value_str, int* result, const char* param_name) {
    char* endptr;
    errno = 0;
    *result = (int)strtol(value_str, &endptr, 10);
    
    if (errno != 0 || endptr == value_str || *endptr != '\0') {
        fprintf(stderr, "[CONFIG ERROR] Invalid integer value '%s' for parameter '%s'\n", 
                value_str, param_name);
        return -1;
    }
    return 0;
}

/**************************************************************************************
 * Configuration File Creation
 **************************************************************************************/

int create_default_config_file(const char* config_file_path) {
    FILE* file = fopen(config_file_path, "w");
    if (!file) {
        fprintf(stderr, "[CONFIG ERROR] Cannot create config file '%s': %s\n", 
                config_file_path, strerror(errno));
        return -1;
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
    fprintf(file, "# Base frequency for the first note (Hz)\n");
    fprintf(file, "start_frequency = %.2f\n", DEFAULT_CONFIG.start_frequency);
    fprintf(file, "\n");
    fprintf(file, "# Musical scale configuration\n");
    fprintf(file, "semitone_per_octave = %d\n", DEFAULT_CONFIG.semitone_per_octave);
    fprintf(file, "comma_per_semitone = %d\n", DEFAULT_CONFIG.comma_per_semitone);
    fprintf(file, "\n");
    fprintf(file, "# Pixel to note mapping and intensity behavior\n");
    fprintf(file, "pixels_per_note = %d\n", DEFAULT_CONFIG.pixels_per_note);
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
    
    fclose(file);
    
    printf("[CONFIG] Created default configuration file: %s\n", config_file_path);
    return 0;
}

/**************************************************************************************
 * Configuration Validation
 **************************************************************************************/

void validate_config(const sp3ctra_config_t* config) {
    int errors = 0;
    
    // Validate audio system parameters
    if (config->sampling_frequency != 22050 && config->sampling_frequency != 44100 && 
        config->sampling_frequency != 48000 && config->sampling_frequency != 96000) {
        fprintf(stderr, "[CONFIG ERROR] sampling_frequency must be 22050, 44100, 48000, or 96000, got %d\n",
                config->sampling_frequency);
        errors++;
    }
    
    if (config->audio_buffer_size < 16 || config->audio_buffer_size > 2048) {
        fprintf(stderr, "[CONFIG ERROR] audio_buffer_size must be between 16 and 2048, got %d\n",
                config->audio_buffer_size);
        errors++;
    }
    
    // Validate auto-volume parameters (runtime configurable)
    if (config->auto_volume_enabled != 0 && config->auto_volume_enabled != 1) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_enabled must be 0 or 1, got %d\n",
                config->auto_volume_enabled);
        errors++;
    }
    
    if (config->imu_inactivity_timeout_s < 1 || config->imu_inactivity_timeout_s > 3600) {
        fprintf(stderr, "[CONFIG ERROR] imu_inactivity_timeout_s must be between 1 and 3600, got %d\n", 
                config->imu_inactivity_timeout_s);
        errors++;
    }
    
    if (config->auto_volume_inactive_level < 0.0f || config->auto_volume_inactive_level > 1.0f) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_inactive_level must be between 0.0 and 1.0, got %.3f\n", 
                config->auto_volume_inactive_level);
        errors++;
    }
    
    if (config->auto_volume_fade_ms < 10 || config->auto_volume_fade_ms > 10000) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_fade_ms must be between 10 and 10000, got %d\n", 
                config->auto_volume_fade_ms);
        errors++;
    }
    
    // Validate synthesis parameters
    if (config->start_frequency < 20.0f || config->start_frequency > 20000.0f) {
        fprintf(stderr, "[CONFIG ERROR] start_frequency must be between 20.0 and 20000.0, got %.2f\n", 
                config->start_frequency);
        errors++;
    }
    
    if (config->semitone_per_octave < 1 || config->semitone_per_octave > 24) {
        fprintf(stderr, "[CONFIG ERROR] semitone_per_octave must be between 1 and 24, got %d\n", 
                config->semitone_per_octave);
        errors++;
    }
    
    if (config->comma_per_semitone < 1 || config->comma_per_semitone > 100) {
        fprintf(stderr, "[CONFIG ERROR] comma_per_semitone must be between 1 and 100, got %d\n", 
                config->comma_per_semitone);
        errors++;
    }
    
    // Validate pixels_per_note parameter
    if (config->pixels_per_note < 1 || config->pixels_per_note > 100) {
        fprintf(stderr, "[CONFIG ERROR] pixels_per_note must be between 1 and 100, got %d\n", 
                config->pixels_per_note);
        errors++;
    }
    
    // Ensure CIS_MAX_PIXELS_NB is divisible by pixels_per_note
    if ((CIS_MAX_PIXELS_NB % config->pixels_per_note) != 0) {
        fprintf(stderr, "[CONFIG ERROR] pixels_per_note (%d) must divide evenly into CIS_MAX_PIXELS_NB (%d)\n", 
                config->pixels_per_note, CIS_MAX_PIXELS_NB);
        errors++;
    }
    if (config->invert_intensity != 0 && config->invert_intensity != 1) {
        fprintf(stderr, "[CONFIG ERROR] invert_intensity must be 0 or 1, got %d\n",
                config->invert_intensity);
        errors++;
    }

    // Validate envelope slew parameters
    if (config->tau_up_base_ms <= 0.0f || config->tau_up_base_ms > TAU_UP_MAX_MS) {
        fprintf(stderr, "[CONFIG ERROR] tau_up_base_ms must be > 0 and <= %.1f, got %.3f\n",
                TAU_UP_MAX_MS, config->tau_up_base_ms);
        errors++;
    }
    if (config->tau_down_base_ms <= 0.0f || config->tau_down_base_ms > TAU_DOWN_MAX_MS) {
        fprintf(stderr, "[CONFIG ERROR] tau_down_base_ms must be > 0 and <= %.1f, got %.3f\n",
                TAU_DOWN_MAX_MS, config->tau_down_base_ms);
        errors++;
    }
    if (config->decay_freq_ref_hz < 20.0f || config->decay_freq_ref_hz > 20000.0f) {
        fprintf(stderr, "[CONFIG ERROR] decay_freq_ref_hz must be between 20.0 and 20000.0, got %.3f\n",
                config->decay_freq_ref_hz);
        errors++;
    }
    if (config->decay_freq_beta < -10.0f || config->decay_freq_beta > 10.0f) {
        fprintf(stderr, "[CONFIG ERROR] decay_freq_beta must be between -10.0 and 10.0, got %.2f\n",
                config->decay_freq_beta);
        errors++;
    }
    
    // Validate stereo processing parameters
    if (config->stereo_mode_enabled != 0 && config->stereo_mode_enabled != 1) {
        fprintf(stderr, "[CONFIG ERROR] stereo_mode_enabled must be 0 or 1, got %d\n",
                config->stereo_mode_enabled);
        errors++;
    }
    
    if (config->stereo_temperature_amplification < 0.1f || config->stereo_temperature_amplification > 10.0f) {
        fprintf(stderr, "[CONFIG ERROR] stereo_temperature_amplification must be between 0.1 and 10.0, got %.1f\n", 
                config->stereo_temperature_amplification);
        errors++;
    }
    
    if (config->stereo_blue_red_weight < 0.0f || config->stereo_blue_red_weight > 1.0f) {
        fprintf(stderr, "[CONFIG ERROR] stereo_blue_red_weight must be between 0.0 and 1.0, got %.1f\n", 
                config->stereo_blue_red_weight);
        errors++;
    }
    
    if (config->stereo_cyan_yellow_weight < 0.0f || config->stereo_cyan_yellow_weight > 1.0f) {
        fprintf(stderr, "[CONFIG ERROR] stereo_cyan_yellow_weight must be between 0.0 and 1.0, got %.1f\n", 
                config->stereo_cyan_yellow_weight);
        errors++;
    }
    
    if (config->stereo_temperature_curve_exponent < 0.1f || config->stereo_temperature_curve_exponent > 2.0f) {
        fprintf(stderr, "[CONFIG ERROR] stereo_temperature_curve_exponent must be between 0.1 and 2.0, got %.1f\n", 
                config->stereo_temperature_curve_exponent);
        errors++;
    }
    
    // Validate summation normalization parameters
    if (config->volume_weighting_exponent < 0.01f || config->volume_weighting_exponent > 10.0f) {
        fprintf(stderr, "[CONFIG ERROR] volume_weighting_exponent must be between 0.01 and 10.0, got %.1f\n",
                config->volume_weighting_exponent);
        errors++;
    }
    
    if (config->summation_response_exponent < 0.1f || config->summation_response_exponent > 3.0f) {
        fprintf(stderr, "[CONFIG ERROR] summation_response_exponent must be between 0.1 and 3.0, got %.1f\n", 
                config->summation_response_exponent);
        errors++;
    }
    
    if (errors > 0) {
        fprintf(stderr, "[CONFIG ERROR] Configuration validation failed with %d error(s). Exiting.\n", errors);
        exit(EXIT_FAILURE);
    }
    
    printf("[CONFIG] Configuration validation passed\n");
}

/**************************************************************************************
 * INI File Parser
 **************************************************************************************/

int load_additive_config(const char* config_file_path) {
    FILE* file = fopen(config_file_path, "r");
    
    // If file doesn't exist, create it with default values
    if (!file) {
        printf("[CONFIG] Configuration file '%s' not found, creating with default values\n", config_file_path);
        if (create_default_config_file(config_file_path) != 0) {
            return -1;
        }
        // Load the default values
        g_sp3ctra_config = DEFAULT_CONFIG;
        validate_config(&g_sp3ctra_config);
        return 0;
    }
    
    // Initialize with default values
    g_sp3ctra_config = DEFAULT_CONFIG;
    
    char line[256];
    char current_section[64] = "";
    int line_number = 0;
    
    printf("[CONFIG] Loading configuration from: %s\n", config_file_path);
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char* trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        
        // Parse section headers
        if (*trimmed == '[') {
            char* end = strchr(trimmed, ']');
            if (!end) {
                fprintf(stderr, "[CONFIG ERROR] Line %d: Invalid section header\n", line_number);
                fclose(file);
                exit(EXIT_FAILURE);
            }
            *end = '\0';
            strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            continue;
        }
        
        // Parse key=value pairs
        char* equals = strchr(trimmed, '=');
        if (!equals) {
            fprintf(stderr, "[CONFIG ERROR] Line %d: Invalid key=value format: '%s'\n", line_number, trimmed);
            fclose(file);
            exit(EXIT_FAILURE);
        }
        
        *equals = '\0';
        char* key = trim_whitespace(trimmed);
        char* value = trim_whitespace(equals + 1);
        
        // Parse parameters based on section and key
        if (strcmp(current_section, "audio") == 0) {
            if (strcmp(key, "sampling_frequency") == 0) {
                if (parse_int(value, &g_sp3ctra_config.sampling_frequency, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "audio_buffer_size") == 0) {
                if (parse_int(value, &g_sp3ctra_config.audio_buffer_size, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
        } else if (strcmp(current_section, "auto_volume") == 0) {
            if (strcmp(key, "auto_volume_enabled") == 0) {
                if (parse_int(value, &g_sp3ctra_config.auto_volume_enabled, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "imu_inactivity_timeout_s") == 0) {
                if (parse_int(value, &g_sp3ctra_config.imu_inactivity_timeout_s, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_inactive_level") == 0) {
                if (parse_float(value, &g_sp3ctra_config.auto_volume_inactive_level, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_fade_ms") == 0) {
                if (parse_int(value, &g_sp3ctra_config.auto_volume_fade_ms, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "imu_active_threshold_x") == 0 || 
                       strcmp(key, "imu_filter_alpha_x") == 0 ||
                       strcmp(key, "auto_volume_active_level") == 0 ||
                       strcmp(key, "auto_volume_poll_ms") == 0) {
                // Ignore deprecated parameters (now #define constants)
                fprintf(stderr, "[CONFIG INFO] Line %d: Parameter '%s' is now a compile-time constant, ignoring\n", 
                        line_number, key);
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
        } else if (strcmp(current_section, "synthesis") == 0) {
            if (strcmp(key, "start_frequency") == 0) {
                if (parse_float(value, &g_sp3ctra_config.start_frequency, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "semitone_per_octave") == 0) {
                if (parse_int(value, &g_sp3ctra_config.semitone_per_octave, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "comma_per_semitone") == 0) {
                if (parse_int(value, &g_sp3ctra_config.comma_per_semitone, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "volume_increment") == 0 || strcmp(key, "volume_decrement") == 0 || 
                       strcmp(key, "volume_ramp_up_divisor") == 0 || strcmp(key, "volume_ramp_down_divisor") == 0) {
                // Ignore deprecated parameters (now handled by tau_up_base_ms/tau_down_base_ms)
                fprintf(stderr, "[CONFIG INFO] Line %d: Parameter '%s' is deprecated (replaced by tau_up_base_ms/tau_down_base_ms), ignoring\n", 
                        line_number, key);
            } else if (strcmp(key, "pixels_per_note") == 0) {
                if (parse_int(value, &g_sp3ctra_config.pixels_per_note, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "invert_intensity") == 0) {
                if (parse_int(value, &g_sp3ctra_config.invert_intensity, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
        } else {
            fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                    line_number, key, current_section);
        }
    } else if (strcmp(current_section, "envelope_slew") == 0) {
            if (strcmp(key, "tau_up_base_ms") == 0) {
                if (parse_float(value, &g_sp3ctra_config.tau_up_base_ms, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "tau_down_base_ms") == 0) {
                if (parse_float(value, &g_sp3ctra_config.tau_down_base_ms, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "decay_freq_ref_hz") == 0) {
                if (parse_float(value, &g_sp3ctra_config.decay_freq_ref_hz, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "decay_freq_beta") == 0) {
                if (parse_float(value, &g_sp3ctra_config.decay_freq_beta, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "enable_phase_weighted_slew") == 0 || strcmp(key, "phase_weight_power") == 0) {
                // Ignore deprecated phase weighting parameters (replaced by precomputed coefficients)
                fprintf(stderr, "[CONFIG INFO] Line %d: Parameter '%s' is deprecated (phase weighting removed), ignoring\n", 
                        line_number, key);
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
    } else if (strcmp(current_section, "stereo_processing") == 0) {
            if (strcmp(key, "stereo_mode_enabled") == 0) {
                if (parse_int(value, &g_sp3ctra_config.stereo_mode_enabled, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "stereo_temperature_amplification") == 0) {
                if (parse_float(value, &g_sp3ctra_config.stereo_temperature_amplification, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "stereo_blue_red_weight") == 0) {
                if (parse_float(value, &g_sp3ctra_config.stereo_blue_red_weight, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "stereo_cyan_yellow_weight") == 0) {
                if (parse_float(value, &g_sp3ctra_config.stereo_cyan_yellow_weight, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "stereo_temperature_curve_exponent") == 0) {
                if (parse_float(value, &g_sp3ctra_config.stereo_temperature_curve_exponent, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
        } else if (strcmp(current_section, "summation_normalization") == 0) {
            if (strcmp(key, "volume_weighting_exponent") == 0) {
                if (parse_float(value, &g_sp3ctra_config.volume_weighting_exponent, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "summation_response_exponent") == 0) {
                if (parse_float(value, &g_sp3ctra_config.summation_response_exponent, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
        } else {
            fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown section '%s'\n", line_number, current_section);
        }
    }
    
    fclose(file);
    
    // Validate the loaded configuration
    validate_config(&g_sp3ctra_config);
    
    printf("[CONFIG] Configuration loaded successfully\n");
    return 0;
}
