/* config_display_loader.c */

#include "config_display_loader.h"
#include "../core/display_globals.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper function to trim whitespace
static char* trim_whitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Helper function to parse float value
static int parse_float_value(const char* value_str, float* out_value) {
    char* endptr;
    *out_value = strtof(value_str, &endptr);
    return (endptr != value_str && (*endptr == '\0' || isspace(*endptr)));
}

// Helper function to parse int value
static int parse_int_value(const char* value_str, int* out_value) {
    char* endptr;
    *out_value = (int)strtol(value_str, &endptr, 10);
    return (endptr != value_str && (*endptr == '\0' || isspace(*endptr)));
}

int load_display_config(const char* config_file_path) {
    FILE* file = fopen(config_file_path, "r");
    if (!file) {
        log_error("CONFIG_DISPLAY", "Failed to open config file: %s", config_file_path);
        return CONFIG_ERROR_FILE_NOT_FOUND;
    }
    
    char line[256];
    int in_display_section = 0;
    int line_number = 0;
    int params_loaded = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char* trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        
        // Check for section header
        if (trimmed[0] == '[') {
            char* end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                char* section_name = trim_whitespace(trimmed + 1);
                in_display_section = (strcmp(section_name, "display") == 0);
            }
            continue;
        }
        
        // Parse key=value pairs in display section
        if (in_display_section) {
            char* equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                char* key = trim_whitespace(trimmed);
                char* value = trim_whitespace(equals + 1);
                
                // Parse display parameters
                if (strcmp(key, "orientation") == 0) {
                    parse_float_value(value, &g_display_config.orientation);
                    params_loaded++;
                } else if (strcmp(key, "udp_scroll_speed") == 0) {
                    parse_float_value(value, &g_display_config.udp_scroll_speed);
                    params_loaded++;
                } else if (strcmp(key, "initial_line_position") == 0) {
                    parse_float_value(value, &g_display_config.initial_line_position);
                    params_loaded++;
                } else if (strcmp(key, "line_thickness") == 0) {
                    parse_float_value(value, &g_display_config.line_thickness);
                    params_loaded++;
                } else if (strcmp(key, "window_width") == 0) {
                    parse_int_value(value, &g_display_config.window_width);
                    params_loaded++;
                } else if (strcmp(key, "window_height") == 0) {
                    parse_int_value(value, &g_display_config.window_height);
                    params_loaded++;
                }
            }
        }
    }
    
    fclose(file);
    
    log_info("CONFIG_DISPLAY", "Loaded %d display parameters from %s", 
             params_loaded, config_file_path);
    
    // Validate loaded configuration
    return validate_display_config();
}

int validate_display_config(void) {
    int valid = 1;
    
    // Validate orientation
    if (g_display_config.orientation < 0.0f || g_display_config.orientation > 1.0f) {
        log_error("CONFIG_DISPLAY", "Invalid orientation: %.2f (must be 0.0-1.0)", 
                 g_display_config.orientation);
        valid = 0;
    }
    
    // Validate scroll speed
    if (g_display_config.udp_scroll_speed < -1.0f || g_display_config.udp_scroll_speed > 1.0f) {
        log_error("CONFIG_DISPLAY", "Invalid udp_scroll_speed: %.2f (must be -1.0 to +1.0)", 
                 g_display_config.udp_scroll_speed);
        valid = 0;
    }
    
    // Validate window dimensions
    if (g_display_config.window_width <= 0 || g_display_config.window_height <= 0) {
        log_error("CONFIG_DISPLAY", "Invalid window dimensions: %dx%d", 
                 g_display_config.window_width, g_display_config.window_height);
        valid = 0;
    }
    
    if (valid) {
        log_info("CONFIG_DISPLAY", "Display configuration validated successfully");
        return CONFIG_SUCCESS;
    } else {
        log_error("CONFIG_DISPLAY", "Display configuration validation failed");
        return CONFIG_ERROR_VALIDATION_FAILED;
    }
}
