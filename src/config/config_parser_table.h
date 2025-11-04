/* config_parser_table.h */

#ifndef CONFIG_PARSER_TABLE_H
#define CONFIG_PARSER_TABLE_H

#include "config_loader.h"
#include <stddef.h>

typedef enum {
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_BOOL
} param_type_t;

typedef struct {
    const char* section;
    const char* key;
    param_type_t type;
    size_t offset;       // Offset in sp3ctra_config_t structure
    float min_value;     // For validation
    float max_value;     // For validation
} config_param_def_t;

// Macro to help with offsetof
#define CONFIG_PARAM(sect, k, t, field, min, max) \
    { sect, k, t, offsetof(sp3ctra_config_t, field), min, max }

// Table definition
static const config_param_def_t CONFIG_PARAMS[] = {
    // Audio section
    CONFIG_PARAM("audio", "sampling_frequency", PARAM_TYPE_INT, 
                 sampling_frequency, 22050, 96000),
    CONFIG_PARAM("audio", "audio_buffer_size", PARAM_TYPE_INT, 
                 audio_buffer_size, 16, 2048),
    
    // Auto-volume section
    CONFIG_PARAM("auto_volume", "auto_volume_enabled", PARAM_TYPE_BOOL, 
                 auto_volume_enabled, 0, 1),
    CONFIG_PARAM("auto_volume", "imu_inactivity_timeout_s", PARAM_TYPE_INT, 
                 imu_inactivity_timeout_s, 1, 3600),
    CONFIG_PARAM("auto_volume", "auto_volume_inactive_level", PARAM_TYPE_FLOAT, 
                 auto_volume_inactive_level, 0.0f, 1.0f),
    CONFIG_PARAM("auto_volume", "auto_volume_fade_ms", PARAM_TYPE_INT, 
                 auto_volume_fade_ms, 10, 10000),
    CONFIG_PARAM("auto_volume", "imu_sensitivity", PARAM_TYPE_FLOAT, 
                 imu_sensitivity, 0.1f, 10.0f),
    CONFIG_PARAM("auto_volume", "vibration_protection_factor", PARAM_TYPE_FLOAT, 
                 vibration_protection_factor, 1.0f, 5.0f),
    CONFIG_PARAM("auto_volume", "contrast_change_threshold", PARAM_TYPE_FLOAT, 
                 contrast_change_threshold, 0.01f, 0.5f),
    
    // Synthesis section
    CONFIG_PARAM("synthesis", "start_frequency", PARAM_TYPE_FLOAT, 
                 start_frequency, 20.0f, 20000.0f),
    CONFIG_PARAM("synthesis", "semitone_per_octave", PARAM_TYPE_INT, 
                 semitone_per_octave, 1, 24),
    CONFIG_PARAM("synthesis", "comma_per_semitone", PARAM_TYPE_INT, 
                 comma_per_semitone, 1, 100),
    CONFIG_PARAM("synthesis", "pixels_per_note", PARAM_TYPE_INT, 
                 pixels_per_note, 1, 100),
    CONFIG_PARAM("synthesis", "invert_intensity", PARAM_TYPE_BOOL, 
                 invert_intensity, 0, 1),
    
    // Envelope slew section
    CONFIG_PARAM("envelope_slew", "tau_up_base_ms", PARAM_TYPE_FLOAT, 
                 tau_up_base_ms, 0.001f, 1000.0f),
    CONFIG_PARAM("envelope_slew", "tau_down_base_ms", PARAM_TYPE_FLOAT, 
                 tau_down_base_ms, 0.001f, 1000.0f),
    CONFIG_PARAM("envelope_slew", "decay_freq_ref_hz", PARAM_TYPE_FLOAT, 
                 decay_freq_ref_hz, 20.0f, 20000.0f),
    CONFIG_PARAM("envelope_slew", "decay_freq_beta", PARAM_TYPE_FLOAT, 
                 decay_freq_beta, -10.0f, 10.0f),
    
    // Stereo processing section
    CONFIG_PARAM("stereo_processing", "stereo_mode_enabled", PARAM_TYPE_BOOL, 
                 stereo_mode_enabled, 0, 1),
    CONFIG_PARAM("stereo_processing", "stereo_temperature_amplification", PARAM_TYPE_FLOAT, 
                 stereo_temperature_amplification, 0.1f, 10.0f),
    CONFIG_PARAM("stereo_processing", "stereo_blue_red_weight", PARAM_TYPE_FLOAT, 
                 stereo_blue_red_weight, 0.0f, 1.0f),
    CONFIG_PARAM("stereo_processing", "stereo_cyan_yellow_weight", PARAM_TYPE_FLOAT, 
                 stereo_cyan_yellow_weight, 0.0f, 1.0f),
    CONFIG_PARAM("stereo_processing", "stereo_temperature_curve_exponent", PARAM_TYPE_FLOAT, 
                 stereo_temperature_curve_exponent, 0.1f, 2.0f),
    
    // Summation normalization section
    CONFIG_PARAM("summation_normalization", "volume_weighting_exponent", PARAM_TYPE_FLOAT, 
                 volume_weighting_exponent, 0.01f, 10.0f),
    CONFIG_PARAM("summation_normalization", "summation_response_exponent", PARAM_TYPE_FLOAT, 
                 summation_response_exponent, 0.1f, 3.0f),
    CONFIG_PARAM("summation_normalization", "noise_gate_threshold", PARAM_TYPE_FLOAT, 
                 noise_gate_threshold, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "soft_limit_threshold", PARAM_TYPE_FLOAT, 
                 soft_limit_threshold, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "soft_limit_knee", PARAM_TYPE_FLOAT, 
                 soft_limit_knee, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "contrast_min", PARAM_TYPE_FLOAT, 
                 contrast_min, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "contrast_stride", PARAM_TYPE_FLOAT, 
                 contrast_stride, 1.0f, 10.0f),
    CONFIG_PARAM("summation_normalization", "contrast_adjustment_power", PARAM_TYPE_FLOAT, 
                 contrast_adjustment_power, 0.1f, 5.0f),
    
    // Image processing section
    CONFIG_PARAM("image_processing", "enable_non_linear_mapping", PARAM_TYPE_BOOL, 
                 enable_non_linear_mapping, 0, 1),
    CONFIG_PARAM("image_processing", "gamma_value", PARAM_TYPE_FLOAT, 
                 gamma_value, 0.1f, 5.0f),
};

#define CONFIG_PARAMS_COUNT (sizeof(CONFIG_PARAMS) / sizeof(CONFIG_PARAMS[0]))

// Deprecated parameters list (for informational warnings)
typedef struct {
    const char* section;
    const char* key;
    const char* replacement;
} deprecated_param_t;

static const deprecated_param_t DEPRECATED_PARAMS[] = {
    {"synthesis", "volume_increment", "tau_up_base_ms"},
    {"synthesis", "volume_decrement", "tau_down_base_ms"},
    {"synthesis", "volume_ramp_up_divisor", "tau_up_base_ms"},
    {"synthesis", "volume_ramp_down_divisor", "tau_down_base_ms"},
    {"envelope_slew", "enable_phase_weighted_slew", "removed (precomputed coefficients)"},
    {"envelope_slew", "phase_weight_power", "removed (precomputed coefficients)"},
    {"auto_volume", "imu_active_threshold_x", "compile-time constant"},
    {"auto_volume", "imu_filter_alpha_x", "compile-time constant"},
    {"auto_volume", "auto_volume_active_level", "compile-time constant"},
    {"auto_volume", "auto_volume_poll_ms", "compile-time constant"},
};

#define DEPRECATED_PARAMS_COUNT (sizeof(DEPRECATED_PARAMS) / sizeof(DEPRECATED_PARAMS[0]))

#endif // CONFIG_PARSER_TABLE_H
