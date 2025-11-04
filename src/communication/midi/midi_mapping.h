/*
 * midi_mapping.h
 *
 * Unified MIDI Mapping System
 * Provides configurable MIDI CC mapping with centralized callback dispatch
 *
 * Created: 30/10/2025
 * Author: Sp3ctra Team
 */

#ifndef MIDI_MAPPING_H
#define MIDI_MAPPING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define MIDI_MAX_PARAMETERS 128
#define MIDI_MAX_CALLBACKS 128
#define MIDI_MAX_PARAM_NAME 64

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================ */

/* MIDI message types */
typedef enum {
    MIDI_MSG_NONE = 0,
    MIDI_MSG_CC,           // Control Change
    MIDI_MSG_NOTE_ON,      // Note On
    MIDI_MSG_NOTE_OFF,     // Note Off
    MIDI_MSG_PITCHBEND,    // Pitch Bend
    MIDI_MSG_AFTERTOUCH    // Channel Aftertouch
} MidiMessageType;

/* Parameter scaling types */
typedef enum {
    MIDI_SCALE_LINEAR = 0,
    MIDI_SCALE_LOGARITHMIC,
    MIDI_SCALE_EXPONENTIAL,
    MIDI_SCALE_DISCRETE
} MidiScalingType;

/* ============================================================================
 * STRUCTURES
 * ============================================================================ */

/* MIDI control specification */
typedef struct {
    MidiMessageType type;
    int channel;           // MIDI channel (0-15, or -1 for any)
    int number;            // CC number, note number, etc.
} MidiControl;

/* Parameter range and default value */
typedef struct {
    float min_value;
    float max_value;
    float default_value;
    MidiScalingType scaling;
    int is_button;         // 1 if button/trigger, 0 if continuous
} MidiParameterSpec;

/* Parameter value with metadata */
typedef struct {
    float value;           // Normalized value [0.0, 1.0]
    float raw_value;       // Raw value in parameter's native unit
    const char *param_name;
    int is_button;         // 1 if button/trigger, 0 if continuous
} MidiParameterValue;

/* Callback function type */
typedef void (*MidiCallback)(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * PUBLIC API - INITIALIZATION
 * ============================================================================ */

/**
 * Initialize the MIDI mapping system
 * @return 0 on success, -1 on error
 */
int midi_mapping_init(void);

/**
 * Cleanup and free all resources
 */
void midi_mapping_cleanup(void);

/* ============================================================================
 * PUBLIC API - CONFIGURATION
 * ============================================================================ */

/**
 * Load user MIDI mappings from configuration file
 * @param config_file Path to midi_mapping.ini
 * @return 0 on success, -1 on error
 */
int midi_mapping_load_mappings(const char *config_file);

/**
 * Load parameter specifications from system configuration
 * @param params_file Path to midi_parameters_defaults.ini
 * @return 0 on success, -1 on error
 */
int midi_mapping_load_parameters(const char *params_file);

/* ============================================================================
 * PUBLIC API - CALLBACK REGISTRATION
 * ============================================================================ */

/**
 * Register callback for specific parameter
 * @param param_name Parameter identifier (e.g., "master_volume")
 * @param callback Function to call when parameter changes
 * @param user_data User data to pass to callback
 * @return 0 on success, -1 on error
 */
int midi_mapping_register_callback(
    const char *param_name,
    MidiCallback callback,
    void *user_data
);

/**
 * Unregister callback for specific parameter
 * @param param_name Parameter identifier
 */
void midi_mapping_unregister_callback(const char *param_name);

/* ============================================================================
 * PUBLIC API - MIDI MESSAGE DISPATCH
 * ============================================================================ */

/**
 * Dispatch incoming MIDI message to appropriate callbacks
 * This function is RT-safe when callbacks are RT-safe
 * @param type MIDI message type
 * @param channel MIDI channel (0-15)
 * @param number CC number, note number, etc.
 * @param value MIDI value (0-127)
 */
void midi_mapping_dispatch(
    MidiMessageType type,
    int channel,
    int number,
    int value
);

/* ============================================================================
 * PUBLIC API - PARAMETER QUERIES
 * ============================================================================ */

/**
 * Get current normalized value of parameter
 * @param param_name Parameter identifier
 * @return Normalized value [0.0, 1.0], or -1.0 if not found
 */
float midi_mapping_get_parameter_value(const char *param_name);

/**
 * Get current raw value of parameter (in native units)
 * @param param_name Parameter identifier
 * @return Raw value, or 0.0 if not found
 */
float midi_mapping_get_parameter_raw_value(const char *param_name);

/**
 * Set parameter value programmatically (triggers callbacks)
 * @param param_name Full parameter name
 * @param normalized_value Normalized value [0.0, 1.0]
 * @return 0 on success, -1 on error
 */
int midi_mapping_set_parameter_value(const char *param_name, float normalized_value);

/**
 * Apply default values to all parameters (triggers callbacks)
 * Should be called after loading parameters and registering callbacks
 * @return Number of parameters initialized, -1 on error
 */
int midi_mapping_apply_defaults(void);

/* ============================================================================
 * PUBLIC API - VALIDATION AND DIAGNOSTICS
 * ============================================================================ */

/**
 * Validate all mappings for conflicts
 * @return 0 if valid, number of conflicts otherwise
 */
int midi_mapping_validate(void);

/**
 * Check if there are any mapping conflicts
 * @return 1 if conflicts exist, 0 otherwise
 */
int midi_mapping_has_conflicts(void);

/**
 * Print current mapping status to stdout
 */
void midi_mapping_print_status(void);

/**
 * Print detailed mapping information for debugging
 */
void midi_mapping_print_debug_info(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_MAPPING_H */
