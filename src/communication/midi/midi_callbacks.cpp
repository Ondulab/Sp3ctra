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
#include <stdio.h>
#include <pthread.h>

/* External declarations for global objects */
extern AudioSystem *gAudioSystem;
extern ThreeBandEQ *gEqualizer;
extern MidiController *gMidiController;

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
        // TODO: Implement setReverbSize method in AudioSystem
        printf("\033[1;36mREVERB SIZE: %.2f\033[0m\n", param->value);
    }
}

void midi_cb_audio_reverb_damp(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        // TODO: Implement setReverbDamp method in AudioSystem
        printf("\033[1;36mREVERB DAMP: %.2f\033[0m\n", param->value);
    }
}

void midi_cb_audio_reverb_width(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    if (gAudioSystem) {
        if (!gAudioSystem->isReverbEnabled()) {
            gAudioSystem->enableReverb(true);
        }
        // TODO: Implement setReverbWidth method in AudioSystem
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
    (void)param;
    (void)user_data;
    
    // TODO: Implement sequencer record toggle
    printf("Sequencer: Record toggle\n");
}

void midi_cb_sequencer_player_play_stop(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;
    
    // TODO: Implement sequencer play/stop
    printf("Sequencer: Play/Stop\n");
}

void midi_cb_sequencer_player_mute_toggle(const MidiParameterValue *param, void *user_data) {
    (void)param;
    (void)user_data;
    
    // TODO: Implement sequencer mute toggle
    printf("Sequencer: Mute toggle\n");
}

void midi_cb_sequencer_player_speed(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer speed control
    printf("Sequencer: Speed %.2fx\n", param->raw_value);
}

void midi_cb_sequencer_player_blend_level(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer blend level
    printf("Sequencer: Blend level %d%%\n", (int)(param->value * 100));
}

void midi_cb_sequencer_player_offset(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer offset
    printf("Sequencer: Offset %d%%\n", (int)(param->value * 100));
}

void midi_cb_sequencer_player_attack(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer attack
    printf("Sequencer: Attack %d ms\n", (int)param->raw_value);
}

void midi_cb_sequencer_player_release(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer release
    printf("Sequencer: Release %d ms\n", (int)param->raw_value);
}

void midi_cb_sequencer_player_loop_mode(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer loop mode
    const char *modes[] = {"SIMPLE", "PINGPONG", "ONESHOT"};
    int mode = (int)param->raw_value;
    if (mode >= 0 && mode <= 2) {
        printf("Sequencer: Loop mode %s\n", modes[mode]);
    }
}

void midi_cb_sequencer_player_playback_direction(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement sequencer playback direction
    const char *dirs[] = {"FORWARD", "REVERSE"};
    int dir = (int)param->raw_value;
    if (dir >= 0 && dir <= 1) {
        printf("Sequencer: Playback direction %s\n", dirs[dir]);
    }
}

/* ============================================================================
 * SEQUENCER GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_live_mix_level(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement live mix level
    printf("Sequencer: Live mix %d%%\n", (int)(param->value * 100));
}

void midi_cb_sequencer_blend_mode(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement blend mode
    const char *modes[] = {"MIX", "CROSSFADE", "OVERLAY", "MASK"};
    int mode = (int)param->raw_value;
    if (mode >= 0 && mode <= 3) {
        printf("Sequencer: Blend mode %s\n", modes[mode]);
    }
}

void midi_cb_sequencer_master_tempo(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement master tempo
    printf("Sequencer: Master tempo %.0f BPM\n", param->raw_value);
}

void midi_cb_sequencer_quantize_res(const MidiParameterValue *param, void *user_data) {
    (void)user_data;
    
    // TODO: Implement quantize resolution
    const char *res[] = {"QUARTER", "EIGHTH", "SIXTEENTH", "BAR"};
    int r = (int)param->raw_value;
    if (r >= 0 && r <= 3) {
        printf("Sequencer: Quantize %s\n", res[r]);
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
    
    // TODO: Register sequencer callbacks with proper player IDs
    // For now, just stubs
    
    printf("MIDI Callbacks: Sequencer registered (stub)\n");
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
    midi_callbacks_register_system();
    
    printf("MIDI Callbacks: All registered\n");
}
