/*
 * midi_mapping.c
 *
 * Unified MIDI Mapping System Implementation
 *
 * Created: 30/10/2025
 * Author: Sp3ctra Team
 */

#include "midi_mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/* Parameter entry with mapping and specification */
typedef struct {
    char name[MIDI_MAX_PARAM_NAME];
    MidiControl control;
    MidiParameterSpec spec;
    float current_value;        // Normalized value [0.0, 1.0]
    float current_raw_value;    // Raw value in native units
    int is_mapped;              // 1 if has MIDI mapping, 0 otherwise
} ParameterEntry;

/* Callback registration entry */
typedef struct {
    char param_name[MIDI_MAX_PARAM_NAME];
    MidiCallback callback;
    void *user_data;
    int is_active;
} CallbackEntry;

/* Global mapping system state */
typedef struct {
    ParameterEntry parameters[MIDI_MAX_PARAMETERS];
    int num_parameters;
    
    CallbackEntry callbacks[MIDI_MAX_CALLBACKS];
    int num_callbacks;
    
    int is_initialized;
} MidiMappingSystem;

/* ============================================================================
 * STATIC DATA
 * ============================================================================ */

static MidiMappingSystem g_midi_system = {0};

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================ */

/**
 * Find parameter by name
 * @return Pointer to parameter or NULL if not found
 */
static ParameterEntry* find_parameter(const char *param_name) {
    if (!param_name) return NULL;
    
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        if (strcmp(g_midi_system.parameters[i].name, param_name) == 0) {
            return &g_midi_system.parameters[i];
        }
    }
    return NULL;
}

/**
 * Find parameter by MIDI control
 * @return Pointer to parameter or NULL if not found
 */
static ParameterEntry* find_parameter_by_control(MidiMessageType type, int channel, int number) {
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        ParameterEntry *param = &g_midi_system.parameters[i];
        
        if (!param->is_mapped) continue;
        
        if (param->control.type == type && param->control.number == number) {
            // Check channel (-1 means any channel)
            if (param->control.channel == -1 || param->control.channel == channel) {
                return param;
            }
        }
    }
    return NULL;
}

/**
 * Convert normalized value to raw value based on scaling
 */
static float normalized_to_raw(float normalized, const MidiParameterSpec *spec) {
    // Clamp normalized value
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    
    float raw;
    
    switch (spec->scaling) {
        case MIDI_SCALE_LINEAR:
            raw = spec->min_value + normalized * (spec->max_value - spec->min_value);
            break;
            
        case MIDI_SCALE_LOGARITHMIC: {
            // Logarithmic scaling: good for frequency
            float log_min = logf(spec->min_value);
            float log_max = logf(spec->max_value);
            raw = expf(log_min + normalized * (log_max - log_min));
            break;
        }
        
        case MIDI_SCALE_EXPONENTIAL: {
            // Exponential scaling: good for time-based parameters
            float exp_range = spec->max_value / spec->min_value;
            raw = spec->min_value * powf(exp_range, normalized);
            break;
        }
        
        case MIDI_SCALE_DISCRETE:
            // Discrete values (enums, modes)
            raw = spec->min_value + roundf(normalized * (spec->max_value - spec->min_value));
            break;
            
        default:
            raw = spec->min_value + normalized * (spec->max_value - spec->min_value);
            break;
    }
    
    return raw;
}

/**
 * Convert MIDI value (0-127) to normalized value (0.0-1.0)
 */
static float midi_to_normalized(int midi_value) {
    if (midi_value < 0) midi_value = 0;
    if (midi_value > 127) midi_value = 127;
    return (float)midi_value / 127.0f;
}

/**
 * Update parameter value and trigger callbacks
 */
static void update_parameter_value(ParameterEntry *param, float normalized_value) {
    if (!param) return;
    
    printf("\033[1;35m[DEBUG] update_parameter_value: '%s' normalized=%.3f\033[0m\n", 
           param->name, normalized_value);
    
    // Update values
    param->current_value = normalized_value;
    param->current_raw_value = normalized_to_raw(normalized_value, &param->spec);
    
    printf("\033[1;35m[DEBUG]   Raw value computed: %.3f\033[0m\n", param->current_raw_value);
    
    // Prepare callback data
    MidiParameterValue callback_data = {
        .value = param->current_value,
        .raw_value = param->current_raw_value,
        .param_name = param->name,
        .is_button = param->spec.is_button
    };
    
    // Count callbacks for this parameter
    int callback_count = 0;
    for (int i = 0; i < g_midi_system.num_callbacks; i++) {
        CallbackEntry *cb = &g_midi_system.callbacks[i];
        if (cb->is_active && strcmp(cb->param_name, param->name) == 0) {
            callback_count++;
        }
    }
    
    printf("\033[1;35m[DEBUG]   Found %d active callbacks for '%s'\033[0m\n", 
           callback_count, param->name);
    
    // Trigger all registered callbacks for this parameter
    for (int i = 0; i < g_midi_system.num_callbacks; i++) {
        CallbackEntry *cb = &g_midi_system.callbacks[i];
        
        if (cb->is_active && strcmp(cb->param_name, param->name) == 0) {
            if (cb->callback) {
                printf("\033[1;35m[DEBUG]   Triggering callback %d for '%s'\033[0m\n", i, param->name);
                cb->callback(&callback_data, cb->user_data);
            }
        }
    }
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - INITIALIZATION
 * ============================================================================ */

int midi_mapping_init(void) {
    if (g_midi_system.is_initialized) {
        return 0; // Already initialized
    }
    
    memset(&g_midi_system, 0, sizeof(MidiMappingSystem));
    g_midi_system.is_initialized = 1;
    
    printf("MIDI Mapping System: Initialized\n");
    return 0;
}

void midi_mapping_cleanup(void) {
    if (!g_midi_system.is_initialized) {
        return;
    }
    
    memset(&g_midi_system, 0, sizeof(MidiMappingSystem));
    printf("MIDI Mapping System: Cleaned up\n");
}

/* ============================================================================
 * INI PARSING HELPERS (adapted from config_loader.c)
 * ============================================================================ */

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
 * Parse scaling type from string
 */
static MidiScalingType parse_scaling_type(const char *str) {
    if (strcmp(str, "linear") == 0) return MIDI_SCALE_LINEAR;
    if (strcmp(str, "logarithmic") == 0 || strcmp(str, "log") == 0) return MIDI_SCALE_LOGARITHMIC;
    if (strcmp(str, "exponential") == 0 || strcmp(str, "exp") == 0) return MIDI_SCALE_EXPONENTIAL;
    if (strcmp(str, "discrete") == 0) return MIDI_SCALE_DISCRETE;
    return MIDI_SCALE_LINEAR; // Default
}

/**
 * Parse MIDI control specification from string (e.g., "CC:20" or "NOTE:*")
 */
static int parse_midi_control(const char *str, MidiControl *control) {
    if (strcmp(str, "none") == 0) {
        control->type = MIDI_MSG_NONE;
        return 0;
    }
    
    char type_str[16];
    int number;
    
    // Parse type:number format
    if (sscanf(str, "%15[^:]:%d", type_str, &number) == 2) {
        if (strcmp(type_str, "CC") == 0) {
            control->type = MIDI_MSG_CC;
            control->number = number;
            control->channel = -1; // Any channel
            return 0;
        } else if (strcmp(type_str, "NOTE") == 0) {
            control->type = MIDI_MSG_NOTE_ON;
            control->number = number;
            control->channel = -1;
            return 0;
        }
    }
    
    // Check for wildcard note
    if (strcmp(str, "NOTE:*") == 0) {
        control->type = MIDI_MSG_NOTE_ON;
        control->number = -1; // Any note
        control->channel = -1;
        return 0;
    }
    
    return -1;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - CONFIGURATION
 * ============================================================================ */

int midi_mapping_load_parameters(const char *params_file) {
    if (!g_midi_system.is_initialized) {
        fprintf(stderr, "ERROR: MIDI mapping system not initialized\n");
        return -1;
    }
    
    FILE *file = fopen(params_file, "r");
    if (!file) {
        fprintf(stderr, "WARNING: Cannot open MIDI parameters file '%s', using defaults\n", params_file);
        return -1;
    }
    
    char line[256];
    char current_param[MIDI_MAX_PARAM_NAME] = "";
    int line_number = 0;
    
    ParameterEntry *current_entry = NULL;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        
        // Parse section headers [CATEGORY.parameter_name]
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) {
                fprintf(stderr, "ERROR: Line %d: Invalid section header\n", line_number);
                fclose(file);
                return -1;
            }
            *end = '\0';
            
            // Extract full parameter name (convert CATEGORY.param to category_param)
            const char *section_name = trimmed + 1;
            char full_name[MIDI_MAX_PARAM_NAME];
            strncpy(full_name, section_name, MIDI_MAX_PARAM_NAME - 1);
            full_name[MIDI_MAX_PARAM_NAME - 1] = '\0';
            
            // Convert dots to underscores and lowercase
            for (int i = 0; full_name[i]; i++) {
                if (full_name[i] == '.') {
                    full_name[i] = '_';
                } else if (full_name[i] >= 'A' && full_name[i] <= 'Z') {
                    full_name[i] = full_name[i] - 'A' + 'a';
                }
            }
            
            strncpy(current_param, full_name, MIDI_MAX_PARAM_NAME - 1);
            current_param[MIDI_MAX_PARAM_NAME - 1] = '\0';
            
            // Find or create parameter entry
            current_entry = find_parameter(current_param);
            if (!current_entry) {
                if (g_midi_system.num_parameters >= MIDI_MAX_PARAMETERS) {
                    fprintf(stderr, "ERROR: Maximum number of parameters reached\n");
                    fclose(file);
                    return -1;
                }
                current_entry = &g_midi_system.parameters[g_midi_system.num_parameters++];
                strncpy(current_entry->name, current_param, MIDI_MAX_PARAM_NAME - 1);
                current_entry->name[MIDI_MAX_PARAM_NAME - 1] = '\0';
            }
            
            continue;
        }
        
        // Parse key=value pairs
        char *equals = strchr(trimmed, '=');
        if (!equals || !current_entry) {
            continue;
        }
        
        *equals = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(equals + 1);
        
        // Parse parameter specification fields
        if (strcmp(key, "default") == 0) {
            current_entry->spec.default_value = strtof(value, NULL);
            current_entry->current_value = current_entry->spec.default_value;
        } else if (strcmp(key, "min") == 0) {
            current_entry->spec.min_value = strtof(value, NULL);
        } else if (strcmp(key, "max") == 0) {
            current_entry->spec.max_value = strtof(value, NULL);
        } else if (strcmp(key, "scaling") == 0) {
            current_entry->spec.scaling = parse_scaling_type(value);
        } else if (strcmp(key, "type") == 0 && strcmp(value, "button") == 0) {
            current_entry->spec.is_button = 1;
        }
    }
    
    fclose(file);
    printf("MIDI Mapping System: Loaded %d parameter specifications from %s\n", 
           g_midi_system.num_parameters, params_file);
    return 0;
}

int midi_mapping_load_mappings(const char *config_file) {
    if (!g_midi_system.is_initialized) {
        fprintf(stderr, "ERROR: MIDI mapping system not initialized\n");
        return -1;
    }
    
    FILE *file = fopen(config_file, "r");
    if (!file) {
        fprintf(stderr, "WARNING: Cannot open MIDI mappings file '%s'\n", config_file);
        return -1;
    }
    
    char line[256];
    char current_section[64] = "";
    int line_number = 0;
    int mappings_loaded = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        
        // Parse section headers
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) {
                fprintf(stderr, "ERROR: Line %d: Invalid section header\n", line_number);
                fclose(file);
                return -1;
            }
            *end = '\0';
            strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            continue;
        }
        
        // Parse key=value pairs (param_name=CC:number)
        char *equals = strchr(trimmed, '=');
        if (!equals) {
            continue;
        }
        
        *equals = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(equals + 1);
        
        // Build full parameter name from section + key (section_key)
        char full_param_name[MIDI_MAX_PARAM_NAME];
        if (strlen(current_section) > 0) {
            snprintf(full_param_name, MIDI_MAX_PARAM_NAME, "%s_%s", current_section, key);
            // Convert to lowercase
            for (int i = 0; full_param_name[i]; i++) {
                if (full_param_name[i] >= 'A' && full_param_name[i] <= 'Z') {
                    full_param_name[i] = full_param_name[i] - 'A' + 'a';
                }
            }
        } else {
            strncpy(full_param_name, key, MIDI_MAX_PARAM_NAME - 1);
            full_param_name[MIDI_MAX_PARAM_NAME - 1] = '\0';
        }
        
        // Find parameter
        ParameterEntry *param = find_parameter(full_param_name);
        if (!param) {
            fprintf(stderr, "WARNING: Line %d: Unknown parameter '%s' (looked for '%s')\n", 
                    line_number, key, full_param_name);
            continue;
        }
        
        // Parse MIDI control
        MidiControl control;
        if (parse_midi_control(value, &control) == 0) {
            if (control.type != MIDI_MSG_NONE) {
                param->control = control;
                param->is_mapped = 1;
                mappings_loaded++;
            }
        } else {
            fprintf(stderr, "WARNING: Line %d: Invalid MIDI control format '%s'\n", line_number, value);
        }
    }
    
    fclose(file);
    printf("MIDI Mapping System: Loaded %d MIDI mappings from %s\n", mappings_loaded, config_file);
    return 0;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - CALLBACK REGISTRATION
 * ============================================================================ */

int midi_mapping_register_callback(const char *param_name, 
                                   MidiCallback callback, 
                                   void *user_data) {
    if (!g_midi_system.is_initialized) {
        fprintf(stderr, "ERROR: MIDI mapping system not initialized\n");
        return -1;
    }
    
    if (!param_name || !callback) {
        fprintf(stderr, "ERROR: Invalid parameters for callback registration\n");
        return -1;
    }
    
    if (g_midi_system.num_callbacks >= MIDI_MAX_CALLBACKS) {
        fprintf(stderr, "ERROR: Maximum number of callbacks reached\n");
        return -1;
    }
    
    // Check if parameter exists (optional, depends on initialization order)
    ParameterEntry *param = find_parameter(param_name);
    if (!param) {
        fprintf(stderr, "WARNING: Registering callback for unknown parameter: %s\n", param_name);
    }
    
    // Add callback entry
    CallbackEntry *cb = &g_midi_system.callbacks[g_midi_system.num_callbacks];
    strncpy(cb->param_name, param_name, MIDI_MAX_PARAM_NAME - 1);
    cb->param_name[MIDI_MAX_PARAM_NAME - 1] = '\0';
    cb->callback = callback;
    cb->user_data = user_data;
    cb->is_active = 1;
    
    g_midi_system.num_callbacks++;
    
    return 0;
}

void midi_mapping_unregister_callback(const char *param_name) {
    if (!g_midi_system.is_initialized || !param_name) {
        return;
    }
    
    for (int i = 0; i < g_midi_system.num_callbacks; i++) {
        if (strcmp(g_midi_system.callbacks[i].param_name, param_name) == 0) {
            g_midi_system.callbacks[i].is_active = 0;
        }
    }
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - MIDI MESSAGE DISPATCH
 * ============================================================================ */

void midi_mapping_dispatch(MidiMessageType type, int channel, int number, int value) {
    if (!g_midi_system.is_initialized) {
        return;
    }
    
    // Find parameter mapped to this MIDI control
    ParameterEntry *param = find_parameter_by_control(type, channel, number);
    
    if (!param) {
        // No mapping for this control
        return;
    }
    
    // Convert MIDI value to normalized value
    float normalized = midi_to_normalized(value);
    
    // Handle button triggers differently
    if (param->spec.is_button) {
        // For buttons, trigger on value > threshold (typically 64)
        if (value > 64) {
            normalized = 1.0f;
        } else {
            return; // Don't trigger callback on button release
        }
    }
    
    // Update parameter and trigger callbacks
    update_parameter_value(param, normalized);
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - PARAMETER QUERIES
 * ============================================================================ */

float midi_mapping_get_parameter_value(const char *param_name) {
    if (!g_midi_system.is_initialized || !param_name) {
        return -1.0f;
    }
    
    ParameterEntry *param = find_parameter(param_name);
    if (!param) {
        return -1.0f;
    }
    
    return param->current_value;
}

float midi_mapping_get_parameter_raw_value(const char *param_name) {
    if (!g_midi_system.is_initialized || !param_name) {
        return 0.0f;
    }
    
    ParameterEntry *param = find_parameter(param_name);
    if (!param) {
        return 0.0f;
    }
    
    return param->current_raw_value;
}

int midi_mapping_set_parameter_value(const char *param_name, float normalized_value) {
    if (!g_midi_system.is_initialized || !param_name) {
        return -1;
    }
    
    ParameterEntry *param = find_parameter(param_name);
    if (!param) {
        return -1;
    }
    
    update_parameter_value(param, normalized_value);
    return 0;
}

int midi_mapping_apply_defaults(void) {
    if (!g_midi_system.is_initialized) {
        fprintf(stderr, "ERROR: MIDI mapping system not initialized\n");
        return -1;
    }
    
    printf("\033[1;36m[DEBUG] midi_mapping_apply_defaults: Starting...\033[0m\n");
    printf("\033[1;36m[DEBUG] Total parameters: %d\033[0m\n", g_midi_system.num_parameters);
    
    int count = 0;
    
    // Apply default values to all parameters
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        ParameterEntry *param = &g_midi_system.parameters[i];
        
        // 🔧 BUGFIX: Skip buttons - they should only be triggered by actual MIDI events
        // Buttons are momentary triggers, not persistent states
        if (param->spec.is_button) {
            printf("\033[1;36m[DEBUG] Skipping button parameter '%s' (buttons not initialized)\033[0m\n", param->name);
            continue;
        }
        
        printf("\033[1;36m[DEBUG] Processing parameter '%s'\033[0m\n", param->name);
        printf("\033[1;36m[DEBUG]   Default: %.3f, Min: %.3f, Max: %.3f\033[0m\n", 
               param->spec.default_value, param->spec.min_value, param->spec.max_value);
        
        // Calculate normalized default value from raw default
        float normalized_default;
        
        if (param->spec.scaling == MIDI_SCALE_DISCRETE || param->spec.is_button) {
            // For discrete/button parameters, use default directly
            float range = param->spec.max_value - param->spec.min_value;
            if (range > 0.0f) {
                normalized_default = (param->spec.default_value - param->spec.min_value) / range;
            } else {
                normalized_default = 0.0f;
            }
            printf("\033[1;36m[DEBUG]   Discrete/Button: normalized = %.3f\033[0m\n", normalized_default);
        } else {
            // For continuous parameters, reverse the scaling to get normalized value
            float default_val = param->spec.default_value;
            float min_val = param->spec.min_value;
            float max_val = param->spec.max_value;
            
            switch (param->spec.scaling) {
                case MIDI_SCALE_LINEAR:
                    normalized_default = (default_val - min_val) / (max_val - min_val);
                    printf("\033[1;36m[DEBUG]   Linear scaling: normalized = %.3f\033[0m\n", normalized_default);
                    break;
                    
                case MIDI_SCALE_LOGARITHMIC:
                    if (min_val > 0.0f && max_val > 0.0f && default_val > 0.0f) {
                        float log_min = logf(min_val);
                        float log_max = logf(max_val);
                        float log_val = logf(default_val);
                        normalized_default = (log_val - log_min) / (log_max - log_min);
                        printf("\033[1;36m[DEBUG]   Logarithmic scaling: normalized = %.3f\033[0m\n", normalized_default);
                    } else {
                        normalized_default = 0.5f;
                        printf("\033[1;36m[DEBUG]   Logarithmic scaling FALLBACK: normalized = %.3f\033[0m\n", normalized_default);
                    }
                    break;
                    
                case MIDI_SCALE_EXPONENTIAL:
                    if (min_val > 0.0f && max_val > min_val && default_val > 0.0f) {
                        float exp_range = max_val / min_val;
                        normalized_default = logf(default_val / min_val) / logf(exp_range);
                        printf("\033[1;36m[DEBUG]   Exponential scaling: normalized = %.3f\033[0m\n", normalized_default);
                    } else {
                        normalized_default = 0.5f;
                        printf("\033[1;36m[DEBUG]   Exponential scaling FALLBACK: normalized = %.3f\033[0m\n", normalized_default);
                    }
                    break;
                    
                default:
                    normalized_default = (default_val - min_val) / (max_val - min_val);
                    printf("\033[1;36m[DEBUG]   Default scaling: normalized = %.3f\033[0m\n", normalized_default);
                    break;
            }
        }
        
        // Clamp to valid range
        if (normalized_default < 0.0f) normalized_default = 0.0f;
        if (normalized_default > 1.0f) normalized_default = 1.0f;
        
        printf("\033[1;36m[DEBUG]   Calling update_parameter_value with normalized = %.3f\033[0m\n", normalized_default);
        
        // Update parameter (this will trigger callbacks)
        update_parameter_value(param, normalized_default);
        count++;
    }
    
    printf("\033[1;36m[DEBUG] midi_mapping_apply_defaults: Completed, applied %d defaults\033[0m\n", count);
    printf("MIDI Mapping System: Applied default values to %d parameters\n", count);
    return count;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - VALIDATION AND DIAGNOSTICS
 * ============================================================================ */

int midi_mapping_validate(void) {
    if (!g_midi_system.is_initialized) {
        return -1;
    }
    
    int conflicts = 0;
    
    // Check for duplicate MIDI mappings
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        if (!g_midi_system.parameters[i].is_mapped) continue;
        
        MidiControl *ctrl1 = &g_midi_system.parameters[i].control;
        
        for (int j = i + 1; j < g_midi_system.num_parameters; j++) {
            if (!g_midi_system.parameters[j].is_mapped) continue;
            
            MidiControl *ctrl2 = &g_midi_system.parameters[j].control;
            
            // Check if same MIDI control
            if (ctrl1->type == ctrl2->type && ctrl1->number == ctrl2->number) {
                // Check channel conflict
                if (ctrl1->channel == ctrl2->channel || 
                    ctrl1->channel == -1 || ctrl2->channel == -1) {
                    fprintf(stderr, "WARNING: MIDI conflict: %s and %s both use same control\n",
                           g_midi_system.parameters[i].name,
                           g_midi_system.parameters[j].name);
                    conflicts++;
                }
            }
        }
    }
    
    return conflicts;
}

int midi_mapping_has_conflicts(void) {
    return midi_mapping_validate() > 0 ? 1 : 0;
}

void midi_mapping_print_status(void) {
    if (!g_midi_system.is_initialized) {
        printf("MIDI Mapping System: Not initialized\n");
        return;
    }
    
    printf("\n=== MIDI Mapping System Status ===\n");
    printf("Parameters: %d / %d\n", g_midi_system.num_parameters, MIDI_MAX_PARAMETERS);
    printf("Callbacks: %d / %d\n", g_midi_system.num_callbacks, MIDI_MAX_CALLBACKS);
    printf("Mapped parameters: ");
    
    int mapped_count = 0;
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        if (g_midi_system.parameters[i].is_mapped) {
            mapped_count++;
        }
    }
    printf("%d\n", mapped_count);
    
    int conflicts = midi_mapping_validate();
    if (conflicts > 0) {
        printf("WARNING: %d mapping conflict(s) detected!\n", conflicts);
    } else {
        printf("No conflicts detected\n");
    }
    
    printf("===================================\n\n");
}

void midi_mapping_print_debug_info(void) {
    if (!g_midi_system.is_initialized) {
        printf("MIDI Mapping System: Not initialized\n");
        return;
    }
    
    printf("\n=== MIDI Mapping Debug Information ===\n\n");
    
    printf("--- Parameters ---\n");
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        ParameterEntry *p = &g_midi_system.parameters[i];
        printf("%s:\n", p->name);
        printf("  Mapped: %s\n", p->is_mapped ? "yes" : "no");
        if (p->is_mapped) {
            const char *type_str = "UNKNOWN";
            switch (p->control.type) {
                case MIDI_MSG_CC: type_str = "CC"; break;
                case MIDI_MSG_NOTE_ON: type_str = "NOTE_ON"; break;
                case MIDI_MSG_NOTE_OFF: type_str = "NOTE_OFF"; break;
                default: break;
            }
            printf("  Control: %s %d (channel %d)\n", type_str, p->control.number, p->control.channel);
        }
        printf("  Current: %.3f (raw: %.3f)\n", p->current_value, p->current_raw_value);
        printf("  Range: [%.3f, %.3f], default: %.3f\n", 
               p->spec.min_value, p->spec.max_value, p->spec.default_value);
        printf("\n");
    }
    
    printf("--- Callbacks ---\n");
    for (int i = 0; i < g_midi_system.num_callbacks; i++) {
        CallbackEntry *cb = &g_midi_system.callbacks[i];
        printf("%s: %s\n", cb->param_name, cb->is_active ? "active" : "inactive");
    }
    
    printf("\n======================================\n\n");
}
