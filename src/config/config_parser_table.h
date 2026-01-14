/* config_parser_table.h */

#ifndef CONFIG_PARSER_TABLE_H
#define CONFIG_PARSER_TABLE_H

#include "config_loader.h"
#include <stddef.h>

typedef enum {
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_STRING
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
    
    // Instrument section (hardware configuration)
    CONFIG_PARAM("instrument", "sensor_dpi", PARAM_TYPE_INT, 
                 sensor_dpi, 200, 400),
    
    // LuxStral synthesis section (frequency mapping)
    CONFIG_PARAM("synth_luxstral", "low_frequency", PARAM_TYPE_FLOAT, 
                 low_frequency, 20.0f, 20000.0f),
    CONFIG_PARAM("synth_luxstral", "high_frequency", PARAM_TYPE_FLOAT, 
                 high_frequency, 20.0f, 20000.0f),
    
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
    
    // Threading section
    CONFIG_PARAM("synth_luxstral", "num_workers", PARAM_TYPE_INT,
                 num_workers, 1, 8),
    
    // Summation normalization section
    CONFIG_PARAM("summation_normalization", "volume_weighting_exponent", PARAM_TYPE_FLOAT, 
                 volume_weighting_exponent, 0.01f, 10.0f),
    CONFIG_PARAM("summation_normalization", "summation_response_exponent", PARAM_TYPE_FLOAT, 
                 summation_response_exponent, 0.1f, 3.0f),
    CONFIG_PARAM("summation_normalization", "soft_limit_threshold", PARAM_TYPE_FLOAT, 
                 soft_limit_threshold, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "soft_limit_knee", PARAM_TYPE_FLOAT, 
                 soft_limit_knee, 0.0f, 1.0f),
    CONFIG_PARAM("summation_normalization", "noise_gate_threshold", PARAM_TYPE_FLOAT,
                 noise_gate_threshold, 0.0f, 0.1f),
    
    // Image processing - LUXSTRAL SYNTHESIS
    CONFIG_PARAM("image_processing_luxstral", "invert_intensity", PARAM_TYPE_BOOL, 
                 invert_intensity, 0, 1),
    CONFIG_PARAM("image_processing_luxstral", "enable_non_linear_mapping", PARAM_TYPE_BOOL, 
                 additive_enable_non_linear_mapping, 0, 1),
    CONFIG_PARAM("image_processing_luxstral", "gamma_value", PARAM_TYPE_FLOAT, 
                 additive_gamma_value, 0.1f, 10.0f),
    CONFIG_PARAM("image_processing_luxstral", "contrast_min", PARAM_TYPE_FLOAT, 
                 additive_contrast_min, 0.0f, 1.0f),
    CONFIG_PARAM("image_processing_luxstral", "contrast_stride", PARAM_TYPE_FLOAT, 
                 additive_contrast_stride, 1.0f, 10.0f),
    CONFIG_PARAM("image_processing_luxstral", "contrast_adjustment_power", PARAM_TYPE_FLOAT, 
                 additive_contrast_adjustment_power, 0.1f, 5.0f),
    
    // LuxWave synthesis section - all parameters now in synth_luxwave
    CONFIG_PARAM("synth_luxwave", "continuous_mode", PARAM_TYPE_BOOL,
                 photowave_continuous_mode, 0, 1),
    CONFIG_PARAM("synth_luxwave", "scan_mode", PARAM_TYPE_INT,
                 photowave_scan_mode, 0, 2),
    CONFIG_PARAM("synth_luxwave", "interp_mode", PARAM_TYPE_INT,
                 photowave_interp_mode, 0, 1),
    CONFIG_PARAM("synth_luxwave", "amplitude", PARAM_TYPE_FLOAT,
                 photowave_amplitude, 0.0f, 1.0f),
    
    // LuxWave ADSR Volume parameters
    CONFIG_PARAM("synth_luxwave", "volume_env_attack", PARAM_TYPE_FLOAT,
                 photowave_volume_adsr_attack_s, 0.001f, 5.0f),
    CONFIG_PARAM("synth_luxwave", "volume_env_decay", PARAM_TYPE_FLOAT,
                 photowave_volume_adsr_decay_s, 0.001f, 5.0f),
    CONFIG_PARAM("synth_luxwave", "volume_env_sustain", PARAM_TYPE_FLOAT,
                 photowave_volume_adsr_sustain_level, 0.0f, 1.0f),
    CONFIG_PARAM("synth_luxwave", "volume_env_release", PARAM_TYPE_FLOAT,
                 photowave_volume_adsr_release_s, 0.001f, 10.0f),
    
    // LuxWave ADSR Filter parameters
    CONFIG_PARAM("synth_luxwave", "filter_env_attack", PARAM_TYPE_FLOAT,
                 photowave_filter_adsr_attack_s, 0.001f, 5.0f),
    CONFIG_PARAM("synth_luxwave", "filter_env_decay", PARAM_TYPE_FLOAT,
                 photowave_filter_adsr_decay_s, 0.001f, 5.0f),
    CONFIG_PARAM("synth_luxwave", "filter_env_sustain", PARAM_TYPE_FLOAT,
                 photowave_filter_adsr_sustain_level, 0.0f, 1.0f),
    CONFIG_PARAM("synth_luxwave", "filter_env_release", PARAM_TYPE_FLOAT,
                 photowave_filter_adsr_release_s, 0.001f, 10.0f),
    
    // LuxWave LFO parameters
    CONFIG_PARAM("synth_luxwave", "lfo_vibrato_rate", PARAM_TYPE_FLOAT,
                 photowave_lfo_rate_hz, 0.0f, 20.0f),
    CONFIG_PARAM("synth_luxwave", "lfo_vibrato_depth", PARAM_TYPE_FLOAT,
                 photowave_lfo_depth_semitones, 0.0f, 2.0f),
    
    // LuxWave spectral filter parameters
    CONFIG_PARAM("synth_luxwave", "filter_cutoff", PARAM_TYPE_FLOAT,
                 photowave_filter_cutoff_hz, 100.0f, 20000.0f),
    CONFIG_PARAM("synth_luxwave", "filter_env_depth", PARAM_TYPE_FLOAT,
                 photowave_filter_env_depth_hz, -10000.0f, 10000.0f),
    
    // LuxSynth synthesis section - all parameters now in synth_luxsynth
    CONFIG_PARAM("synth_luxsynth", "num_voices", PARAM_TYPE_INT, 
                 poly_num_voices, 1, 32),
    CONFIG_PARAM("synth_luxsynth", "max_oscillators", PARAM_TYPE_INT, 
                 poly_max_oscillators, 1, 256),
    
    // LuxSynth ADSR Volume parameters
    CONFIG_PARAM("synth_luxsynth", "volume_env_attack", PARAM_TYPE_FLOAT,
                 poly_volume_adsr_attack_s, 0.0f, 10.0f),
    CONFIG_PARAM("synth_luxsynth", "volume_env_decay", PARAM_TYPE_FLOAT,
                 poly_volume_adsr_decay_s, 0.0f, 10.0f),
    CONFIG_PARAM("synth_luxsynth", "volume_env_sustain", PARAM_TYPE_FLOAT,
                 poly_volume_adsr_sustain_level, 0.0f, 1.0f),
    CONFIG_PARAM("synth_luxsynth", "volume_env_release", PARAM_TYPE_FLOAT,
                 poly_volume_adsr_release_s, 0.0f, 10.0f),
    
    // LuxSynth ADSR Filter parameters
    CONFIG_PARAM("synth_luxsynth", "filter_env_attack", PARAM_TYPE_FLOAT,
                 poly_filter_adsr_attack_s, 0.0f, 10.0f),
    CONFIG_PARAM("synth_luxsynth", "filter_env_decay", PARAM_TYPE_FLOAT,
                 poly_filter_adsr_decay_s, 0.0f, 10.0f),
    CONFIG_PARAM("synth_luxsynth", "filter_env_sustain", PARAM_TYPE_FLOAT,
                 poly_filter_adsr_sustain_level, 0.0f, 1.0f),
    CONFIG_PARAM("synth_luxsynth", "filter_env_release", PARAM_TYPE_FLOAT,
                 poly_filter_adsr_release_s, 0.0f, 10.0f),
    
    // LuxSynth LFO parameters
    CONFIG_PARAM("synth_luxsynth", "lfo_vibrato_rate", PARAM_TYPE_FLOAT,
                 poly_lfo_rate_hz, 0.0f, 30.0f),
    CONFIG_PARAM("synth_luxsynth", "lfo_vibrato_depth", PARAM_TYPE_FLOAT,
                 poly_lfo_depth_semitones, -12.0f, 12.0f),
    
    // LuxSynth spectral filter parameters
    CONFIG_PARAM("synth_luxsynth", "filter_cutoff", PARAM_TYPE_FLOAT,
                 poly_filter_cutoff_hz, 20.0f, 20000.0f),
    CONFIG_PARAM("synth_luxsynth", "filter_env_depth", PARAM_TYPE_FLOAT,
                 poly_filter_env_depth_hz, -20000.0f, 20000.0f),
    
    // LuxSynth performance parameters
    CONFIG_PARAM("synth_luxsynth", "master_volume", PARAM_TYPE_FLOAT,
                 poly_master_volume, 0.0f, 1.0f),
    CONFIG_PARAM("synth_luxsynth", "amplitude_gamma", PARAM_TYPE_FLOAT,
                 poly_amplitude_gamma, 0.1f, 5.0f),
    CONFIG_PARAM("synth_luxsynth", "min_audible_amplitude", PARAM_TYPE_FLOAT,
                 poly_min_audible_amplitude, 0.0f, 0.1f),
    CONFIG_PARAM("synth_luxsynth", "high_freq_harmonic_limit_hz", PARAM_TYPE_FLOAT,
                 poly_high_freq_harmonic_limit_hz, 1000.0f, 20000.0f),
    
    // LuxSynth advanced parameters
    CONFIG_PARAM("polyphonic", "amplitude_smoothing_alpha", PARAM_TYPE_FLOAT,
                 poly_amplitude_smoothing_alpha, 0.0f, 1.0f),
    CONFIG_PARAM("polyphonic", "norm_factor_bin0", PARAM_TYPE_FLOAT,
                 poly_norm_factor_bin0, 1.0f, 10000000.0f),
    CONFIG_PARAM("polyphonic", "norm_factor_harmonics", PARAM_TYPE_FLOAT,
                 poly_norm_factor_harmonics, 1.0f, 10000000.0f),
    
    // LuxSynth harmonicity parameters (color-based timbre control)
    CONFIG_PARAM("synth_luxsynth", "detune_max_cents", PARAM_TYPE_FLOAT,
                 poly_detune_max_cents, 0.0f, 50.0f),
    CONFIG_PARAM("synth_luxsynth", "harmonicity_curve_exponent", PARAM_TYPE_FLOAT,
                 poly_harmonicity_curve_exponent, 0.5f, 2.0f),
    
    // Network configuration
    CONFIG_PARAM("network", "udp_address", PARAM_TYPE_STRING,
                 udp_address, 0, 0),
    CONFIG_PARAM("network", "udp_port", PARAM_TYPE_INT,
                 udp_port, 1, 65535),
    CONFIG_PARAM("network", "multicast_interface", PARAM_TYPE_STRING,
                 multicast_interface, 0, 0),
    
    // DMX lighting parameters
    CONFIG_PARAM("dmx", "brightness", PARAM_TYPE_FLOAT,
                 dmx_brightness, 0.0f, 5.0f),
    CONFIG_PARAM("dmx", "gamma", PARAM_TYPE_FLOAT,
                 dmx_gamma, 0.5f, 2.5f),
    CONFIG_PARAM("dmx", "black_threshold", PARAM_TYPE_FLOAT,
                 dmx_black_threshold, 0.0f, 0.5f),
    CONFIG_PARAM("dmx", "response_curve", PARAM_TYPE_FLOAT,
                 dmx_response_curve, 1.0f, 5.0f),
    CONFIG_PARAM("dmx", "red_factor", PARAM_TYPE_FLOAT,
                 dmx_red_factor, 0.5f, 2.0f),
    CONFIG_PARAM("dmx", "green_factor", PARAM_TYPE_FLOAT,
                 dmx_green_factor, 0.5f, 2.0f),
    CONFIG_PARAM("dmx", "blue_factor", PARAM_TYPE_FLOAT,
                 dmx_blue_factor, 0.5f, 2.0f),
    CONFIG_PARAM("dmx", "saturation_factor", PARAM_TYPE_FLOAT,
                 dmx_saturation_factor, 1.0f, 5.0f),
    
    // Display system parameters
    CONFIG_PARAM("display", "orientation", PARAM_TYPE_FLOAT,
                 display_orientation, 0.0f, 1.0f),
    CONFIG_PARAM("display", "udp_scroll_speed", PARAM_TYPE_FLOAT,
                 display_udp_scroll_speed, -1.0f, 1.0f),
    CONFIG_PARAM("display", "accel_x_scroll_speed", PARAM_TYPE_FLOAT,
                 display_accel_x_scroll_speed, -1.0f, 1.0f),
    CONFIG_PARAM("display", "accel_y_offset", PARAM_TYPE_FLOAT,
                 display_accel_y_offset, -1.0f, 1.0f),
    CONFIG_PARAM("display", "initial_line_position", PARAM_TYPE_FLOAT,
                 display_initial_line_position, -1.0f, 1.0f),
    CONFIG_PARAM("display", "line_thickness", PARAM_TYPE_FLOAT,
                 display_line_thickness, 0.0f, 1.0f),
    CONFIG_PARAM("display", "transition_time_ms", PARAM_TYPE_FLOAT,
                 display_transition_time_ms, 0.0f, 1000.0f),
    CONFIG_PARAM("display", "accel_sensitivity", PARAM_TYPE_FLOAT,
                 display_accel_sensitivity, 0.1f, 5.0f),
    CONFIG_PARAM("display", "fade_strength", PARAM_TYPE_FLOAT,
                 display_fade_strength, 0.0f, 1.0f),
    CONFIG_PARAM("display", "line_persistence", PARAM_TYPE_FLOAT,
                 display_line_persistence, 0.0f, 10.0f),
    CONFIG_PARAM("display", "display_zoom", PARAM_TYPE_FLOAT,
                 display_zoom, -1.0f, 1.0f),
    CONFIG_PARAM("display", "history_buffer_size", PARAM_TYPE_INT,
                 display_history_buffer_size, 100, 10000),
    CONFIG_PARAM("display", "window_width", PARAM_TYPE_INT,
                 display_window_width, 1, 10000),
    CONFIG_PARAM("display", "window_height", PARAM_TYPE_INT,
                 display_window_height, 1, 10000),
    
    // IMU rotation parameters
    CONFIG_PARAM("display", "gyro_rotation_enabled", PARAM_TYPE_FLOAT,
                 display_gyro_rotation_enabled, 0.0f, 1.0f),
    CONFIG_PARAM("display", "gyro_rotation_sensitivity", PARAM_TYPE_FLOAT,
                 display_gyro_rotation_sensitivity, 0.1f, 5.0f),
    CONFIG_PARAM("display", "rotation_smoothing", PARAM_TYPE_FLOAT,
                 display_rotation_smoothing, 0.0f, 0.95f),
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
    {"synthesis", "start_frequency", "low_frequency (auto-calculated from low/high frequency and DPI)"},
    {"synthesis", "semitone_per_octave", "removed (always 12, auto-calculated)"},
    {"synthesis", "comma_per_semitone", "removed (auto-calculated from low/high frequency and DPI)"},
    {"synthesis", "pixels_per_note", "removed (always 1, auto-calculated)"},
    {"envelope_slew", "enable_phase_weighted_slew", "removed (precomputed coefficients)"},
    {"envelope_slew", "phase_weight_power", "removed (precomputed coefficients)"},
    {"auto_volume", "imu_active_threshold_x", "compile-time constant"},
    {"auto_volume", "imu_filter_alpha_x", "compile-time constant"},
    {"auto_volume", "auto_volume_active_level", "compile-time constant"},
    {"auto_volume", "auto_volume_poll_ms", "compile-time constant"},
    {"image_processing", "enable_non_linear_mapping", "moved to [image_processing_luxstral]"},
    {"image_processing", "gamma_value", "moved to [image_processing_luxstral]"},
    {"summation_normalization", "contrast_min", "moved to [image_processing_luxstral]"},
    {"summation_normalization", "contrast_stride", "moved to [image_processing_luxstral]"},
    {"summation_normalization", "contrast_adjustment_power", "moved to [image_processing_luxstral]"},
};

#define DEPRECATED_PARAMS_COUNT (sizeof(DEPRECATED_PARAMS) / sizeof(DEPRECATED_PARAMS[0]))

#endif // CONFIG_PARSER_TABLE_H
