/*
 * midi_mapping.c
 *
 * Unified MIDI Mapping System Implementation
 *
 * Created: 30/10/2025
 * Author: Sp3ctra Team
 */

#include "midi_mapping.h"
#include "../../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

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

/* Global MIDI device configuration */
static char g_midi_device_name[256] = "auto";
static int g_midi_device_id = -1; // -1 means auto

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
    
    log_debug("MIDI_MAP", "update_parameter_value: '%s' normalized=%.3f", 
              param->name, normalized_value);
    
    // Update values
    param->current_value = normalized_value;
    param->current_raw_value = normalized_to_raw(normalized_value, &param->spec);
    
    log_debug("MIDI_MAP", "  Raw value computed: %.3f", param->current_raw_value);
    
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
    
    log_debug("MIDI_MAP", "  Found %d active callbacks for '%s'", 
              callback_count, param->name);
    
    // Trigger all registered callbacks for this parameter
    for (int i = 0; i < g_midi_system.num_callbacks; i++) {
        CallbackEntry *cb = &g_midi_system.callbacks[i];
        
        if (cb->is_active && strcmp(cb->param_name, param->name) == 0) {
            if (cb->callback) {
                log_debug("MIDI_MAP", "  Triggering callback %d for '%s'", i, param->name);
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
    
    log_info("MIDI_MAP", "MIDI Mapping System initialized");
    return 0;
}

void midi_mapping_cleanup(void) {
    if (!g_midi_system.is_initialized) {
        return;
    }
    
    memset(&g_midi_system, 0, sizeof(MidiMappingSystem));
    log_info("MIDI_MAP", "MIDI Mapping System cleaned up");
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
 * Remove inline comments from a string (everything after '#')
 */
static void remove_inline_comment(char* str) {
    char* comment = strchr(str, '#');
    if (comment) {
        *comment = '\0';
    }
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
 * Parse MIDI control specification from string
 * Formats supported:
 *   - "CC:20" or "NOTE:60" - Any channel (backward compatible)
 *   - "CC:20:1" or "NOTE:60:2" - Specific channel (0-15)
 *   - "NOTE:*" or "NOTE:*:1" - Wildcard note (any/specific channel)
 */
static int parse_midi_control(const char *str, MidiControl *control) {
    if (strcmp(str, "none") == 0) {
        control->type = MIDI_MSG_NONE;
        return 0;
    }
    
    char type_str[16];
    int number, channel;
    
    // Parse type:number:channel format (e.g., "CC:20:1")
    if (sscanf(str, "%15[^:]:%d:%d", type_str, &number, &channel) == 3) {
        // Validate channel range (0-15 in MIDI spec)
        if (channel < 0 || channel > 15) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Invalid MIDI channel %d (must be 0-15), using any channel", channel);
            log_warning("MIDI_MAP", msg);
            channel = -1;
        }
        
        if (strcmp(type_str, "CC") == 0) {
            // Validate CC number range (0-127)
            if (number < 0 || number > 127) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid CC number %d (must be 0-127)", number);
                log_error("MIDI_MAP", msg);
                return -1;
            }
            control->type = MIDI_MSG_CC;
            control->number = number;
            control->channel = channel;
            return 0;
        } else if (strcmp(type_str, "NOTE") == 0) {
            // Validate note number range (0-127)
            if (number < 0 || number > 127) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid note number %d (must be 0-127)", number);
                log_error("MIDI_MAP", msg);
                return -1;
            }
            control->type = MIDI_MSG_NOTE_ON;
            control->number = number;
            control->channel = channel;
            return 0;
        }
    }
    
    // Parse type:number format (backward compatible - any channel)
    if (sscanf(str, "%15[^:]:%d", type_str, &number) == 2) {
        if (strcmp(type_str, "CC") == 0) {
            // Validate CC number range (0-127)
            if (number < 0 || number > 127) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid CC number %d (must be 0-127)", number);
                log_error("MIDI_MAP", msg);
                return -1;
            }
            control->type = MIDI_MSG_CC;
            control->number = number;
            control->channel = -1; // Any channel
            return 0;
        } else if (strcmp(type_str, "NOTE") == 0) {
            // Validate note number range (0-127)
            if (number < 0 || number > 127) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid note number %d (must be 0-127)", number);
                log_error("MIDI_MAP", msg);
                return -1;
            }
            control->type = MIDI_MSG_NOTE_ON;
            control->number = number;
            control->channel = -1;
            return 0;
        }
    }
    
    // Check for wildcard note with optional channel (e.g., "NOTE:*" or "NOTE:*:1")
    if (sscanf(str, "%15[^:]:%*[*]:%d", type_str, &channel) == 2) {
        if (strcmp(type_str, "NOTE") == 0) {
            if (channel < 0 || channel > 15) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid MIDI channel %d (must be 0-15), using any channel", channel);
                log_warning("MIDI_MAP", msg);
                channel = -1;
            }
            control->type = MIDI_MSG_NOTE_ON;
            control->number = -1; // Any note
            control->channel = channel;
            return 0;
        }
    }
    
    // Check for wildcard note without channel (backward compatible)
    if (strcmp(str, "NOTE:*") == 0) {
        control->type = MIDI_MSG_NOTE_ON;
        control->number = -1; // Any note
        control->channel = -1; // Any channel
        return 0;
    }
    
    return -1;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - CONFIGURATION
 * ============================================================================ */

/**
 * Create default MIDI mapping file with all mappings set to 'none'
 */
static int create_default_midi_mapping_file(const char *mapping_file) {
    FILE *file = fopen(mapping_file, "w");
    if (!file) {
        log_error("MIDI_MAP", "Cannot create MIDI mapping file '%s': %s", 
                  mapping_file, strerror(errno));
        return -1;
    }
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# MIDI MAPPING CONFIGURATION\n");
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# Format: parameter_name=TYPE:NUMBER\n");
    fprintf(file, "# Types: CC (Control Change), NOTE (Note On/Off), PITCHBEND\n");
    fprintf(file, "# Use \"none\" to disable a mapping\n");
    fprintf(file, "#\n");
    fprintf(file, "# Examples:\n");
    fprintf(file, "#   master_volume=CC:1        # CC1 controls master volume\n");
    fprintf(file, "#   note_on=NOTE:*            # All MIDI notes trigger note on\n");
    fprintf(file, "#   freeze=CC:105             # CC105 triggers freeze\n");
    fprintf(file, "#\n");
    fprintf(file, "# See midi_params.ini for parameter ranges and defaults\n");
    fprintf(file, "# See MIDI_SYSTEM_SPECIFICATION.md for complete documentation\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[MIDI_DEVICE]\n");
    fprintf(file, "device_name=auto              # \"auto\" or specific device name\n");
    fprintf(file, "device_id=auto                # \"auto\" or specific device ID\n\n");
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# AUDIO GLOBAL\n");
    fprintf(file, "# ============================================================================\n\n");
    fprintf(file, "[AUDIO_GLOBAL]\n");
    fprintf(file, "master_volume=none            # Master output volume\n");
    fprintf(file, "reverb_mix=none               # Reverb dry/wet mix\n");
    fprintf(file, "reverb_size=none              # Reverb room size\n");
    fprintf(file, "reverb_damp=none              # Reverb high frequency damping\n");
    fprintf(file, "reverb_width=none             # Reverb stereo width\n");
    fprintf(file, "eq_low_gain=none              # EQ low frequency gain\n");
    fprintf(file, "eq_mid_gain=none              # EQ mid frequency gain\n");
    fprintf(file, "eq_high_gain=none             # EQ high frequency gain\n");
    fprintf(file, "eq_mid_freq=none              # EQ mid frequency center\n\n");
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYNTHESIS ADDITIVE\n");
    fprintf(file, "# ============================================================================\n\n");
    fprintf(file, "[SYNTH_ADDITIVE]\n");
    fprintf(file, "volume=none                   # Additive synthesis mix level\n");
    fprintf(file, "reverb_send=none              # Additive reverb send amount\n\n");
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYNTHESIS POLYPHONIC\n");
    fprintf(file, "# ============================================================================\n\n");
    fprintf(file, "[SYNTH_POLYPHONIC]\n");
    fprintf(file, "volume=none                   # Polyphonic synthesis mix level\n");
    fprintf(file, "reverb_send=none              # Polyphonic reverb send amount\n");
    fprintf(file, "note_on=none                  # MIDI note on (use NOTE:* for all notes)\n");
    fprintf(file, "note_off=none                 # MIDI note off (use NOTE:* for all notes)\n");
    fprintf(file, "volume_env_attack=none        # Volume envelope attack time\n");
    fprintf(file, "volume_env_decay=none         # Volume envelope decay time\n");
    fprintf(file, "volume_env_sustain=none       # Volume envelope sustain level\n");
    fprintf(file, "volume_env_release=none       # Volume envelope release time\n");
    fprintf(file, "filter_env_attack=none        # Filter envelope attack time\n");
    fprintf(file, "filter_env_decay=none         # Filter envelope decay time\n");
    fprintf(file, "filter_env_sustain=none       # Filter envelope sustain level\n");
    fprintf(file, "filter_env_release=none       # Filter envelope release time\n");
    fprintf(file, "lfo_vibrato_rate=none         # LFO vibrato rate\n");
    fprintf(file, "lfo_vibrato_depth=none        # LFO vibrato depth\n");
    fprintf(file, "filter_cutoff=none            # Lowpass filter cutoff frequency\n");
    fprintf(file, "filter_env_depth=none         # Filter envelope modulation depth\n\n");
    
    // Generate sequencer player sections (1-5)
    for (int player = 1; player <= 5; player++) {
        fprintf(file, "# ============================================================================\n");
        fprintf(file, "# SEQUENCER - PLAYER %d\n", player);
        fprintf(file, "# ============================================================================\n\n");
        fprintf(file, "[SEQUENCER_PLAYER_%d]\n", player);
        fprintf(file, "record_toggle=none            # Toggle recording\n");
        fprintf(file, "play_stop=none                # Toggle playback/pause\n");
        fprintf(file, "mute_toggle=none              # Toggle mute\n");
        fprintf(file, "speed=none                    # Playback speed multiplier\n");
        fprintf(file, "exposure=none                 # Exposure control (0%=dark, 50%=normal, 100%=blown out)\n");
        fprintf(file, "offset=none                   # Playback start offset\n");
        fprintf(file, "env_attack=none               # ADSR attack time\n");
        fprintf(file, "env_decay=none                # ADSR decay time\n");
        fprintf(file, "env_sustain=none              # ADSR sustain level\n");
        fprintf(file, "env_release=none              # ADSR release time\n");
        fprintf(file, "loop_mode=none                # Loop mode selector (0=SIMPLE, 1=PINGPONG, 2=ONESHOT)\n");
        fprintf(file, "playback_direction=none       # Playback direction (0=FORWARD, 1=REVERSE)\n\n");
    }
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SEQUENCER - GLOBAL\n");
    fprintf(file, "# ============================================================================\n\n");
    fprintf(file, "[SEQUENCER_GLOBAL]\n");
    fprintf(file, "live_mix_level=none           # Live input mix level\n");
    fprintf(file, "blend_mode=none               # Blending mode selector (0=MIX, 1=ADD, 2=SCREEN, 3=MASK)\n");
    fprintf(file, "master_tempo=none             # Manual BPM control\n");
    fprintf(file, "quantize_res=none             # Quantization resolution\n\n");
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYSTEM\n");
    fprintf(file, "# ============================================================================\n\n");
    fprintf(file, "[SYSTEM]\n");
    fprintf(file, "freeze=none                   # Freeze synth data\n");
    fprintf(file, "resume=none                   # Resume synth data with fade\n");
    
    fclose(file);
    
    log_info("MIDI_MAP", "Created default mapping file with all mappings disabled: %s", mapping_file);
    return 0;
}

/**
 * Create default MIDI parameters file with complete specifications
 */
static int create_default_midi_params_file(const char *params_file) {
    FILE *file = fopen(params_file, "w");
    if (!file) {
        log_error("MIDI_MAP", "Cannot create MIDI parameters file '%s'", params_file);
        return -1;
    }
    
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# MIDI PARAMETERS SPECIFICATIONS\n");
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# This file defines default values, ranges, and scaling for all MIDI-controllable parameters\n");
    fprintf(file, "# Format per parameter:\n");
    fprintf(file, "#   [SECTION.parameter_name]\n");
    fprintf(file, "#   default=<value>      # Default value\n");
    fprintf(file, "#   min=<value>          # Minimum value\n");
    fprintf(file, "#   max=<value>          # Maximum value\n");
    fprintf(file, "#   scaling=<type>       # linear, logarithmic, exponential, discrete\n");
    fprintf(file, "#   type=button          # Optional: for momentary triggers\n");
    fprintf(file, "#\n");
    fprintf(file, "# See MIDI_SYSTEM_SPECIFICATION.md for complete documentation\n");
    fprintf(file, "# ============================================================================\n\n");
    
    // AUDIO GLOBAL PARAMETERS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# AUDIO GLOBAL PARAMETERS\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.master_volume]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.reverb_mix]\n");
    fprintf(file, "default=0.3\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.reverb_size]\n");
    fprintf(file, "default=0.5\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.reverb_damp]\n");
    fprintf(file, "default=0.5\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.reverb_width]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.eq_low_gain]\n");
    fprintf(file, "default=0.0\nmin=-12.0\nmax=12.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.eq_mid_gain]\n");
    fprintf(file, "default=0.0\nmin=-12.0\nmax=12.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.eq_high_gain]\n");
    fprintf(file, "default=0.0\nmin=-12.0\nmax=12.0\nscaling=linear\n\n");
    
    fprintf(file, "[AUDIO_GLOBAL.eq_mid_freq]\n");
    fprintf(file, "default=1000.0\nmin=200.0\nmax=5000.0\nscaling=logarithmic\n\n");
    
    // SYNTHESIS ADDITIVE PARAMETERS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYNTHESIS ADDITIVE PARAMETERS\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[SYNTH_ADDITIVE.volume]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_ADDITIVE.reverb_send]\n");
    fprintf(file, "default=0.2\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    // SYNTHESIS POLYPHONIC PARAMETERS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYNTHESIS POLYPHONIC PARAMETERS\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.volume]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.reverb_send]\n");
    fprintf(file, "default=0.2\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.lfo_vibrato_rate]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=20.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.lfo_vibrato_depth]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=2.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.volume_env_attack]\n");
    fprintf(file, "default=0.01\nmin=0.001\nmax=5.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.volume_env_decay]\n");
    fprintf(file, "default=0.1\nmin=0.001\nmax=5.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.volume_env_sustain]\n");
    fprintf(file, "default=0.8\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.volume_env_release]\n");
    fprintf(file, "default=0.5\nmin=0.001\nmax=10.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_env_attack]\n");
    fprintf(file, "default=0.02\nmin=0.001\nmax=5.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_env_decay]\n");
    fprintf(file, "default=0.2\nmin=0.001\nmax=5.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_env_sustain]\n");
    fprintf(file, "default=0.1\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_env_release]\n");
    fprintf(file, "default=0.3\nmin=0.001\nmax=10.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_cutoff]\n");
    fprintf(file, "default=8000.0\nmin=20.0\nmax=20000.0\nscaling=logarithmic\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.filter_env_depth]\n");
    fprintf(file, "default=-7800.0\nmin=-10000.0\nmax=10000.0\nscaling=linear\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.note_on]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=127.0\nscaling=discrete\ntype=button\n\n");
    
    fprintf(file, "[SYNTH_POLYPHONIC.note_off]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=127.0\nscaling=discrete\ntype=button\n\n");
    
    // SEQUENCER PLAYER DEFAULTS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SEQUENCER PLAYER DEFAULTS (applied to all 5 players)\n");
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# These defaults are automatically expanded to individual player parameters\n");
    fprintf(file, "# (sequencer_player_1_*, sequencer_player_2_*, etc.)\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.record_toggle]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\ntype=button\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.play_stop]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\ntype=button\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.mute_toggle]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\ntype=button\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.speed]\n");
    fprintf(file, "default=1.0\nmin=0.1\nmax=4.0\nscaling=exponential\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.exposure]\n");
    fprintf(file, "default=0.5\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.offset]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.env_attack]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.env_decay]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.env_sustain]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.env_release]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.loop_mode]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=2.0\nscaling=discrete\n\n");
    
    fprintf(file, "[SEQUENCER_PLAYER_DEFAULTS.playback_direction]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\n\n");
    
    // SEQUENCER GLOBAL PARAMETERS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SEQUENCER GLOBAL PARAMETERS\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[SEQUENCER_GLOBAL.live_mix_level]\n");
    fprintf(file, "default=1.0\nmin=0.0\nmax=1.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_GLOBAL.blend_mode]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=3.0\nscaling=discrete\n\n");
    
    fprintf(file, "[SEQUENCER_GLOBAL.master_tempo]\n");
    fprintf(file, "default=120.0\nmin=40.0\nmax=240.0\nscaling=linear\n\n");
    
    fprintf(file, "[SEQUENCER_GLOBAL.quantize_res]\n");
    fprintf(file, "default=4.0\nmin=1.0\nmax=16.0\nscaling=discrete\n\n");
    
    // SYSTEM PARAMETERS
    fprintf(file, "# ============================================================================\n");
    fprintf(file, "# SYSTEM PARAMETERS\n");
    fprintf(file, "# ============================================================================\n\n");
    
    fprintf(file, "[SYSTEM.freeze]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\ntype=button\n\n");
    
    fprintf(file, "[SYSTEM.resume]\n");
    fprintf(file, "default=0.0\nmin=0.0\nmax=1.0\nscaling=discrete\ntype=button\n");
    
    fclose(file);
    log_info("MIDI_MAP", "Created complete default parameters file: %s", params_file);
    return 0;
}

int midi_mapping_load_parameters(const char *params_file) {
    if (!g_midi_system.is_initialized) {
        log_error("MIDI_MAP", "MIDI mapping system not initialized");
        return -1;
    }
    
    FILE *file = fopen(params_file, "r");
    if (!file) {
        log_info("MIDI_MAP", "Parameters file '%s' not found, creating with defaults", params_file);
        if (create_default_midi_params_file(params_file) != 0) {
            return -1;
        }
        // Try to open again
        file = fopen(params_file, "r");
        if (!file) {
            log_error("MIDI_MAP", "Cannot open MIDI parameters file '%s' after creation", params_file);
            return -1;
        }
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
                log_error("MIDI_MAP", "Line %d: Invalid section header", line_number);
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
                    log_error("MIDI_MAP", "Maximum number of parameters reached");
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
    
    // Post-processing: Expand SEQUENCER_PLAYER_DEFAULTS to individual players
    // Find all DEFAULTS parameters and duplicate them for players 1-5
    int defaults_count = 0;
    ParameterEntry defaults_params[50]; // Temporary storage for defaults
    
    // First pass: collect all DEFAULTS parameters
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        if (strncmp(g_midi_system.parameters[i].name, "sequencer_player_defaults_", 26) == 0) {
            if (defaults_count < 50) {
                defaults_params[defaults_count++] = g_midi_system.parameters[i];
            }
        }
    }
    
    // Second pass: create individual player parameters from defaults
    if (defaults_count > 0) {
        log_info("MIDI_MAP", "Expanding %d SEQUENCER_PLAYER_DEFAULTS to 5 players", defaults_count);
        
        for (int player = 1; player <= 5; player++) {
            for (int d = 0; d < defaults_count; d++) {
                // Extract parameter suffix (e.g., "speed" from "sequencer_player_defaults_speed")
                const char *suffix = defaults_params[d].name + 26; // Skip "sequencer_player_defaults_"
                
                // Build player-specific name (e.g., "sequencer_player_1_speed")
                char player_param_name[MIDI_MAX_PARAM_NAME];
                snprintf(player_param_name, MIDI_MAX_PARAM_NAME, "sequencer_player_%d_%s", player, suffix);
                
                // Check if this player parameter already exists (override case)
                ParameterEntry *existing = find_parameter(player_param_name);
                if (existing) {
                    // Parameter already defined specifically for this player, skip
                    continue;
                }
                
                // Create new parameter entry for this player
                if (g_midi_system.num_parameters >= MIDI_MAX_PARAMETERS) {
                    log_warning("MIDI_MAP", "Maximum parameters reached while expanding defaults");
                    break;
                }
                
                ParameterEntry *new_param = &g_midi_system.parameters[g_midi_system.num_parameters++];
                *new_param = defaults_params[d]; // Copy all fields
                strncpy(new_param->name, player_param_name, MIDI_MAX_PARAM_NAME - 1);
                new_param->name[MIDI_MAX_PARAM_NAME - 1] = '\0';
                new_param->is_mapped = 0; // Will be set by load_mappings if needed
            }
        }
        
        log_info("MIDI_MAP", "Expanded to %d total parameters", g_midi_system.num_parameters);
    }
    
    log_info("MIDI_MAP", "Loaded %d parameter specifications from %s", 
             g_midi_system.num_parameters, params_file);
    return 0;
}

int midi_mapping_load_mappings(const char *config_file) {
    if (!g_midi_system.is_initialized) {
        log_error("MIDI_MAP", "MIDI mapping system not initialized");
        return -1;
    }
    
    FILE *file = fopen(config_file, "r");
    if (!file) {
        log_info("MIDI_MAP", "Mappings file '%s' not found, creating with all mappings disabled (none)", config_file);
        if (create_default_midi_mapping_file(config_file) != 0) {
            return -1;
        }
        // Try to open again
        file = fopen(config_file, "r");
        if (!file) {
            log_error("MIDI_MAP", "Cannot open MIDI mappings file '%s' after creation", config_file);
            return -1;
        }
    }
    
    char line[256];
    char current_section[64] = "";
    int line_number = 0;
    int mappings_loaded = 0;
    int in_midi_device_section = 0; // Flag for MIDI_DEVICE section
    
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
                log_error("MIDI_MAP", "Line %d: Invalid section header", line_number);
                fclose(file);
                return -1;
            }
            *end = '\0';
            const char *section_name = trimmed + 1;
            
            // Handle MIDI_DEVICE section specially - read device config
            if (strcmp(section_name, "MIDI_DEVICE") == 0) {
                in_midi_device_section = 1;
                current_section[0] = '\0'; // Clear current section
                continue;
            }
            
            in_midi_device_section = 0; // Reset flag for other sections
            strncpy(current_section, section_name, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            continue;
        }
        
        // Handle MIDI_DEVICE section parameters
        if (in_midi_device_section) {
            // Parse device_name and device_id
            char *equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                char *key = trim_whitespace(trimmed);
                char *value = trim_whitespace(equals + 1);
                
                // Remove inline comments from value
                remove_inline_comment(value);
                value = trim_whitespace(value);
                
                if (strcmp(key, "device_name") == 0) {
                    strncpy(g_midi_device_name, value, sizeof(g_midi_device_name) - 1);
                    g_midi_device_name[sizeof(g_midi_device_name) - 1] = '\0';
                    log_info("MIDI_MAP", "MIDI device_name configured: '%s'", g_midi_device_name);
                } else if (strcmp(key, "device_id") == 0) {
                    if (strcmp(value, "auto") == 0) {
                        g_midi_device_id = -1;
                    } else {
                        g_midi_device_id = atoi(value);
                    }
                    log_info("MIDI_MAP", "MIDI device_id configured: %d", g_midi_device_id);
                }
            }
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
        
        // Remove inline comments from value
        remove_inline_comment(value);
        value = trim_whitespace(value);
        
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
            char msg[256];
            snprintf(msg, sizeof(msg), "Line %d: Unknown parameter '%s' (looked for '%s')", 
                     line_number, key, full_param_name);
            log_warning("MIDI_MAP", msg);
            continue;
        }
        
        // Parse MIDI control
        MidiControl control;
        if (parse_midi_control(value, &control) == 0) {
            if (control.type != MIDI_MSG_NONE) {
                // BUGFIX: If parameter name ends with "_note_off", change type to NOTE_OFF
                // This allows note_on and note_off to share the same MIDI control but respond to different message types
                size_t param_len = strlen(full_param_name);
                if (param_len > 9 && strcmp(full_param_name + param_len - 9, "_note_off") == 0) {
                    if (control.type == MIDI_MSG_NOTE_ON) {
                        control.type = MIDI_MSG_NOTE_OFF;
                    }
                }
                
                param->control = control;
                param->is_mapped = 1;
                mappings_loaded++;
            }
            // Silently accept "none" - it's a valid way to disable a mapping
        } else {
            // Only warn if it's not "none" (which is handled above)
            if (strcmp(value, "none") != 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Line %d: Invalid MIDI control format '%s'", line_number, value);
                log_warning("MIDI_MAP", msg);
            }
        }
    }
    
    fclose(file);
    log_info("MIDI_MAP", "Loaded %d MIDI mappings from %s", mappings_loaded, config_file);
    
    // Log channel-specific mappings for debugging
    int channel_specific = 0;
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        if (g_midi_system.parameters[i].is_mapped && 
            g_midi_system.parameters[i].control.channel != -1) {
            channel_specific++;
        }
    }
    if (channel_specific > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "  %d mappings use specific MIDI channels", channel_specific);
        log_info("MIDI_MAP", msg);
    }
    
    return 0;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - CALLBACK REGISTRATION
 * ============================================================================ */

int midi_mapping_register_callback(const char *param_name, 
                                   MidiCallback callback, 
                                   void *user_data) {
    if (!g_midi_system.is_initialized) {
        log_error("MIDI_MAP", "MIDI mapping system not initialized");
        return -1;
    }
    
    if (!param_name || !callback) {
        log_error("MIDI_MAP", "Invalid parameters for callback registration");
        return -1;
    }
    
    if (g_midi_system.num_callbacks >= MIDI_MAX_CALLBACKS) {
        log_error("MIDI_MAP", "Maximum number of callbacks reached");
        return -1;
    }
    
    // Check if parameter exists (optional, depends on initialization order)
    ParameterEntry *param = find_parameter(param_name);
    if (!param) {
        log_warning("MIDI_MAP", "Registering callback for unknown parameter: %s", param_name);
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
    
    // BUGFIX: Find ALL parameters mapped to this MIDI control (not just the first one)
    // This allows multiple synths to respond to the same MIDI notes
    int matches_found = 0;
    
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        ParameterEntry *param = &g_midi_system.parameters[i];
        
        if (!param->is_mapped) continue;
        
        // Check if this parameter matches the MIDI control
        // Support wildcard matching: number=-1 means "any note/CC number"
        int number_matches = (param->control.number == number) || (param->control.number == -1);
        
        if (param->control.type == type && number_matches) {
            // Check channel (-1 means any channel)
            if (param->control.channel == -1 || param->control.channel == channel) {
                matches_found++;
                
                // Log mapped message at DEBUG level with real parameter value
                float real_value = normalized_to_raw(midi_to_normalized(value), &param->spec);
                log_debug("MIDI_MAP", "Mapped to '%s': MIDI=%d → %.2f %s", 
                         param->name, value, real_value,
                         param->spec.is_button ? "(trigger)" : "");
                
                // BUGFIX: For NOTE_ON and NOTE_OFF messages, we need to pass both note number and velocity
                // The callback expects: raw_value = note number, value = normalized velocity
                if (type == MIDI_MSG_NOTE_ON || type == MIDI_MSG_NOTE_OFF) {
                    // For notes: raw_value = note number, value = normalized velocity
                    param->current_raw_value = (float)number;  // Note number (0-127)
                    param->current_value = midi_to_normalized(value);  // Normalized velocity (0.0-1.0)
                    
                    // Prepare callback data with note-specific values
                    MidiParameterValue callback_data = {
                        .value = param->current_value,           // Normalized velocity
                        .raw_value = param->current_raw_value,   // Note number
                        .param_name = param->name,
                        .is_button = param->spec.is_button
                    };
                    
                    // Trigger all registered callbacks for this parameter
                    for (int j = 0; j < g_midi_system.num_callbacks; j++) {
                        CallbackEntry *cb = &g_midi_system.callbacks[j];
                        
                        if (cb->is_active && strcmp(cb->param_name, param->name) == 0) {
                            if (cb->callback) {
                                cb->callback(&callback_data, cb->user_data);
                            }
                        }
                    }
                } else {
                    // For CC and other messages: use standard parameter update
                    float normalized = midi_to_normalized(value);
                    
                    // Handle button triggers differently
                    if (param->spec.is_button) {
                        // For buttons, trigger on both press AND release
                        // Prepare callback data with button state
                        MidiParameterValue callback_data = {
                            .value = (value > 64) ? 1.0f : 0.0f,
                            .raw_value = param->current_raw_value,
                            .param_name = param->name,
                            .is_button = 1,
                            .button_pressed = (value > 64) ? 1 : 0
                        };
                        
                        // Update internal state
                        param->current_value = callback_data.value;
                        
                        // Trigger all registered callbacks for this parameter
                        for (int j = 0; j < g_midi_system.num_callbacks; j++) {
                            CallbackEntry *cb = &g_midi_system.callbacks[j];
                            
                            if (cb->is_active && strcmp(cb->param_name, param->name) == 0) {
                                if (cb->callback) {
                                    cb->callback(&callback_data, cb->user_data);
                                }
                            }
                        }
                    } else {
                        // For continuous parameters: use standard parameter update
                        update_parameter_value(param, normalized);
                    }
                }
            }
        }
    }
    
    if (matches_found == 0) {
        // No mapping for this control - log at debug level
        const char *type_str = "UNKNOWN";
        if (type == MIDI_MSG_CC) type_str = "CC";
        else if (type == MIDI_MSG_NOTE_ON) type_str = "NOTE_ON";
        else if (type == MIDI_MSG_NOTE_OFF) type_str = "NOTE_OFF";
        else if (type == MIDI_MSG_PITCHBEND) type_str = "PITCHBEND";
        
        log_debug("MIDI_MAP", "Unmapped: %s:%d (ch=%d, val=%d)", type_str, number, channel, value);
    }
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
        log_error("MIDI_MAP", "MIDI mapping system not initialized");
        return -1;
    }
    
    log_debug("MIDI_MAP", "midi_mapping_apply_defaults: Starting...");
    log_debug("MIDI_MAP", "Total parameters: %d", g_midi_system.num_parameters);
    
    int count = 0;
    
    // Apply default values to all parameters
    for (int i = 0; i < g_midi_system.num_parameters; i++) {
        ParameterEntry *param = &g_midi_system.parameters[i];
        
        // BUGFIX: Skip buttons - they should only be triggered by actual MIDI events
        // Buttons are momentary triggers, not persistent states
        if (param->spec.is_button) {
            log_debug("MIDI_MAP", "Skipping button parameter '%s' (buttons not initialized)", param->name);
            continue;
        }
        
        log_debug("MIDI_MAP", "Processing parameter '%s'", param->name);
        log_debug("MIDI_MAP", "  Default: %.3f, Min: %.3f, Max: %.3f", 
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
            log_debug("MIDI_MAP", "  Discrete/Button: normalized = %.3f", normalized_default);
        } else {
            // For continuous parameters, reverse the scaling to get normalized value
            float default_val = param->spec.default_value;
            float min_val = param->spec.min_value;
            float max_val = param->spec.max_value;
            
            switch (param->spec.scaling) {
                case MIDI_SCALE_LINEAR:
                    normalized_default = (default_val - min_val) / (max_val - min_val);
                    log_debug("MIDI_MAP", "  Linear scaling: normalized = %.3f", normalized_default);
                    break;
                    
                case MIDI_SCALE_LOGARITHMIC:
                    if (min_val > 0.0f && max_val > 0.0f && default_val > 0.0f) {
                        float log_min = logf(min_val);
                        float log_max = logf(max_val);
                        float log_val = logf(default_val);
                        normalized_default = (log_val - log_min) / (log_max - log_min);
                        log_debug("MIDI_MAP", "  Logarithmic scaling: normalized = %.3f", normalized_default);
                    } else {
                        normalized_default = 0.5f;
                        log_debug("MIDI_MAP", "  Logarithmic scaling FALLBACK: normalized = %.3f", normalized_default);
                    }
                    break;
                    
                case MIDI_SCALE_EXPONENTIAL:
                    if (min_val > 0.0f && max_val > min_val && default_val > 0.0f) {
                        float exp_range = max_val / min_val;
                        normalized_default = logf(default_val / min_val) / logf(exp_range);
                        log_debug("MIDI_MAP", "  Exponential scaling: normalized = %.3f", normalized_default);
                    } else {
                        normalized_default = 0.5f;
                        log_debug("MIDI_MAP", "  Exponential scaling FALLBACK: normalized = %.3f", normalized_default);
                    }
                    break;
                    
                default:
                    normalized_default = (default_val - min_val) / (max_val - min_val);
                    log_debug("MIDI_MAP", "  Default scaling: normalized = %.3f", normalized_default);
                    break;
            }
        }
        
        // Clamp to valid range
        if (normalized_default < 0.0f) normalized_default = 0.0f;
        if (normalized_default > 1.0f) normalized_default = 1.0f;
        
        log_debug("MIDI_MAP", "  Calling update_parameter_value with normalized = %.3f", normalized_default);
        
        // Update parameter (this will trigger callbacks)
        update_parameter_value(param, normalized_default);
        count++;
    }
    
    log_debug("MIDI_MAP", "midi_mapping_apply_defaults: Completed, applied %d defaults", count);
    log_info("MIDI_MAP", "Applied default values to %d parameters", count);
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
        const char *name1 = g_midi_system.parameters[i].name;
        
        for (int j = i + 1; j < g_midi_system.num_parameters; j++) {
            if (!g_midi_system.parameters[j].is_mapped) continue;
            
            MidiControl *ctrl2 = &g_midi_system.parameters[j].control;
            const char *name2 = g_midi_system.parameters[j].name;
            
            // Check if same MIDI control
            if (ctrl1->type == ctrl2->type && ctrl1->number == ctrl2->number) {
                // Check channel conflict
                if (ctrl1->channel == ctrl2->channel || 
                    ctrl1->channel == -1 || ctrl2->channel == -1) {
                    
                    // BUGFIX: Allow note_on/note_off pairs to share the same MIDI control
                    // This is a valid and common configuration for note-based synthesis
                    int is_note_on_off_pair = 0;
                    
                    // Check if one ends with "_note_on" and the other with "_note_off"
                    size_t len1 = strlen(name1);
                    size_t len2 = strlen(name2);
                    
                    if (len1 > 8 && len2 > 9) {
                        int name1_is_note_on = (strcmp(name1 + len1 - 8, "_note_on") == 0);
                        int name1_is_note_off = (strcmp(name1 + len1 - 9, "_note_off") == 0);
                        int name2_is_note_on = (strcmp(name2 + len2 - 8, "_note_on") == 0);
                        int name2_is_note_off = (strcmp(name2 + len2 - 9, "_note_off") == 0);
                        
                        // Check if they form a note_on/note_off pair
                        if ((name1_is_note_on && name2_is_note_off) || 
                            (name1_is_note_off && name2_is_note_on)) {
                            // Extract base names (everything before _note_on/_note_off)
                            char base1[MIDI_MAX_PARAM_NAME];
                            char base2[MIDI_MAX_PARAM_NAME];
                            
                            if (name1_is_note_on) {
                                strncpy(base1, name1, len1 - 8);
                                base1[len1 - 8] = '\0';
                            } else {
                                strncpy(base1, name1, len1 - 9);
                                base1[len1 - 9] = '\0';
                            }
                            
                            if (name2_is_note_on) {
                                strncpy(base2, name2, len2 - 8);
                                base2[len2 - 8] = '\0';
                            } else {
                                strncpy(base2, name2, len2 - 9);
                                base2[len2 - 9] = '\0';
                            }
                            
                            // If base names match, this is a valid note_on/note_off pair
                            if (strcmp(base1, base2) == 0) {
                                is_note_on_off_pair = 1;
                            }
                        }
                    }
                    
                    // Only report conflict if it's not a note_on/note_off pair
                    if (!is_note_on_off_pair) {
                        log_warning("MIDI_MAP", "MIDI conflict: %s and %s both use same control",
                                    name1, name2);
                        conflicts++;
                    }
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

/* ============================================================================
 * PUBLIC API IMPLEMENTATION - DEVICE CONFIGURATION
 * ============================================================================ */

const char* midi_mapping_get_device_name(void) {
    return g_midi_device_name;
}

int midi_mapping_get_device_id(void) {
    return g_midi_device_id;
}
