/*
 * midi_callbacks.c
 *
 * MIDI Callback Functions Implementation
 *
 * Created: 30/10/2025
 * Author: Sp3ctra Team
 */

#include "midi_callbacks.h"
#include "midi_controller.h"
#include "../../audio/rtaudio/audio_rtaudio.h"
#include "../../audio/rtaudio/audio_c_api.h"  // For setSynthAdditiveMixLevel, setSynthPolyphonicMixLevel, setSynthPhotowaveMixLevel
#include "../../audio/effects/three_band_eq.h"
#include "../../audio/pan/lock_free_pan.h"
#include "../../synthesis/additive/synth_additive.h"
#include "../../synthesis/additive/synth_additive_algorithms.h"  // For update_gap_limiter_coefficients
#include "../../synthesis/polyphonic/synth_polyphonic.h"
#include "../../synthesis/photowave/synth_photowave.h"
#include "../../processing/image_sequencer.h"
#include "../../utils/logger.h"
#include <stdio.h>
#include <pthread.h>
#include <math.h>

/* External declarations for global objects */
extern AudioSystem *gAudioSystem;
extern ThreeBandEQ *gEqualizer;
extern MidiController *gMidiController;
extern ImageSequencer *g_image_sequencer;

/* External declarations for synth freeze control */
extern pthread_mutex_t g_synth_data_freeze_mutex;
extern volatile int g_is_synth_data_frozen;
extern volatile int g_is_synth_data_fading_out;

/* ============================================================================
 * AUDIO GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_audio_master_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        gAudioSystem->setMasterVolume(param->value);
        
        log_info("MASTER", "Volume: %d%%", (int)(param->value * 100));
    }
}

void midi_cb_audio_reverb_mix(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbMix(param->value);
        log_info("AUDIO", "Reverb mix set to %d%%", (int)(param->value * 100));
    }
}

void midi_cb_audio_reverb_size(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbRoomSize(param->value);
        log_info("AUDIO", "Reverb size set to %.2f", param->value);
    }
}

void midi_cb_audio_reverb_damp(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbDamping(param->value);
        log_info("AUDIO", "Reverb damping set to %.2f", param->value);
    }
}

void midi_cb_audio_reverb_width(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbWidth(param->value);
        log_info("AUDIO", "Reverb width set to %.2f", param->value);
    }
}

void midi_cb_audio_eq_low_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setLowGain(param->raw_value);
        log_info("AUDIO", "EQ Low gain set to %.1f dB", param->raw_value);
    }
}

void midi_cb_audio_eq_mid_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setMidGain(param->raw_value);
        log_info("AUDIO", "EQ Mid gain set to %.1f dB", param->raw_value);
    }
}

void midi_cb_audio_eq_high_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setHighGain(param->raw_value);
        log_info("AUDIO", "EQ High gain set to %.1f dB", param->raw_value);
    }
}

void midi_cb_audio_eq_mid_freq(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setMidFrequency(param->raw_value);
        log_info("AUDIO", "EQ Mid frequency set to %.0f Hz", param->raw_value);
    }
}

/* ============================================================================
 * SYNTHESIS ADDITIVE CALLBACKS
 * ============================================================================ */

void midi_cb_synth_additive_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set mix level directly (thread-safe)
    setSynthAdditiveMixLevel(param->value);
    
    log_info("ADDITIVE", "Volume: %d%%", (int)(param->value * 100));
}

void midi_cb_synth_additive_reverb_send(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set reverb send level for additive synth
    setReverbSendAdditive(param->value);
    
    if (gAudioSystem && param->value > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
    }
    
    log_info("ADDITIVE", "Reverb send: %d%%", (int)(param->value * 100));
}

/* ============================================================================
 * SYNTHESIS ADDITIVE ENVELOPE CALLBACKS
 * ============================================================================ */

void midi_cb_synth_additive_tau_up(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // RT-safe: Direct atomic write to config structure
    g_sp3ctra_config.tau_up_base_ms = param->raw_value;
    
    // Recalculate envelope coefficients for all oscillators
    update_gap_limiter_coefficients();
    
    log_info("ADDITIVE", "Envelope attack: %.3f ms", param->raw_value);
}

void midi_cb_synth_additive_tau_down(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // RT-safe: Direct atomic write to config structure
    g_sp3ctra_config.tau_down_base_ms = param->raw_value;
    
    // Recalculate envelope coefficients for all oscillators
    update_gap_limiter_coefficients();
    
    log_info("ADDITIVE", "Envelope release: %.3f ms", param->raw_value);
}

void midi_cb_synth_additive_decay_freq_ref(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // RT-safe: Direct atomic write to config structure
    g_sp3ctra_config.decay_freq_ref_hz = param->raw_value;
    
    // Recalculate envelope coefficients for all oscillators
    update_gap_limiter_coefficients();
    
    log_info("ADDITIVE", "Decay freq ref: %.1f Hz", param->raw_value);
}

void midi_cb_synth_additive_decay_freq_beta(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // RT-safe: Direct atomic write to config structure
    g_sp3ctra_config.decay_freq_beta = param->raw_value;
    
    // Recalculate envelope coefficients for all oscillators
    update_gap_limiter_coefficients();
    
    log_info("ADDITIVE", "Decay freq beta: %.2f", param->raw_value);
}

/* ============================================================================
 * SYNTHESIS ADDITIVE STEREO CALLBACKS
 * ============================================================================ */

// Global variables for stereo fade (0.0 = mono, 1.0 = stereo)
// Using volatile for thread visibility (simple case, no complex synchronization needed)
static volatile float g_stereo_fade_factor = 0.0f;
static volatile int g_stereo_fade_active = 0;
static volatile double g_stereo_fade_start_time = 0.0;

// Fade duration in seconds (20ms for smooth transition)
#define STEREO_FADE_DURATION_S 0.020

void midi_cb_synth_additive_stereo_toggle(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    int new_state = (int)param->raw_value;
    int current_state = g_sp3ctra_config.stereo_mode_enabled;
    
    // DEBUG: Always log the received value
    log_info("ADDITIVE", "Stereo toggle callback: raw_value=%.2f, new_state=%d, current_state=%d", 
             param->raw_value, new_state, current_state);
    
    if (new_state != current_state) {
        // Start fade transition
        g_stereo_fade_active = 1;
        g_stereo_fade_start_time = synth_getCurrentTimeInSeconds();
        
        // Update target state
        g_sp3ctra_config.stereo_mode_enabled = new_state;
        
        if (new_state) {
            // Initialize lock-free pan system if enabling stereo
            lock_free_pan_init();
            log_info("ADDITIVE", "Stereo mode ENABLED (fading in)");
        } else {
            log_info("ADDITIVE", "Stereo mode DISABLED (fading out)");
        }
    } else {
        log_info("ADDITIVE", "Stereo mode unchanged (already %s)", new_state ? "ENABLED" : "DISABLED");
    }
}

/**
 * Get current stereo fade factor (called from audio thread)
 * Returns 0.0 for mono, 1.0 for stereo, or intermediate value during fade
 */
float synth_additive_get_stereo_fade_factor(void) {
    if (!g_stereo_fade_active) {
        // No fade active, return target state directly
        return g_sp3ctra_config.stereo_mode_enabled ? 1.0f : 0.0f;
    }
    
    // Calculate fade progress
    double elapsed = synth_getCurrentTimeInSeconds() - g_stereo_fade_start_time;
    float progress = (float)(elapsed / STEREO_FADE_DURATION_S);
    
    if (progress >= 1.0f) {
        // Fade complete
        g_stereo_fade_active = 0;
        g_stereo_fade_factor = g_sp3ctra_config.stereo_mode_enabled ? 1.0f : 0.0f;
        return g_stereo_fade_factor;
    }
    
    // Smooth fade curve (quadratic easing)
    progress = progress * progress;
    
    // Calculate fade factor based on direction
    if (g_sp3ctra_config.stereo_mode_enabled) {
        // Fading in: 0.0 -> 1.0
        g_stereo_fade_factor = progress;
    } else {
        // Fading out: 1.0 -> 0.0
        g_stereo_fade_factor = 1.0f - progress;
    }
    
    return g_stereo_fade_factor;
}

/* ============================================================================
 * SYNTHESIS POLYPHONIC CALLBACKS
 * ============================================================================ */

void midi_cb_synth_polyphonic_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set mix level directly (thread-safe)
    setSynthPolyphonicMixLevel(param->value);
    
    log_info("POLYPHONIC", "Volume: %d%%", (int)(param->value * 100));
}

void midi_cb_synth_polyphonic_reverb_send(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set reverb send level for polyphonic synth
    setReverbSendPolyphonic(param->value);
    
    if (gAudioSystem && param->value > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
    }
    
    log_info("POLYPHONIC", "Reverb send: %d%%", (int)(param->value * 100));
}

void midi_cb_synth_polyphonic_lfo_vibrato(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_vibrato_rate(param->raw_value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "VIBRATO LFO SPEED: %.2f Hz", param->raw_value);
    }
}

void midi_cb_synth_polyphonic_lfo_vibrato_depth(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_vibrato_depth(param->raw_value);
    
    log_info("POLYPHONIC", "LFO Vibrato Depth: %.2f semitones", param->raw_value);
}

void midi_cb_synth_polyphonic_env_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_attack(param->raw_value);
    
    log_info("POLYPHONIC", "Volume ADSR Attack: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_env_decay(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_decay(param->raw_value);
    
    log_info("POLYPHONIC", "Volume ADSR Decay: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_env_sustain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_sustain(param->value);
    
    log_info("POLYPHONIC", "Volume ADSR Sustain: %.0f%%", param->value * 100.0f);
}

void midi_cb_synth_polyphonic_env_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_release(param->raw_value);
    
    log_info("POLYPHONIC", "Volume ADSR Release: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_note_on(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    // param->value contains normalized velocity (0.0 to 1.0)
    int velocity = (int)(param->value * 127.0f);
    
    synth_polyphonic_note_on(note_number, velocity);
    
    log_debug("MIDI", "Note On: %d (velocity %d)", note_number, velocity);
}

void midi_cb_synth_polyphonic_note_off(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    
    synth_polyphonic_note_off(note_number);
    
    log_debug("MIDI", "Note Off: %d", note_number);
}

void midi_cb_synth_polyphonic_filter_cutoff(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_cutoff(param->raw_value);
    
    log_info("POLYPHONIC", "Filter Cutoff: %.0f Hz", param->raw_value);
}

void midi_cb_synth_polyphonic_filter_env_depth(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_env_depth(param->raw_value);
    
    log_info("POLYPHONIC", "Filter Env Depth: %.0f Hz", param->raw_value);
}

void midi_cb_synth_polyphonic_filter_adsr_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_adsr_attack(param->raw_value);
    
    log_info("POLYPHONIC", "Filter ADSR Attack: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_filter_adsr_decay(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_adsr_decay(param->raw_value);
    
    log_info("POLYPHONIC", "Filter ADSR Decay: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_filter_adsr_sustain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_adsr_sustain(param->value);
    
    log_info("POLYPHONIC", "Filter ADSR Sustain: %.0f%%", param->value * 100.0f);
}

void midi_cb_synth_polyphonic_filter_adsr_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_filter_adsr_release(param->raw_value);
    
    log_info("POLYPHONIC", "Filter ADSR Release: %d ms", (int)(param->raw_value * 1000));
}

/* ============================================================================
 * SYNTHESIS PHOTOWAVE CALLBACKS
 * ============================================================================ */

void midi_cb_synth_photowave_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set mix level directly (thread-safe)
    setSynthPhotowaveMixLevel(param->value);
    
    log_info("PHOTOWAVE", "Volume: %d%%", (int)(param->value * 100));
}

void midi_cb_synth_photowave_reverb_send(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set reverb send level for photowave synth
    setReverbSendPhotowave(param->value);
    
    if (gAudioSystem && param->value > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
    }
    
    log_info("PHOTOWAVE", "Reverb send: %d%%", (int)(param->value * 100));
}

void midi_cb_synth_photowave_note_on(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    // param->value contains normalized velocity (0.0 to 1.0)
    int velocity = (int)(param->value * 127.0f);
    
    log_debug("PHOTOWAVE_DEBUG", "MIDI Callback: Note On received - note=%d, velocity=%d", note_number, velocity);
    
    synth_photowave_note_on(&g_photowave_state, (uint8_t)note_number, (uint8_t)velocity);
    
    log_debug("MIDI", "Photowave Note On: %d (velocity %d)", note_number, velocity);
}

void midi_cb_synth_photowave_note_off(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    
    log_debug("PHOTOWAVE_DEBUG", "MIDI Callback: Note Off received - note=%d", note_number);
    
    synth_photowave_note_off(&g_photowave_state, (uint8_t)note_number);
    
    log_debug("MIDI", "Photowave Note Off: %d", note_number);
}

void midi_cb_synth_photowave_modulation(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // CC1 (Modulation): Scan mode (0-42=L→R, 43-84=R→L, 85-127=Dual)
    synth_photowave_control_change(&g_photowave_state, 1, (uint8_t)(param->value * 127.0f));
    
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE MODULATION (Scan Mode): %d", (int)(param->value * 127));
    }
}

void midi_cb_synth_photowave_resonance(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // CC71 (Resonance): Blur amount (0-127 → 0.0-1.0)
    synth_photowave_control_change(&g_photowave_state, 71, (uint8_t)(param->value * 127.0f));
    
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE RESONANCE (Blur): %d%%", (int)(param->value * 100));
    }
}

void midi_cb_synth_photowave_brightness(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // CC74 (Brightness): Interpolation mode (0-63=Linear, 64-127=Cubic)
    synth_photowave_control_change(&g_photowave_state, 74, (uint8_t)(param->value * 127.0f));
    
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE BRIGHTNESS (Interp): %d", (int)(param->value * 127));
    }
}

void midi_cb_synth_photowave_pitch(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;

    // Photowave is now polyphonic and controlled via MIDI notes
    // This pitch CC callback is deprecated - use MIDI Note On/Off instead
    // Keeping function for backward compatibility but it does nothing
    
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE PITCH CC ignored (use MIDI notes for polyphonic control)");
    }
}

void midi_cb_synth_photowave_volume_env_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_volume_adsr_attack(param->raw_value);
    log_info("PHOTOWAVE", "ADSR Attack: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_photowave_volume_env_decay(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_volume_adsr_decay(param->raw_value);
    log_info("PHOTOWAVE", "ADSR Decay: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_photowave_volume_env_sustain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_volume_adsr_sustain(param->value);
    log_info("PHOTOWAVE", "ADSR Sustain: %.0f%%", param->value * 100.0f);
}

void midi_cb_synth_photowave_volume_env_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_volume_adsr_release(param->raw_value);
    log_info("PHOTOWAVE", "ADSR Release: %d ms", (int)(param->raw_value * 1000));
}

void midi_cb_synth_photowave_filter_env_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_adsr_attack(param->raw_value);
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE FILT ENV ATTACK: %d ms", (int)(param->raw_value * 1000));
    }
}

void midi_cb_synth_photowave_filter_env_decay(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_adsr_decay(param->raw_value);
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE FILT ENV DECAY: %d ms", (int)(param->raw_value * 1000));
    }
}

void midi_cb_synth_photowave_filter_env_sustain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_adsr_sustain(param->value);
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE FILT ENV SUSTAIN: %.0f%%", param->value * 100.0f);
    }
}

void midi_cb_synth_photowave_filter_env_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_adsr_release(param->raw_value);
    if (is_startup_verbose()) {
        log_info("MIDI", "PHOTOWAVE FILT ENV RELEASE: %d ms", (int)(param->raw_value * 1000));
    }
}

void midi_cb_synth_photowave_lfo_vibrato_rate(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_vibrato_rate(param->raw_value);
    log_info("PHOTOWAVE", "LFO Rate: %.2f Hz", param->raw_value);
}

void midi_cb_synth_photowave_lfo_vibrato_depth(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_vibrato_depth(param->raw_value);
    log_info("PHOTOWAVE", "LFO Depth: %.2f semitones", param->raw_value);
}

void midi_cb_synth_photowave_filter_cutoff(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_cutoff(param->raw_value);
    log_info("PHOTOWAVE", "Filter Cutoff: %.0f Hz", param->raw_value);
}

void midi_cb_synth_photowave_filter_env_depth(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    synth_photowave_set_filter_env_depth(param->raw_value);
    log_info("PHOTOWAVE", "Filter Env Depth: %.0f Hz", param->raw_value);
}

/* ============================================================================
 * SEQUENCER PLAYER CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_player_record_toggle(const MidiParameterValue *param, void *user_data) {
    log_debug("MIDI", "SEQUENCER CALLBACK CALLED: record_toggle");
    
    if (!g_image_sequencer) {
        log_error("MIDI", "g_image_sequencer is NULL");
        return;
    }
    
    if (!user_data) {
        log_error("MIDI", "user_data is NULL");
        return;
    }
    
    int player_id = *(int*)user_data;
    log_debug("MIDI", "Player ID: %d, button_pressed: %d", player_id, param->button_pressed);
    
    // Monostable behavior: press = start recording, release = stop recording
    if (param->button_pressed) {
        log_debug("MIDI", "Button pressed: Starting recording");
        image_sequencer_start_recording(g_image_sequencer, player_id);
    } else {
        log_debug("MIDI", "Button released: Stopping recording");
        image_sequencer_stop_recording(g_image_sequencer, player_id);
    }
}

void midi_cb_sequencer_player_play_stop(const MidiParameterValue *param, void *user_data) {
    log_debug("MIDI", "SEQUENCER CALLBACK CALLED: play_stop");
    
    if (!g_image_sequencer) {
        log_error("MIDI", "g_image_sequencer is NULL");
        return;
    }
    
    if (!user_data) {
        log_error("MIDI", "user_data is NULL");
        return;
    }
    
    int player_id = *(int*)user_data;
    log_debug("MIDI", "Player ID: %d, button_pressed: %d", player_id, param->button_pressed);
    
    // Monostable behavior: press = start playback, release = stop playback
    if (param->button_pressed) {
        log_debug("MIDI", "Button pressed: Starting playback");
        image_sequencer_start_playback(g_image_sequencer, player_id);
    } else {
        log_debug("MIDI", "Button released: Stopping playback");
        image_sequencer_stop_playback(g_image_sequencer, player_id);
    }
}

void midi_cb_sequencer_player_clear(const MidiParameterValue *param, void *user_data) {
    log_debug("MIDI", "SEQUENCER CALLBACK CALLED: clear");
    (void)param;
    
    if (!g_image_sequencer) {
        log_error("MIDI", "g_image_sequencer is NULL");
        return;
    }
    
    if (!user_data) {
        log_error("MIDI", "user_data is NULL");
        return;
    }
    
    int player_id = *(int*)user_data;
    log_debug("MIDI", "Player ID: %d", player_id);
    
    image_sequencer_clear_buffer(g_image_sequencer, player_id);
}

void midi_cb_sequencer_player_speed(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_speed(g_image_sequencer, player_id, param->raw_value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Speed %.2fx", player_id, param->raw_value);
    }
}

void midi_cb_sequencer_player_exposure(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_exposure(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Exposure %d%%", player_id, (int)(param->value * 100));
    }
}

void midi_cb_sequencer_player_brightness(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_brightness(g_image_sequencer, player_id, param->raw_value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Brightness %.0f%%", player_id, param->raw_value * 100);
    }
}

void midi_cb_sequencer_player_mix(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_player_mix(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Player mix %d%% (0%%=player, 100%%=mask)", player_id, (int)(param->value * 100));
    }
}

void midi_cb_sequencer_player_offset(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    // Convert 0.0-1.0 to frame offset (will be clamped to recorded_frames)
    int offset_frames = (int)(param->value * 5000); // Max 5000 frames
    image_sequencer_set_offset(g_image_sequencer, player_id, offset_frames);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Offset %d frames", player_id, offset_frames);
    }
}

void midi_cb_sequencer_player_attack(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_attack(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Attack %.0f%%", player_id, param->value * 100.0f);
    }
}

void midi_cb_sequencer_player_decay(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_decay(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Decay %.0f%%", player_id, param->value * 100.0f);
    }
}

void midi_cb_sequencer_player_sustain(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_sustain(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Sustain %.0f%%", player_id, param->value * 100.0f);
    }
}

void midi_cb_sequencer_player_release(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_release(g_image_sequencer, player_id, param->value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQ Player %d: Release %.0f%%", player_id, param->value * 100.0f);
    }
}

void midi_cb_sequencer_player_loop_mode(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    int mode = (int)param->raw_value;
    
    const char *modes[] = {"SIMPLE", "PINGPONG", "ONESHOT"};
    if (mode >= 0 && mode <= 2) {
        image_sequencer_set_loop_mode(g_image_sequencer, player_id, (LoopMode)mode);
        if (is_startup_verbose()) {
            log_info("MIDI", "SEQ Player %d: Loop %s", player_id, modes[mode]);
        }
    }
}

void midi_cb_sequencer_player_playback_direction(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    int dir = (int)param->raw_value;
    
    const char *dirs[] = {"FORWARD", "REVERSE"};
    image_sequencer_set_playback_direction(g_image_sequencer, player_id, dir == 0 ? 1 : -1);
    
    if (is_startup_verbose() && dir >= 0 && dir <= 1) {
        log_info("MIDI", "SEQ Player %d: Direction %s", player_id, dirs[dir]);
    }
}

/* ============================================================================
 * SEQUENCER GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_blend_mode(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (!g_image_sequencer) return;
    
    const char *modes[] = {"MIX", "CROSSFADE", "OVERLAY", "MASK"};
    int mode = (int)param->raw_value;
    
    if (mode >= 0 && mode <= 3) {
        image_sequencer_set_blend_mode(g_image_sequencer, (BlendMode)mode);
        if (is_startup_verbose()) {
            log_info("MIDI", "SEQUENCER: Blend mode %s", modes[mode]);
        }
    }
}

void midi_cb_sequencer_master_tempo(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (!g_image_sequencer) return;
    
    image_sequencer_set_bpm(g_image_sequencer, param->raw_value);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SEQUENCER: Tempo %.0f BPM", param->raw_value);
    }
}

void midi_cb_sequencer_quantize_res(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement quantization (will be part of MIDI sync feature)
    const char *res[] = {"QUARTER", "EIGHTH", "SIXTEENTH", "BAR"};
    int r = (int)param->raw_value;
    if (r >= 0 && r <= 3 && is_startup_verbose()) {
        log_info("MIDI", "SEQUENCER: Quantize %s", res[r]);
    }
}

/* ============================================================================
 * SYSTEM CALLBACKS
 * ============================================================================ */

void midi_cb_system_freeze(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;
    
    pthread_mutex_lock(&g_synth_data_freeze_mutex);
    g_is_synth_data_frozen = 1;
    g_is_synth_data_fading_out = 0;
    pthread_mutex_unlock(&g_synth_data_freeze_mutex);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SYNTH DATA FREEZE: ON");
    }
}

void midi_cb_system_resume(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;
    
    pthread_mutex_lock(&g_synth_data_freeze_mutex);
    if (g_is_synth_data_frozen && !g_is_synth_data_fading_out) {
        g_is_synth_data_fading_out = 1;
    }
    pthread_mutex_unlock(&g_synth_data_freeze_mutex);
    
    if (is_startup_verbose()) {
        log_info("MIDI", "SYNTH DATA RESUME: Initiating fade out");
    }
}

/* ============================================================================
 * REGISTRATION HELPERS
 * ============================================================================ */

void midi_callbacks_register_audio(void) {
    midi_mapping_register_callback("audio_global_master_volume", midi_cb_audio_master_volume, NULL);
    midi_mapping_register_callback("audio_global_reverb_mix", midi_cb_audio_reverb_mix, NULL);
    midi_mapping_register_callback("audio_global_reverb_size", midi_cb_audio_reverb_size, NULL);
    midi_mapping_register_callback("audio_global_reverb_damp", midi_cb_audio_reverb_damp, NULL);
    midi_mapping_register_callback("audio_global_reverb_width", midi_cb_audio_reverb_width, NULL);
    midi_mapping_register_callback("audio_global_eq_low_gain", midi_cb_audio_eq_low_gain, NULL);
    midi_mapping_register_callback("audio_global_eq_mid_gain", midi_cb_audio_eq_mid_gain, NULL);
    midi_mapping_register_callback("audio_global_eq_high_gain", midi_cb_audio_eq_high_gain, NULL);
    midi_mapping_register_callback("audio_global_eq_mid_freq", midi_cb_audio_eq_mid_freq, NULL);
    
    log_info("MIDI", "Callbacks: Audio registered");
}

void midi_callbacks_register_synth_additive(void) {
    midi_mapping_register_callback("synth_additive_volume", midi_cb_synth_additive_volume, NULL);
    midi_mapping_register_callback("synth_additive_reverb_send", midi_cb_synth_additive_reverb_send, NULL);
    
    // Envelope parameters
    midi_mapping_register_callback("synth_additive_envelope_tau_up_base_ms", midi_cb_synth_additive_tau_up, NULL);
    midi_mapping_register_callback("synth_additive_envelope_tau_down_base_ms", midi_cb_synth_additive_tau_down, NULL);
    midi_mapping_register_callback("synth_additive_envelope_decay_freq_ref_hz", midi_cb_synth_additive_decay_freq_ref, NULL);
    midi_mapping_register_callback("synth_additive_envelope_decay_freq_beta", midi_cb_synth_additive_decay_freq_beta, NULL);
    
    // Stereo toggle
    midi_mapping_register_callback("synth_additive_stereo_mode_enabled", midi_cb_synth_additive_stereo_toggle, NULL);
    
    log_info("MIDI", "Callbacks: Additive synth registered (with envelope & stereo controls)");
}

void midi_callbacks_register_synth_polyphonic(void) {
    midi_mapping_register_callback("synth_polyphonic_volume", midi_cb_synth_polyphonic_volume, NULL);
    midi_mapping_register_callback("synth_polyphonic_reverb_send", midi_cb_synth_polyphonic_reverb_send, NULL);
    midi_mapping_register_callback("synth_polyphonic_note_on", midi_cb_synth_polyphonic_note_on, NULL);
    midi_mapping_register_callback("synth_polyphonic_note_off", midi_cb_synth_polyphonic_note_off, NULL);
    
    // Volume ADSR Envelope
    midi_mapping_register_callback("synth_polyphonic_volume_env_attack", midi_cb_synth_polyphonic_env_attack, NULL);
    midi_mapping_register_callback("synth_polyphonic_volume_env_decay", midi_cb_synth_polyphonic_env_decay, NULL);
    midi_mapping_register_callback("synth_polyphonic_volume_env_sustain", midi_cb_synth_polyphonic_env_sustain, NULL);
    midi_mapping_register_callback("synth_polyphonic_volume_env_release", midi_cb_synth_polyphonic_env_release, NULL);
    
    // Filter ADSR Envelope
    midi_mapping_register_callback("synth_polyphonic_filter_env_attack", midi_cb_synth_polyphonic_filter_adsr_attack, NULL);
    midi_mapping_register_callback("synth_polyphonic_filter_env_decay", midi_cb_synth_polyphonic_filter_adsr_decay, NULL);
    midi_mapping_register_callback("synth_polyphonic_filter_env_sustain", midi_cb_synth_polyphonic_filter_adsr_sustain, NULL);
    midi_mapping_register_callback("synth_polyphonic_filter_env_release", midi_cb_synth_polyphonic_filter_adsr_release, NULL);
    
    // LFO Vibrato
    midi_mapping_register_callback("synth_polyphonic_lfo_vibrato_rate", midi_cb_synth_polyphonic_lfo_vibrato, NULL);
    midi_mapping_register_callback("synth_polyphonic_lfo_vibrato_depth", midi_cb_synth_polyphonic_lfo_vibrato_depth, NULL);
    
    // Filter Parameters
    midi_mapping_register_callback("synth_polyphonic_filter_cutoff", midi_cb_synth_polyphonic_filter_cutoff, NULL);
    midi_mapping_register_callback("synth_polyphonic_filter_env_depth", midi_cb_synth_polyphonic_filter_env_depth, NULL);
    
    log_info("MIDI", "Callbacks: Polyphonic synth registered (with filter & ADSR controls)");
}

void midi_callbacks_register_synth_photowave(void) {
    midi_mapping_register_callback("synth_photowave_volume", midi_cb_synth_photowave_volume, NULL);
    midi_mapping_register_callback("synth_photowave_reverb_send", midi_cb_synth_photowave_reverb_send, NULL);
    midi_mapping_register_callback("synth_photowave_note_on", midi_cb_synth_photowave_note_on, NULL);
    midi_mapping_register_callback("synth_photowave_note_off", midi_cb_synth_photowave_note_off, NULL);
    midi_mapping_register_callback("synth_photowave_pitch", midi_cb_synth_photowave_pitch, NULL);
    midi_mapping_register_callback("synth_photowave_modulation", midi_cb_synth_photowave_modulation, NULL);
    midi_mapping_register_callback("synth_photowave_resonance", midi_cb_synth_photowave_resonance, NULL);
    midi_mapping_register_callback("synth_photowave_brightness", midi_cb_synth_photowave_brightness, NULL);
    
    // ADSR Volume Envelope
    midi_mapping_register_callback("synth_photowave_volume_env_attack", midi_cb_synth_photowave_volume_env_attack, NULL);
    midi_mapping_register_callback("synth_photowave_volume_env_decay", midi_cb_synth_photowave_volume_env_decay, NULL);
    midi_mapping_register_callback("synth_photowave_volume_env_sustain", midi_cb_synth_photowave_volume_env_sustain, NULL);
    midi_mapping_register_callback("synth_photowave_volume_env_release", midi_cb_synth_photowave_volume_env_release, NULL);
    
    // ADSR Filter Envelope
    midi_mapping_register_callback("synth_photowave_filter_env_attack", midi_cb_synth_photowave_filter_env_attack, NULL);
    midi_mapping_register_callback("synth_photowave_filter_env_decay", midi_cb_synth_photowave_filter_env_decay, NULL);
    midi_mapping_register_callback("synth_photowave_filter_env_sustain", midi_cb_synth_photowave_filter_env_sustain, NULL);
    midi_mapping_register_callback("synth_photowave_filter_env_release", midi_cb_synth_photowave_filter_env_release, NULL);
    
    // LFO Vibrato
    midi_mapping_register_callback("synth_photowave_lfo_vibrato_rate", midi_cb_synth_photowave_lfo_vibrato_rate, NULL);
    midi_mapping_register_callback("synth_photowave_lfo_vibrato_depth", midi_cb_synth_photowave_lfo_vibrato_depth, NULL);
    
    // Filter Parameters
    midi_mapping_register_callback("synth_photowave_filter_cutoff", midi_cb_synth_photowave_filter_cutoff, NULL);
    midi_mapping_register_callback("synth_photowave_filter_env_depth", midi_cb_synth_photowave_filter_env_depth, NULL);
    
    log_info("MIDI", "Callbacks: Photowave synth registered (with reverb send, ADSR/LFO/Filter)");
}

void midi_callbacks_register_sequencer(void *sequencer_instance) {
    (void)sequencer_instance;
    
    if (!g_image_sequencer) {
        log_warning("MIDI", "Callbacks: Sequencer not initialized, skipping registration");
        return;
    }
    
    // Register global sequencer controls
    midi_mapping_register_callback("sequencer_global_blend_mode", midi_cb_sequencer_blend_mode, NULL);
    midi_mapping_register_callback("sequencer_global_master_tempo", midi_cb_sequencer_master_tempo, NULL);
    midi_mapping_register_callback("sequencer_global_quantize_res", midi_cb_sequencer_quantize_res, NULL);
    
    // Register player-specific controls (static allocation for player IDs)
    static int player_ids[5] = {0, 1, 2, 3, 4};
    
    for (int i = 0; i < 5; i++) {
        char param_name[64];
        
        // Record toggle
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_record_toggle", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_record_toggle, &player_ids[i]);
        
        // Play/Stop
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_play_stop", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_play_stop, &player_ids[i]);
        
        // Clear buffer
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_clear", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_clear, &player_ids[i]);
        
        // Speed
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_speed", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_speed, &player_ids[i]);
        
        // Exposure
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_exposure", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_exposure, &player_ids[i]);
        
        // Brightness
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_brightness", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_brightness, &player_ids[i]);
        
        // Player mix
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_player_mix", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_mix, &player_ids[i]);
        
        // Offset
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_offset", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_offset, &player_ids[i]);
        
        // ADSR Envelope
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_env_attack", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_attack, &player_ids[i]);
        
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_env_decay", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_decay, &player_ids[i]);
        
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_env_sustain", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_sustain, &player_ids[i]);
        
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_env_release", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_release, &player_ids[i]);
        
        // Loop mode
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_loop_mode", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_loop_mode, &player_ids[i]);
        
        // Playback direction
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_playback_direction", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_playback_direction, &player_ids[i]);
    }
    
    log_info("MIDI", "Callbacks: Sequencer registered (5 players + global controls)");
}

void midi_callbacks_register_system(void) {
    midi_mapping_register_callback("system_freeze", midi_cb_system_freeze, NULL);
    midi_mapping_register_callback("system_resume", midi_cb_system_resume, NULL);
    
    log_info("MIDI", "Callbacks: System registered");
}

void midi_callbacks_register_all(void) {
    midi_callbacks_register_audio();
    midi_callbacks_register_synth_additive();
    midi_callbacks_register_synth_polyphonic();
    midi_callbacks_register_synth_photowave();
    midi_callbacks_register_sequencer(NULL);
    midi_callbacks_register_system();
    
    log_info("MIDI", "Callbacks: All registered");
}
