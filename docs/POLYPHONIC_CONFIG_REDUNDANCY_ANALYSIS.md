# Polyphonic Configuration Redundancy Analysis

**Date:** 2025-11-23  
**Status:** Analysis Complete  
**Priority:** Medium (Configuration Cleanup)

## Executive Summary

The polyphonic synthesis configuration in `sp3ctra.ini` contains **significant redundancy** between two sections:
- `[polyphonic]` - Engine default parameters (loaded at startup)
- `[synth_polyphonic]` - MIDI-controllable parameters (runtime control)

**Total redundant lines:** ~48 configuration lines (parameters + metadata)

## Detailed Analysis

### 1. Architecture Overview

```
sp3ctra.ini
├── [polyphonic]           → Loads into g_sp3ctra_config.poly_*
│   └── Used by: synth_polyphonic_init()
│
└── [synth_polyphonic]     → Parsed by MIDI mapping system
    └── Used by: MIDI callbacks → synth_polyphonic_set_*() functions
```

### 2. Redundant Parameter Groups

#### A. Volume ADSR Envelope (16 lines)

**[polyphonic] section:**
```ini
volume_adsr_attack_s = 0.01
volume_adsr_attack_s_min = 0.001
volume_adsr_attack_s_max = 5.0
volume_adsr_attack_s_scaling = exponential
# ... (decay, sustain, release with same pattern)
```

**[synth_polyphonic] section:**
```ini
volume_env_attack = 0.01
volume_env_attack_min = 0.001
volume_env_attack_max = 5.0
volume_env_attack_scaling = exponential
# ... (decay, sustain, release with same pattern)
```

**Code mapping:**
- `[polyphonic]` → `g_sp3ctra_config.poly_volume_adsr_attack_s`
- `[synth_polyphonic]` → MIDI callback → `synth_polyphonic_set_volume_adsr_attack()`
- **Both modify the same global config variable!**

#### B. Filter ADSR Envelope (16 lines)

Same redundancy pattern:
- `filter_adsr_attack_s` vs `filter_env_attack`
- `filter_adsr_decay_s` vs `filter_env_decay`
- `filter_adsr_sustain_level` vs `filter_env_sustain`
- `filter_adsr_release_s` vs `filter_env_release`

#### C. LFO/Vibrato Parameters (8 lines)

- `lfo_rate_hz` vs `lfo_vibrato_rate`
- `lfo_depth_semitones` vs `lfo_vibrato_depth`

#### D. Spectral Filter Parameters (8 lines)

- `filter_cutoff_hz` vs `filter_cutoff`
- `filter_env_depth_hz` vs `filter_env_depth`

### 3. Code Flow Analysis

#### Initialization (synth_polyphonic.c)
```c
void synth_polyphonicMode_init(void) {
    // Reads from [polyphonic] section
    global_spectral_filter_params.base_cutoff_hz = 
        g_sp3ctra_config.poly_filter_cutoff_hz;
    
    lfo_init(&global_vibrato_lfo, 
             g_sp3ctra_config.poly_lfo_rate_hz,
             g_sp3ctra_config.poly_lfo_depth_semitones, ...);
    
    adsr_init_envelope(&poly_voices[i].volume_adsr,
                       g_sp3ctra_config.poly_volume_adsr_attack_s,
                       g_sp3ctra_config.poly_volume_adsr_decay_s, ...);
}
```

#### MIDI Runtime Control (midi_callbacks.cpp)
```cpp
case PARAM_SYNTH_POLYPHONIC_VOLUME_ENV_ATTACK:
    // Reads from [synth_polyphonic] section
    synth_polyphonic_set_volume_adsr_attack(param->raw_value);
    break;
```

#### Setter Functions (synth_polyphonic.c)
```c
void synth_polyphonic_set_volume_adsr_attack(float attack_s) {
    // MODIFIES THE SAME GLOBAL CONFIG!
    g_sp3ctra_config.poly_volume_adsr_attack_s = attack_s;
    
    // Updates all voices
    for (int i = 0; i < g_num_poly_voices; ++i) {
        adsr_update_settings_and_recalculate_rates(
            &poly_voices[i].volume_adsr,
            g_sp3ctra_config.poly_volume_adsr_attack_s, ...);
    }
}
```

### 4. Why This Redundancy Exists

This pattern follows the **MIDI parameter mapping architecture** used throughout Sp3ctra:

1. **Engine section** (`[polyphonic]`): Defines default values loaded at startup
2. **MIDI section** (`[synth_polyphonic]`): Defines MIDI-controllable parameters with ranges/scaling
3. **Both sections** ultimately control the same runtime variables

**Similar patterns exist for:**
- `[photowave]` + `[synth_photowave]`
- `[synth_additive]` (envelope parameters)

### 5. Parameters Unique to Polyphonic Synthesis

These are **NOT redundant** and specific to the FFT-based polyphonic engine:

```ini
[polyphonic]
# Voice architecture
num_voices = 8                          # Polyphony count
max_oscillators = 128                   # Harmonics per voice

# FFT/Harmonic processing
min_audible_amplitude = 0.0001          # Amplitude threshold
amplitude_gamma = 1.5                   # Harmonic emphasis curve
high_freq_harmonic_limit_hz = 2000.0    # CPU optimization
max_harmonics_per_voice = 64            # CPU limit

# Performance tuning
master_volume = 0.3                     # Output level
amplitude_smoothing_alpha = 0.95        # Smoothing factor
norm_factor_bin0 = 1000000.0           # FFT normalization
norm_factor_harmonics = 500000.0       # FFT normalization
```

### 6. Comparison with Photowave

Photowave has the **same redundancy pattern**:

```ini
[photowave]
# Engine defaults (NOT MIDI-controllable)
continuous_mode = 0
scan_mode = 2
interp_mode = 0
amplitude = 0.20

[synth_photowave]
# MIDI-controllable parameters (REDUNDANT with photowave)
volume_env_attack = 0.01
filter_env_attack = 0.02
lfo_vibrato_rate = 5.0
filter_cutoff = 12000.0
# ... etc
```

## Root Cause

The redundancy stems from the **dual-purpose configuration system**:

1. **Config file** (`sp3ctra.ini`) defines:
   - Engine defaults (loaded once at startup)
   - MIDI parameter metadata (min/max/scaling)

2. **MIDI mapping** (`midi_mapping.ini`) defines:
   - Which MIDI CC controls which parameter
   - But parameter ranges come from `sp3ctra.ini`

3. **Runtime behavior:**
   - MIDI callbacks read `[synth_*]` sections for metadata
   - But modify the same `g_sp3ctra_config.poly_*` variables
   - That were initialized from `[polyphonic]` section

## Recommendations

### Option 1: Keep Current Architecture (Recommended)

**Rationale:**
- Consistent with photowave and additive synthesis patterns
- Clear separation: engine config vs MIDI control metadata
- MIDI system needs parameter ranges/scaling information
- Changing this would require major refactoring across all synthesis modes

**Action:** Document this as intentional design pattern

### Option 2: Eliminate [polyphonic] Section

**Changes required:**
- Remove `[polyphonic]` section entirely
- Load all defaults from `[synth_polyphonic]`
- Update `config_parser_table.h` to read from `synth_polyphonic` section
- Risk: breaks consistency with other synthesis modes

**Effort:** Medium (affects config loader, parser table)

### Option 3: Unified Configuration Format

**Long-term refactoring:**
- Single section per synthesis mode
- Metadata embedded differently (JSON/YAML?)
- Requires redesigning entire config system
- Would affect all synthesis modes

**Effort:** High (major architectural change)

## Conclusion

The redundancy is **intentional by design**, not a bug. It reflects the dual nature of parameters:
1. **Static defaults** (loaded at startup from `[polyphonic]`)
2. **MIDI control metadata** (ranges, scaling from `[synth_polyphonic]`)

Both sections ultimately control the same runtime variables (`g_sp3ctra_config.poly_*`), but serve different purposes in the configuration architecture.

**Recommendation:** Keep current architecture and document this pattern clearly in configuration documentation.

## Related Files

- `sp3ctra.ini` - Configuration file with redundant sections
- `src/config/config_loader.h` - Config structure definition
- `src/config/config_parser_table.h` - Parameter parsing table
- `src/synthesis/polyphonic/synth_polyphonic.c` - Uses poly_* config variables
- `src/communication/midi/midi_callbacks.cpp` - MIDI parameter handlers
- `midi_mapping.ini` - MIDI CC to parameter mapping
