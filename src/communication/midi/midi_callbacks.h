/*
 * midi_callbacks.h
 *
 * MIDI Callback Functions - Unified System
 * Centralized callbacks for all MIDI-controllable parameters
 *
 * Created: 30/10/2025
 * Author: Sp3ctra Team
 */

#ifndef MIDI_CALLBACKS_H
#define MIDI_CALLBACKS_H

#include "midi_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * AUDIO GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_audio_master_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_mix(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_size(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_damp(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_width(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_low_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_mid_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_high_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_mid_freq(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYNTHESIS LUXSTRAL CALLBACKS
 * ============================================================================ */

void midi_cb_synth_luxstral_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxstral_reverb_send(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYNTHESIS LUXSYNTH CALLBACKS
 * ============================================================================ */

void midi_cb_synth_luxsynth_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_reverb_send(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_lfo_vibrato(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_lfo_vibrato_depth(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_sustain(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_release(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_note_on(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_note_off(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_cutoff(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_env_depth(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_adsr_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_adsr_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_adsr_sustain(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_filter_adsr_release(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYNTHESIS LUXWAVE CALLBACKS
 * ============================================================================ */

void midi_cb_synth_luxwave_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_reverb_send(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_note_on(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_note_off(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_pitch(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_modulation(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_resonance(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_brightness(const MidiParameterValue *param, void *user_data);

// ADSR Volume Envelope
void midi_cb_synth_luxwave_volume_env_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_volume_env_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_volume_env_sustain(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_volume_env_release(const MidiParameterValue *param, void *user_data);

// ADSR Filter Envelope
void midi_cb_synth_luxwave_filter_env_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_filter_env_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_filter_env_sustain(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_filter_env_release(const MidiParameterValue *param, void *user_data);

// LFO Vibrato
void midi_cb_synth_luxwave_lfo_vibrato_rate(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_lfo_vibrato_depth(const MidiParameterValue *param, void *user_data);

// Filter Parameters
void midi_cb_synth_luxwave_filter_cutoff(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxwave_filter_env_depth(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SEQUENCER PLAYER CALLBACKS (generic, player ID in user_data)
 * ============================================================================ */

void midi_cb_sequencer_player_record_toggle(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_play_stop(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_mute_toggle(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_speed(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_exposure(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_offset(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_sustain(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_release(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_loop_mode(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_playback_direction(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SEQUENCER GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_live_mix_level(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_blend_mode(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_master_tempo(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_quantize_res(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * DISPLAY CALLBACKS
 * ============================================================================ */

void midi_cb_display_orientation(const MidiParameterValue *param, void *user_data);
void midi_cb_display_udp_scroll_speed(const MidiParameterValue *param, void *user_data);
void midi_cb_display_accel_x_scroll_speed(const MidiParameterValue *param, void *user_data);
void midi_cb_display_accel_y_offset(const MidiParameterValue *param, void *user_data);
void midi_cb_display_initial_line_position(const MidiParameterValue *param, void *user_data);
void midi_cb_display_line_thickness(const MidiParameterValue *param, void *user_data);
void midi_cb_display_transition_time_ms(const MidiParameterValue *param, void *user_data);
void midi_cb_display_accel_sensitivity(const MidiParameterValue *param, void *user_data);
void midi_cb_display_fade_strength(const MidiParameterValue *param, void *user_data);
void midi_cb_display_line_persistence(const MidiParameterValue *param, void *user_data);
void midi_cb_display_zoom(const MidiParameterValue *param, void *user_data);
void midi_cb_display_history_buffer_size(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYSTEM CALLBACKS
 * ============================================================================ */

void midi_cb_system_freeze(const MidiParameterValue *param, void *user_data);
void midi_cb_system_resume(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * REGISTRATION HELPERS
 * ============================================================================ */

/**
 * Register all audio global callbacks
 * Should be called after audio system initialization
 */
void midi_callbacks_register_audio(void);

/**
 * Register all additive synthesis callbacks
 * Should be called after additive synth initialization
 */
void midi_callbacks_register_synth_luxstral(void);

/**
 * Register all polyphonic synthesis callbacks
 * Should be called after polyphonic synth initialization
 */
void midi_callbacks_register_synth_luxsynth(void);

/**
 * Register all photowave synthesis callbacks
 * Should be called after photowave synth initialization
 */
void midi_callbacks_register_synth_luxwave(void);

/**
 * Register all sequencer callbacks
 * @param sequencer_instance Pointer to sequencer instance (for user_data)
 */
void midi_callbacks_register_sequencer(void *sequencer_instance);

/**
 * Register all display callbacks
 * Should be called after display system initialization
 */
void midi_callbacks_register_display(void);

/**
 * Register all system callbacks
 */
void midi_callbacks_register_system(void);

/**
 * Register all callbacks at once (convenience function)
 * Call this after all subsystems are initialized
 */
void midi_callbacks_register_all(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CALLBACKS_H */
