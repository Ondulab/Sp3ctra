/**
 * @file synth_common.h
 * @brief Common synthesis structures shared between different synthesis engines
 * 
 * This header contains ADSR envelope and LFO structures that are used by
 * multiple synthesis engines (polyphonic, photowave, etc.)
 */

#ifndef SYNTH_COMMON_H
#define SYNTH_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ADSR ENVELOPE
 * ========================================================================== */

/**
 * @brief ADSR envelope states
 */
typedef enum {
    ADSR_STATE_IDLE,
    ADSR_STATE_ATTACK,
    ADSR_STATE_DECAY,
    ADSR_STATE_SUSTAIN,
    ADSR_STATE_RELEASE
} AdsrState;

/**
 * @brief ADSR envelope generator
 * 
 * Generates Attack-Decay-Sustain-Release envelope for volume or filter modulation.
 * All time values are converted to samples at initialization for RT-safe operation.
 */
typedef struct {
    AdsrState state;                // Current envelope state
    float attack_time_samples;      // Attack time in samples
    float decay_time_samples;       // Decay time in samples
    float sustain_level;            // Sustain level (0.0 to 1.0)
    float release_time_samples;     // Release time in samples
    
    float current_output;           // Current envelope output value (0.0 to 1.0)
    long long current_samples;      // Counter for samples in current state
    float attack_increment;         // Value to add per sample in attack phase
    float decay_decrement;          // Value to subtract per sample in decay phase
    float release_decrement;        // Value to subtract per sample in release phase
    
    // Original time parameters (for recalculation)
    float attack_s;
    float decay_s;
    float release_s;
} AdsrEnvelope;

/* ============================================================================
 * LFO (Low Frequency Oscillator)
 * ========================================================================== */

/**
 * @brief LFO state for vibrato and modulation effects
 * 
 * Generates sinusoidal modulation for pitch vibrato effect.
 */
typedef struct {
    float phase;                // Current phase (0 to 2π)
    float phase_increment;      // Phase increment per sample
    float current_output;       // Current LFO output (-1.0 to 1.0)
    float rate_hz;              // LFO frequency in Hz
    float depth_semitones;      // Modulation depth in semitones
} LfoState;

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_COMMON_H */
