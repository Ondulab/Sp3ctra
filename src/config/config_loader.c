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
additive_synth_config_t g_additive_config;

/**************************************************************************************
 * Default Values (from original #define values)
 **************************************************************************************/
static const additive_synth_config_t DEFAULT_CONFIG = {
    // Auto-volume parameters
    .imu_active_threshold_x = 0.01f,
    .imu_filter_alpha_x = 0.25f,
    .imu_inactivity_timeout_s = 5,
    .auto_volume_inactive_level = 0.01f,
    .auto_volume_active_level = 1.0f,
    .auto_volume_fade_ms = 600,
    .auto_volume_poll_ms = 10,
    
    // Synthesis parameters
    .start_frequency = 65.41f,
    .semitone_per_octave = 12,
    .comma_per_semitone = 36,
    .volume_increment = 1,
    .volume_decrement = 1,
    .pixels_per_note = 1
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
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    
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
    
    fprintf(file, "# Sp3ctra Additive Synthesis Configuration\n");
    fprintf(file, "# This file was automatically generated with default values\n");
    fprintf(file, "# Modify these values as needed - the program will validate them on startup\n\n");
    
    fprintf(file, "[auto_volume]\n");
    fprintf(file, "imu_active_threshold_x = %.3f\n", DEFAULT_CONFIG.imu_active_threshold_x);
    fprintf(file, "imu_filter_alpha_x = %.3f\n", DEFAULT_CONFIG.imu_filter_alpha_x);
    fprintf(file, "imu_inactivity_timeout_s = %d\n", DEFAULT_CONFIG.imu_inactivity_timeout_s);
    fprintf(file, "auto_volume_inactive_level = %.3f\n", DEFAULT_CONFIG.auto_volume_inactive_level);
    fprintf(file, "auto_volume_active_level = %.3f\n", DEFAULT_CONFIG.auto_volume_active_level);
    fprintf(file, "auto_volume_fade_ms = %d\n", DEFAULT_CONFIG.auto_volume_fade_ms);
    fprintf(file, "auto_volume_poll_ms = %d\n", DEFAULT_CONFIG.auto_volume_poll_ms);
    fprintf(file, "\n");
    
    fprintf(file, "[synthesis]\n");
    fprintf(file, "start_frequency = %.2f\n", DEFAULT_CONFIG.start_frequency);
    fprintf(file, "semitone_per_octave = %d\n", DEFAULT_CONFIG.semitone_per_octave);
    fprintf(file, "comma_per_semitone = %d\n", DEFAULT_CONFIG.comma_per_semitone);
    fprintf(file, "volume_increment = %d\n", DEFAULT_CONFIG.volume_increment);
    fprintf(file, "volume_decrement = %d\n", DEFAULT_CONFIG.volume_decrement);
    fprintf(file, "pixels_per_note = %d\n", DEFAULT_CONFIG.pixels_per_note);
    
    fclose(file);
    
    printf("[CONFIG] Created default configuration file: %s\n", config_file_path);
    return 0;
}

/**************************************************************************************
 * Configuration Validation
 **************************************************************************************/

void validate_config(const additive_synth_config_t* config) {
    int errors = 0;
    
    // Validate auto-volume parameters
    if (config->imu_active_threshold_x < 0.0f || config->imu_active_threshold_x > 10.0f) {
        fprintf(stderr, "[CONFIG ERROR] imu_active_threshold_x must be between 0.0 and 10.0, got %.3f\n", 
                config->imu_active_threshold_x);
        errors++;
    }
    
    if (config->imu_filter_alpha_x < 0.0f || config->imu_filter_alpha_x > 1.0f) {
        fprintf(stderr, "[CONFIG ERROR] imu_filter_alpha_x must be between 0.0 and 1.0, got %.3f\n", 
                config->imu_filter_alpha_x);
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
    
    if (config->auto_volume_active_level < 0.0f || config->auto_volume_active_level > 1.0f) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_active_level must be between 0.0 and 1.0, got %.3f\n", 
                config->auto_volume_active_level);
        errors++;
    }
    
    if (config->auto_volume_fade_ms < 10 || config->auto_volume_fade_ms > 10000) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_fade_ms must be between 10 and 10000, got %d\n", 
                config->auto_volume_fade_ms);
        errors++;
    }
    
    if (config->auto_volume_poll_ms < 1 || config->auto_volume_poll_ms > 1000) {
        fprintf(stderr, "[CONFIG ERROR] auto_volume_poll_ms must be between 1 and 1000, got %d\n", 
                config->auto_volume_poll_ms);
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
    
    if (config->volume_increment < 1 || config->volume_increment > 100) {
        fprintf(stderr, "[CONFIG ERROR] volume_increment must be between 1 and 100, got %d\n", 
                config->volume_increment);
        errors++;
    }
    
    if (config->volume_decrement < 1 || config->volume_decrement > 100) {
        fprintf(stderr, "[CONFIG ERROR] volume_decrement must be between 1 and 100, got %d\n", 
                config->volume_decrement);
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
        g_additive_config = DEFAULT_CONFIG;
        validate_config(&g_additive_config);
        return 0;
    }
    
    // Initialize with default values
    g_additive_config = DEFAULT_CONFIG;
    
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
            fprintf(stderr, "[CONFIG ERROR] Line %d: Invalid key=value format\n", line_number);
            fclose(file);
            exit(EXIT_FAILURE);
        }
        
        *equals = '\0';
        char* key = trim_whitespace(trimmed);
        char* value = trim_whitespace(equals + 1);
        
        // Parse parameters based on section and key
        if (strcmp(current_section, "auto_volume") == 0) {
            if (strcmp(key, "imu_active_threshold_x") == 0) {
                if (parse_float(value, &g_additive_config.imu_active_threshold_x, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "imu_filter_alpha_x") == 0) {
                if (parse_float(value, &g_additive_config.imu_filter_alpha_x, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "imu_inactivity_timeout_s") == 0) {
                if (parse_int(value, &g_additive_config.imu_inactivity_timeout_s, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_inactive_level") == 0) {
                if (parse_float(value, &g_additive_config.auto_volume_inactive_level, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_active_level") == 0) {
                if (parse_float(value, &g_additive_config.auto_volume_active_level, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_fade_ms") == 0) {
                if (parse_int(value, &g_additive_config.auto_volume_fade_ms, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "auto_volume_poll_ms") == 0) {
                if (parse_int(value, &g_additive_config.auto_volume_poll_ms, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else {
                fprintf(stderr, "[CONFIG WARNING] Line %d: Unknown parameter '%s' in section '%s'\n", 
                        line_number, key, current_section);
            }
        } else if (strcmp(current_section, "synthesis") == 0) {
            if (strcmp(key, "start_frequency") == 0) {
                if (parse_float(value, &g_additive_config.start_frequency, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "semitone_per_octave") == 0) {
                if (parse_int(value, &g_additive_config.semitone_per_octave, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "comma_per_semitone") == 0) {
                if (parse_int(value, &g_additive_config.comma_per_semitone, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "volume_increment") == 0) {
                if (parse_int(value, &g_additive_config.volume_increment, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "volume_decrement") == 0) {
                if (parse_int(value, &g_additive_config.volume_decrement, key) != 0) {
                    fclose(file);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(key, "pixels_per_note") == 0) {
                if (parse_int(value, &g_additive_config.pixels_per_note, key) != 0) {
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
    validate_config(&g_additive_config);
    
    printf("[CONFIG] Configuration loaded successfully\n");
    return 0;
}
