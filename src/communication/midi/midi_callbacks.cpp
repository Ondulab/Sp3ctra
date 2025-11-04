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
#include "../../audio/rtaudio/audio_c_api.h"  // For setSynthAdditiveMixLevel, setSynthPolyphonicMixLevel
#include "../../audio/effects/three_band_eq.h"
#include "../../synthesis/additive/synth_additive.h"
#include "../../synthesis/polyphonic/synth_polyphonic.h"
#include "../../processing/image_sequencer.h"
#include <stdio.h>
#include <pthread.h>

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
        
        printf("\033[1;37mVOLUME: %d%%\033[0m\n", (int)(param->value * 100));
    }
}

void midi_cb_audio_reverb_mix(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbMix(param->value);
        
        printf("\033[1;36mREVERB MIX: %d%%\033[0m\n", (int)(param->value * 100));
    }
}

void midi_cb_audio_reverb_size(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbRoomSize(param->value);
        
        printf("\033[1;36mREVERB SIZE: %.2f\033[0m\n", param->value);
    }
}

void midi_cb_audio_reverb_damp(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbDamping(param->value);
        
        printf("\033[1;36mREVERB DAMP: %.2f\033[0m\n", param->value);
    }
}

void midi_cb_audio_reverb_width(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbWidth(param->value);
        
        printf("\033[1;36mREVERB WIDTH: %.2f\033[0m\n", param->value);
    }
}

void midi_cb_audio_eq_low_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setLowGain(param->raw_value);
        
        printf("\033[1;32mEQ LOW GAIN: %.1f dB\033[0m\n", param->raw_value);
    }
}

void midi_cb_audio_eq_mid_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setMidGain(param->raw_value);
        
        printf("\033[1;36mEQ MID GAIN: %.1f dB\033[0m\n", param->raw_value);
    }
}

void midi_cb_audio_eq_high_gain(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setHighGain(param->raw_value);
        
        printf("\033[1;35mEQ HIGH GAIN: %.1f dB\033[0m\n", param->raw_value);
    }
}

void midi_cb_audio_eq_mid_freq(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gEqualizer) {
        if (!gEqualizer->isEnabled()) {
            gEqualizer->setEnabled(true);
        }
        gEqualizer->setMidFrequency(param->raw_value);
        
        printf("\033[1;33mEQ MID FREQ: %.0f Hz\033[0m\n", param->raw_value);
    }
}

/* ============================================================================
 * SYNTHESIS ADDITIVE CALLBACKS
 * ============================================================================ */

void midi_cb_synth_additive_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set mix level directly (thread-safe)
    setSynthAdditiveMixLevel(param->value);
    
    printf("\033[1;37mADDITIVE SYNTH VOLUME: %d%%\033[0m\n", (int)(param->value * 100));
}

void midi_cb_synth_additive_reverb_send(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem && param->value > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
    }
    
    printf("\033[1;36mADDITIVE REVERB SEND: %d%%\033[0m\n", (int)(param->value * 100));
}

/* ============================================================================
 * SYNTHESIS POLYPHONIC CALLBACKS
 * ============================================================================ */

void midi_cb_synth_polyphonic_volume(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Set mix level directly (thread-safe)
    setSynthPolyphonicMixLevel(param->value);
    
    printf("\033[1;37mPOLYPHONIC SYNTH VOLUME: %d%%\033[0m\n", (int)(param->value * 100));
}

void midi_cb_synth_polyphonic_reverb_send(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem && param->value > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
    }
    
    printf("\033[1;36mPOLYPHONIC REVERB SEND: %d%%\033[0m\n", (int)(param->value * 100));
}

void midi_cb_synth_polyphonic_lfo_vibrato(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_vibrato_rate(param->raw_value);
    
    printf("\033[1;35mVIBRATO LFO SPEED: %.2f Hz\033[0m\n", param->raw_value);
}

void midi_cb_synth_polyphonic_env_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_attack(param->raw_value);
    
    printf("\033[1;33mPOLYPHONIC ENV ATTACK: %d ms\033[0m\n", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_env_decay(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_decay(param->raw_value);
    
    printf("\033[1;33mPOLYPHONIC ENV DECAY: %d ms\033[0m\n", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_env_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    synth_polyphonic_set_volume_adsr_release(param->raw_value);
    
    printf("\033[1;33mPOLYPHONIC ENV RELEASE: %d ms\033[0m\n", (int)(param->raw_value * 1000));
}

void midi_cb_synth_polyphonic_note_on(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    // param->value contains normalized velocity (0.0 to 1.0)
    int velocity = (int)(param->value * 127.0f);
    
    synth_polyphonic_note_on(note_number, velocity);
    
    printf("\033[1;32mMIDI Note On: %d (velocity %d)\033[0m\n", note_number, velocity);
}

void midi_cb_synth_polyphonic_note_off(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // Note handling is special - raw_value contains note number
    int note_number = (int)param->raw_value;
    
    synth_polyphonic_note_off(note_number);
    
    printf("\033[1;31mMIDI Note Off: %d\033[0m\n", note_number);
}

/* ============================================================================
 * SEQUENCER PLAYER CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_player_record_toggle(const MidiParameterValue *param, void *user_data) {
    printf("\033[1;31m[DEBUG] SEQUENCER CALLBACK CALLED: record_toggle\033[0m\n");
    (void)param;
    
    if (!g_image_sequencer) {
        printf("\033[1;31m[DEBUG] ERROR: g_image_sequencer is NULL!\033[0m\n");
        return;
    }
    
    if (!user_data) {
        printf("\033[1;31m[DEBUG] ERROR: user_data is NULL!\033[0m\n");
        return;
    }
    
    int player_id = *(int*)user_data;
    printf("\033[1;32m[DEBUG] Player ID: %d\033[0m\n", player_id);
    
    PlayerState state = image_sequencer_get_player_state(g_image_sequencer, player_id);
    printf("\033[1;32m[DEBUG] Current state: %d\033[0m\n", state);
    
    if (state == PLAYER_STATE_RECORDING) {
        printf("\033[1;33m[DEBUG] Stopping recording...\033[0m\n");
        image_sequencer_stop_recording(g_image_sequencer, player_id);
    } else {
        printf("\033[1;33m[DEBUG] Starting recording...\033[0m\n");
        image_sequencer_start_recording(g_image_sequencer, player_id);
    }
}

void midi_cb_sequencer_player_play_stop(const MidiParameterValue *param, void *user_data) {
    printf("\033[1;31m[DEBUG] SEQUENCER CALLBACK CALLED: play_stop\033[0m\n");
    (void)param;
    
    if (!g_image_sequencer) {
        printf("\033[1;31m[DEBUG] ERROR: g_image_sequencer is NULL!\033[0m\n");
        return;
    }
    
    if (!user_data) {
        printf("\033[1;31m[DEBUG] ERROR: user_data is NULL!\033[0m\n");
        return;
    }
    
    int player_id = *(int*)user_data;
    printf("\033[1;32m[DEBUG] Player ID: %d\033[0m\n", player_id);
    
    PlayerState state = image_sequencer_get_player_state(g_image_sequencer, player_id);
    printf("\033[1;32m[DEBUG] Current state: %d, toggling playback...\033[0m\n", state);
    
    image_sequencer_toggle_playback(g_image_sequencer, player_id);
}

void midi_cb_sequencer_player_mute_toggle(const MidiParameterValue *param, void *user_data) {
    printf("\033[1;31m[DEBUG] SEQUENCER CALLBACK CALLED: mute_toggle\033[0m\n");
    (void)param;
    
    if (!g_image_sequencer) {
        printf("\033[1;31m[DEBUG] ERROR: g_image_sequencer is NULL!\033[0m\n");
        return;
    }
    
    if (!user_data) {
        printf("\033[1;31m[DEBUG] ERROR: user_data is NULL!\033[0m\n");
        return;
    }
    
    int player_id = *(int*)user_data;
    printf("\033[1;32m[DEBUG] Player ID: %d\033[0m\n", player_id);
    
    PlayerState state = image_sequencer_get_player_state(g_image_sequencer, player_id);
    printf("\033[1;32m[DEBUG] Current state: %d, toggling mute...\033[0m\n", state);
    
    image_sequencer_toggle_mute(g_image_sequencer, player_id);
}

void midi_cb_sequencer_player_speed(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_speed(g_image_sequencer, player_id, param->raw_value);
    
    printf("\033[1;33mSEQ Player %d: Speed %.2fx\033[0m\n", player_id, param->raw_value);
}

void midi_cb_sequencer_player_blend_level(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_blend_level(g_image_sequencer, player_id, param->value);
    
    printf("\033[1;33mSEQ Player %d: Blend %d%%\033[0m\n", player_id, (int)(param->value * 100));
}

void midi_cb_sequencer_player_offset(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    // Convert 0.0-1.0 to frame offset (will be clamped to recorded_frames)
    int offset_frames = (int)(param->value * 5000); // Max 5000 frames
    image_sequencer_set_offset(g_image_sequencer, player_id, offset_frames);
    
    printf("\033[1;33mSEQ Player %d: Offset %d frames\033[0m\n", player_id, offset_frames);
}

void midi_cb_sequencer_player_attack(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_attack(g_image_sequencer, player_id, param->value);
    
    printf("\033[1;33mSEQ Player %d: Attack %.0f%%\033[0m\n", player_id, param->value * 100.0f);
}

void midi_cb_sequencer_player_decay(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_decay(g_image_sequencer, player_id, param->value);
    
    printf("\033[1;33mSEQ Player %d: Decay %.0f%%\033[0m\n", player_id, param->value * 100.0f);
}

void midi_cb_sequencer_player_sustain(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_sustain(g_image_sequencer, player_id, param->value);
    
    printf("\033[1;33mSEQ Player %d: Sustain %.0f%%\033[0m\n", player_id, param->value * 100.0f);
}

void midi_cb_sequencer_player_release(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    image_sequencer_set_release(g_image_sequencer, player_id, param->value);
    
    printf("\033[1;33mSEQ Player %d: Release %.0f%%\033[0m\n", player_id, param->value * 100.0f);
}

void midi_cb_sequencer_player_loop_mode(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    int mode = (int)param->raw_value;
    
    const char *modes[] = {"SIMPLE", "PINGPONG", "ONESHOT"};
    if (mode >= 0 && mode <= 2) {
        image_sequencer_set_loop_mode(g_image_sequencer, player_id, (LoopMode)mode);
        printf("\033[1;35mSEQ Player %d: Loop %s\033[0m\n", player_id, modes[mode]);
    }
}

void midi_cb_sequencer_player_playback_direction(const MidiParameterValue *param, void *user_data) {
    if (!g_image_sequencer || !user_data) return;
    
    int player_id = *(int*)user_data;
    int dir = (int)param->raw_value;
    
    const char *dirs[] = {"FORWARD", "REVERSE"};
    image_sequencer_set_playback_direction(g_image_sequencer, player_id, dir == 0 ? 1 : -1);
    
    if (dir >= 0 && dir <= 1) {
        printf("\033[1;35mSEQ Player %d: Direction %s\033[0m\n", player_id, dirs[dir]);
    }
}

/* ============================================================================
 * SEQUENCER GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_live_mix_level(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (!g_image_sequencer) return;
    
    image_sequencer_set_live_mix_level(g_image_sequencer, param->value);
    
    printf("\033[1;36mSEQUENCER: Live mix %d%%\033[0m\n", (int)(param->value * 100));
}

void midi_cb_sequencer_blend_mode(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (!g_image_sequencer) return;
    
    const char *modes[] = {"MIX", "CROSSFADE", "OVERLAY", "MASK"};
    int mode = (int)param->raw_value;
    
    if (mode >= 0 && mode <= 3) {
        image_sequencer_set_blend_mode(g_image_sequencer, (BlendMode)mode);
        printf("\033[1;36mSEQUENCER: Blend mode %s\033[0m\n", modes[mode]);
    }
}

void midi_cb_sequencer_master_tempo(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (!g_image_sequencer) return;
    
    image_sequencer_set_bpm(g_image_sequencer, param->raw_value);
    
    printf("\033[1;36mSEQUENCER: Tempo %.0f BPM\033[0m\n", param->raw_value);
}

void midi_cb_sequencer_quantize_res(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement quantization (will be part of MIDI sync feature)
    const char *res[] = {"QUARTER", "EIGHTH", "SIXTEENTH", "BAR"};
    int r = (int)param->raw_value;
    if (r >= 0 && r <= 3) {
        printf("\033[1;36mSEQUENCER: Quantize %s\033[0m\n", res[r]);
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
    
    printf("\033[1;34mSYNTH DATA FREEZE: ON\033[0m\n");
}

void midi_cb_system_resume(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;
    
    pthread_mutex_lock(&g_synth_data_freeze_mutex);
    if (g_is_synth_data_frozen && !g_is_synth_data_fading_out) {
        g_is_synth_data_fading_out = 1;
    }
    pthread_mutex_unlock(&g_synth_data_freeze_mutex);
    
    printf("\033[1;34mSYNTH DATA RESUME: Initiating fade out\033[0m\n");
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
    
    printf("MIDI Callbacks: Audio registered\n");
}

void midi_callbacks_register_synth_additive(void) {
    midi_mapping_register_callback("synth_additive_volume", midi_cb_synth_additive_volume, NULL);
    midi_mapping_register_callback("synth_additive_reverb_send", midi_cb_synth_additive_reverb_send, NULL);
    
    printf("MIDI Callbacks: Additive synth registered\n");
}

void midi_callbacks_register_synth_polyphonic(void) {
    midi_mapping_register_callback("synth_polyphonic_volume", midi_cb_synth_polyphonic_volume, NULL);
    midi_mapping_register_callback("synth_polyphonic_reverb_send", midi_cb_synth_polyphonic_reverb_send, NULL);
    midi_mapping_register_callback("synth_polyphonic_lfo_vibrato", midi_cb_synth_polyphonic_lfo_vibrato, NULL);
    midi_mapping_register_callback("synth_polyphonic_env_attack", midi_cb_synth_polyphonic_env_attack, NULL);
    midi_mapping_register_callback("synth_polyphonic_env_decay", midi_cb_synth_polyphonic_env_decay, NULL);
    midi_mapping_register_callback("synth_polyphonic_env_release", midi_cb_synth_polyphonic_env_release, NULL);
    midi_mapping_register_callback("synth_polyphonic_note_on", midi_cb_synth_polyphonic_note_on, NULL);
    midi_mapping_register_callback("synth_polyphonic_note_off", midi_cb_synth_polyphonic_note_off, NULL);
    
    printf("MIDI Callbacks: Polyphonic synth registered\n");
}

void midi_callbacks_register_sequencer(void *sequencer_instance) {
    (void)sequencer_instance;
    
    if (!g_image_sequencer) {
        printf("MIDI Callbacks: Sequencer not initialized, skipping registration\n");
        return;
    }
    
    // Register global sequencer controls
    midi_mapping_register_callback("sequencer_global_live_mix_level", midi_cb_sequencer_live_mix_level, NULL);
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
        
        // Mute toggle
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_mute_toggle", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_mute_toggle, &player_ids[i]);
        
        // Speed
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_speed", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_speed, &player_ids[i]);
        
        // Blend level
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_blend_level", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_blend_level, &player_ids[i]);
        
        // Offset
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_offset", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_offset, &player_ids[i]);
        
        // Attack
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_attack", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_attack, &player_ids[i]);
        
        // Decay
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_decay", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_decay, &player_ids[i]);
        
        // Sustain
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_sustain", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_sustain, &player_ids[i]);
        
        // Release
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_release", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_release, &player_ids[i]);
        
        // Loop mode
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_loop_mode", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_loop_mode, &player_ids[i]);
        
        // Playback direction
        snprintf(param_name, sizeof(param_name), "sequencer_player_%d_playback_direction", i + 1);
        midi_mapping_register_callback(param_name, midi_cb_sequencer_player_playback_direction, &player_ids[i]);
    }
    
    printf("MIDI Callbacks: Sequencer registered (5 players + global controls)\n");
}

void midi_callbacks_register_system(void) {
    midi_mapping_register_callback("system_freeze", midi_cb_system_freeze, NULL);
    midi_mapping_register_callback("system_resume", midi_cb_system_resume, NULL);
    
    printf("MIDI Callbacks: System registered\n");
}

void midi_callbacks_register_all(void) {
    midi_callbacks_register_audio();
    midi_callbacks_register_synth_additive();
    midi_callbacks_register_synth_polyphonic();
    midi_callbacks_register_sequencer(NULL);
    midi_callbacks_register_system();
    
    printf("MIDI Callbacks: All registered\n");
}
