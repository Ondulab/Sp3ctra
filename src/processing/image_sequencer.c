#include "image_sequencer.h"
#include "../config/config_instrument.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

// Forward declarations for RGB frame operations
static void blend_rgb_frames(uint8_t *out_R, uint8_t *out_G, uint8_t *out_B,
                             const uint8_t *a_R, const uint8_t *a_G, const uint8_t *a_B,
                             const uint8_t *b_R, const uint8_t *b_G, const uint8_t *b_B,
                             float blend, int num_pixels);
static float calculate_adsr_level(ADSREnvelope *env, float normalized_position);
static inline float clamp_f(float value, float min, float max);
static inline uint8_t clamp_u8(int value);

// Utility: Get current time in microseconds
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// Utility: Linear interpolation
static inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Utility: Clamp float value
static inline float clamp_f(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Utility: Clamp int to uint8_t range
static inline uint8_t clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

// Calculate ADSR envelope level based on normalized position in sequence
static float calculate_adsr_level(ADSREnvelope *env, float normalized_position) {
    // Clamp position to [0.0, 1.0]
    normalized_position = clamp_f(normalized_position, 0.0f, 1.0f);
    
    float level = 1.0f;
    float attack_end = env->attack_ratio;
    float decay_end = attack_end + env->decay_ratio;
    float sustain_end = 1.0f - env->release_ratio;
    
    // Attack phase (0 → 1.0)
    if (env->attack_ratio > 0.0f && normalized_position < attack_end) {
        level = normalized_position / env->attack_ratio;
    }
    // Decay phase (1.0 → sustain_level)
    else if (env->decay_ratio > 0.0f && normalized_position < decay_end) {
        float decay_pos = (normalized_position - attack_end) / env->decay_ratio;
        level = lerp(1.0f, env->sustain_level, decay_pos);
    }
    // Sustain phase (constant level)
    else if (normalized_position < sustain_end) {
        level = env->sustain_level;
    }
    // Release phase (sustain_level → 0)
    else if (env->release_ratio > 0.0f) {
        float release_pos = (normalized_position - sustain_end) / env->release_ratio;
        level = lerp(env->sustain_level, 0.0f, release_pos);
    }
    else {
        level = env->sustain_level;
    }
    
    env->current_level = level;
    
    // DEBUG: Always log ADSR calculations (temporarily enabled)
    #ifdef DEBUG_SEQUENCER_ADSR
    static int log_counter = 0;
    if (++log_counter % 100 == 0) {  // Log every 100 frames to avoid spam
        log_debug("SEQUENCER", "ADSR: pos=%.3f, A=%.2f, D=%.2f, S=%.2f, R=%.2f → level=%.3f",
                  normalized_position, env->attack_ratio, env->decay_ratio, 
                  env->sustain_level, env->release_ratio, level);
    }
    #endif
    
    return level;
}

// Blend two RGB frames
static void blend_rgb_frames(uint8_t *out_R, uint8_t *out_G, uint8_t *out_B,
                             const uint8_t *a_R, const uint8_t *a_G, const uint8_t *a_B,
                             const uint8_t *b_R, const uint8_t *b_G, const uint8_t *b_B,
                             float blend, int num_pixels) {
    for (int i = 0; i < num_pixels; i++) {
        out_R[i] = (uint8_t)(a_R[i] * (1.0f - blend) + b_R[i] * blend);
        out_G[i] = (uint8_t)(a_G[i] * (1.0f - blend) + b_G[i] * blend);
        out_B[i] = (uint8_t)(a_B[i] * (1.0f - blend) + b_B[i] * blend);
    }
}

// Initialize ADSR envelope (no envelope by default - immediate 100% presence)
static void init_adsr_envelope(ADSREnvelope *env) {
    memset(env, 0, sizeof(ADSREnvelope));
    env->attack_ratio = 0.0f;   // No attack - immediate presence
    env->decay_ratio = 0.0f;    // No decay
    env->sustain_level = 1.0f;  // Full presence
    env->release_ratio = 0.0f;  // No release - instant stop
    env->current_level = 1.0f;  // Start at full level
}

// Initialize sequence player with RGB frame buffers
static void init_sequence_player(SequencePlayer *player, int buffer_capacity) {
    int nb_pixels;
    int i;
    
    memset(player, 0, sizeof(SequencePlayer));
    player->state = PLAYER_STATE_IDLE;
    player->buffer_capacity = buffer_capacity;
    
    nb_pixels = get_cis_pixels_nb();
    
    // Allocate array of RawImageFrame structures
    player->frames = (RawImageFrame *)calloc(buffer_capacity, sizeof(RawImageFrame));
    if (!player->frames) {
        log_error("SEQUENCER", "Failed to allocate frame array");
        return;
    }
    
    // Allocate RGB buffers for each frame
    for (i = 0; i < buffer_capacity; i++) {
        player->frames[i].buffer_R = (uint8_t *)malloc(nb_pixels);
        player->frames[i].buffer_G = (uint8_t *)malloc(nb_pixels);
        player->frames[i].buffer_B = (uint8_t *)malloc(nb_pixels);
        
        if (!player->frames[i].buffer_R || !player->frames[i].buffer_G || !player->frames[i].buffer_B) {
            log_error("SEQUENCER", "Failed to allocate RGB buffers for frame %d", i);
            return;
        }
    }
    
    player->playback_speed = 1.0f;
    player->playback_direction = 1;
    player->exposure = 0.5f;  // Default 50% exposure (normal)
    player->brightness = 1.0f;   // Default 100% brightness (neutral)
    player->player_mix = 0.0f;   // Default 0% = 100% player (no masking)
    player->mix_enabled = 1;     // Default enabled in mix
    player->loop_mode = LOOP_MODE_SIMPLE;
    player->trigger_mode = TRIGGER_MODE_MANUAL;
    
    init_adsr_envelope(&player->envelope);
    
    log_info("SEQUENCER", "Player initialized: %d frames capacity (%.1f MB)", 
             buffer_capacity, (buffer_capacity * nb_pixels * 3) / 1024.0f / 1024.0f);
}

// Cleanup sequence player
static void cleanup_sequence_player(SequencePlayer *player) {
    if (player->frames) {
        for (int i = 0; i < player->buffer_capacity; i++) {
            if (player->frames[i].buffer_R) free(player->frames[i].buffer_R);
            if (player->frames[i].buffer_G) free(player->frames[i].buffer_G);
            if (player->frames[i].buffer_B) free(player->frames[i].buffer_B);
        }
        free(player->frames);
        player->frames = NULL;
    }
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

ImageSequencer *image_sequencer_create(int num_players, float max_duration_s) {
    ImageSequencer *seq;
    int nb_pixels;
    int buffer_capacity;
    int i;
    
    if (num_players < 1 || num_players > 10) {
        log_error("SEQUENCER", "Invalid number of players: %d", num_players);
        return NULL;
    }
    
    seq = (ImageSequencer *)calloc(1, sizeof(ImageSequencer));
    if (!seq) {
        log_error("SEQUENCER", "Failed to allocate ImageSequencer");
        return NULL;
    }
    
    nb_pixels = get_cis_pixels_nb();
    
    seq->num_players = num_players;
    seq->max_duration_s = max_duration_s;
    seq->enabled = 0;
    seq->blend_mode = BLEND_MODE_MASK;  // Default MASK blend mode
    seq->bpm = 120.0f;
    
    if (pthread_mutex_init(&seq->mutex, NULL) != 0) {
        log_error("SEQUENCER", "Failed to initialize mutex");
        free(seq);
        return NULL;
    }
    
    seq->players = (SequencePlayer *)calloc(num_players, sizeof(SequencePlayer));
    if (!seq->players) {
        log_error("SEQUENCER", "Failed to allocate players");
        pthread_mutex_destroy(&seq->mutex);
        free(seq);
        return NULL;
    }
    
    /* Allocate output frame RGB buffers */
    seq->output_frame.buffer_R = (uint8_t *)malloc(nb_pixels);
    seq->output_frame.buffer_G = (uint8_t *)malloc(nb_pixels);
    seq->output_frame.buffer_B = (uint8_t *)malloc(nb_pixels);
    
    if (!seq->output_frame.buffer_R || !seq->output_frame.buffer_G || !seq->output_frame.buffer_B) {
        log_error("SEQUENCER", "Failed to allocate output frame buffers");
        free(seq->players);
        pthread_mutex_destroy(&seq->mutex);
        free(seq);
        return NULL;
    }
    
    buffer_capacity = (int)(max_duration_s * 1000);
    for (i = 0; i < num_players; i++) {
        init_sequence_player(&seq->players[i], buffer_capacity);
    }
    
    log_info("SEQUENCER", "Image Sequencer created: %d players, %.1fs capacity (%.1f MB total)", 
             num_players, max_duration_s,
             (num_players * buffer_capacity * nb_pixels * 3) / 1024.0f / 1024.0f);
    return seq;
}

void image_sequencer_destroy(ImageSequencer *seq) {
    if (!seq) return;
    
    if (seq->players) {
        for (int i = 0; i < seq->num_players; i++) {
            cleanup_sequence_player(&seq->players[i]);
        }
        free(seq->players);
    }
    
    if (seq->output_frame.buffer_R) free(seq->output_frame.buffer_R);
    if (seq->output_frame.buffer_G) free(seq->output_frame.buffer_G);
    if (seq->output_frame.buffer_B) free(seq->output_frame.buffer_B);
    
    pthread_mutex_destroy(&seq->mutex);
    free(seq);
    log_info("SEQUENCER", "Image Sequencer destroyed");
}

// Recording Control
int image_sequencer_start_recording(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    // If currently playing, switch to RECORDING_PLAYING state (simultaneous record+playback)
    if (player->state == PLAYER_STATE_PLAYING) {
        player->state = PLAYER_STATE_RECORDING_PLAYING;
        pthread_mutex_unlock(&seq->mutex);
        log_info("SEQUENCER", "Player %d: Started recording while playing (simultaneous mode, %d frames existing)", 
                 player_id, player->recorded_frames);
        return 0;
    }
    
    // Otherwise, only allow recording from IDLE, READY, or STOPPED states
    if (player->state != PLAYER_STATE_IDLE && 
        player->state != PLAYER_STATE_READY && 
        player->state != PLAYER_STATE_STOPPED) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_RECORDING;
    // NOTE: recorded_frames is NOT reset - additive recording behavior
    // Frames will be added starting from current recorded_frames position
    // playback_position is also NOT reset to allow continuing from where we are
    
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Started recording (additive mode, %d frames existing)", 
             player_id, player->recorded_frames);
    return 0;
}

int image_sequencer_stop_recording(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    // Handle both RECORDING and RECORDING_PLAYING states
    if (player->state == PLAYER_STATE_RECORDING_PLAYING) {
        // Was recording while playing, return to PLAYING state
        player->state = PLAYER_STATE_PLAYING;
        log_info("SEQUENCER", "Player %d: Stopped recording, continuing playback (%d frames)", 
                 player_id, player->recorded_frames);
        pthread_mutex_unlock(&seq->mutex);
        return 0;
    }
    
    if (player->state != PLAYER_STATE_RECORDING) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_READY;
    log_info("SEQUENCER", "Player %d: Stopped recording, ready (%d frames)", 
             player_id, player->recorded_frames);
    
    pthread_mutex_unlock(&seq->mutex);
    return 0;
}

// Playback Control
int image_sequencer_start_playback(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state != PLAYER_STATE_READY && 
        player->state != PLAYER_STATE_STOPPED &&
        player->state != PLAYER_STATE_MUTED) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    if (player->recorded_frames == 0) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_PLAYING;
    player->playback_position = 0.0f;
    
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Started playback", player_id);
    return 0;
}

int image_sequencer_stop_playback(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state != PLAYER_STATE_PLAYING) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_STOPPED;
    
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Stopped playback", player_id);
    return 0;
}

int image_sequencer_toggle_playback(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    int is_playing = (player->state == PLAYER_STATE_PLAYING);
    pthread_mutex_unlock(&seq->mutex);
    
    if (is_playing) {
        return image_sequencer_stop_playback(seq, player_id);
    } else {
        return image_sequencer_start_playback(seq, player_id);
    }
}

// Player Parameters
void image_sequencer_set_speed(ImageSequencer *seq, int player_id, float speed) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    speed = clamp_f(speed, 0.1f, 10.0f);
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].playback_speed = speed;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Speed %.2fx", player_id, speed);
}

void image_sequencer_set_offset(ImageSequencer *seq, int player_id, int offset_frames) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (offset_frames >= 0 && offset_frames < player->recorded_frames) {
        player->playback_offset = offset_frames;
        player->playback_position = (float)offset_frames;
        log_info("SEQUENCER", "Player %d: Offset %d frames", player_id, offset_frames);
    }
    
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_loop_mode(ImageSequencer *seq, int player_id, LoopMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].loop_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
    
    const char *mode_names[] = {"SIMPLE", "PINGPONG", "ONESHOT"};
    if (mode >= 0 && mode <= 2) {
        log_info("SEQUENCER", "Player %d: Loop mode %s", player_id, mode_names[mode]);
    }
}

void image_sequencer_set_trigger_mode(ImageSequencer *seq, int player_id, TriggerMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].trigger_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
    
    const char *mode_names[] = {"MANUAL", "AUTO", "SYNC"};
    if (mode >= 0 && mode <= 2) {
        log_info("SEQUENCER", "Player %d: Trigger mode %s", player_id, mode_names[mode]);
    }
}

void image_sequencer_set_exposure(ImageSequencer *seq, int player_id, float level) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    level = clamp_f(level, 0.0f, 1.0f);
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].exposure = level;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Exposure %d%%", player_id, (int)(level * 100));
}

void image_sequencer_set_brightness(ImageSequencer *seq, int player_id, float brightness) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    brightness = clamp_f(brightness, 0.5f, 2.0f);
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].brightness = brightness;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Brightness %.0f%%", player_id, brightness * 100);
}

void image_sequencer_set_player_mix(ImageSequencer *seq, int player_id, float mix) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    mix = clamp_f(mix, 0.0f, 1.0f);
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].player_mix = mix;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Player mix %d%% (0%%=player, 100%%=mask)", player_id, (int)(mix * 100));
}

void image_sequencer_set_mix_enabled(ImageSequencer *seq, int player_id, int enabled) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].mix_enabled = enabled ? 1 : 0;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Mix %s", player_id, enabled ? "ENABLED" : "DISABLED");
}

void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].playback_direction = (direction >= 0) ? 1 : -1;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Direction %s", player_id, (direction >= 0) ? "FORWARD" : "REVERSE");
}

// Player State Control
int image_sequencer_clear_buffer(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    // Clear all recorded frames
    player->recorded_frames = 0;
    player->playback_position = 0.0f;
    player->playback_offset = 0;
    player->state = PLAYER_STATE_IDLE;
    
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Player %d: Buffer cleared", player_id);
    return 0;
}

// ADSR Control (positional envelope)
void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, 
                              float attack_ratio, float decay_ratio,
                              float sustain_level, float release_ratio) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    attack_ratio = clamp_f(attack_ratio, 0.0f, 1.0f);
    decay_ratio = clamp_f(decay_ratio, 0.0f, 1.0f);
    sustain_level = clamp_f(sustain_level, 0.0f, 1.0f);
    release_ratio = clamp_f(release_ratio, 0.0f, 1.0f);
    
    // Ensure attack + decay + release doesn't exceed 1.0
    float total = attack_ratio + decay_ratio + release_ratio;
    if (total > 1.0f) {
        attack_ratio /= total;
        decay_ratio /= total;
        release_ratio /= total;
    }
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->attack_ratio = attack_ratio;
    env->decay_ratio = decay_ratio;
    env->sustain_level = sustain_level;
    env->release_ratio = release_ratio;
    pthread_mutex_unlock(&seq->mutex);
    
    log_info("SEQUENCER", "Player %d: ADSR A=%.0f%% D=%.0f%% S=%.0f%% R=%.0f%%", 
             player_id, attack_ratio * 100, decay_ratio * 100, sustain_level * 100, release_ratio * 100);
}

void image_sequencer_set_attack(ImageSequencer *seq, int player_id, float attack_ratio) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    attack_ratio = clamp_f(attack_ratio, 0.0f, 1.0f);
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->attack_ratio = attack_ratio;
    
    // Ensure attack + decay + release doesn't exceed 1.0
    float total = env->attack_ratio + env->decay_ratio + env->release_ratio;
    if (total > 1.0f) {
        float scale = 1.0f / total;
        env->attack_ratio *= scale;
        env->decay_ratio *= scale;
        env->release_ratio *= scale;
    }
    pthread_mutex_unlock(&seq->mutex);
    
    log_info("SEQUENCER", "Player %d: Attack %.0f%%", player_id, attack_ratio * 100);
}

void image_sequencer_set_decay(ImageSequencer *seq, int player_id, float decay_ratio) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    decay_ratio = clamp_f(decay_ratio, 0.0f, 1.0f);
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->decay_ratio = decay_ratio;
    
    // Ensure attack + decay + release doesn't exceed 1.0
    float total = env->attack_ratio + env->decay_ratio + env->release_ratio;
    if (total > 1.0f) {
        float scale = 1.0f / total;
        env->attack_ratio *= scale;
        env->decay_ratio *= scale;
        env->release_ratio *= scale;
    }
    pthread_mutex_unlock(&seq->mutex);
    
    log_info("SEQUENCER", "Player %d: Decay %.0f%%", player_id, decay_ratio * 100);
}

void image_sequencer_set_sustain(ImageSequencer *seq, int player_id, float sustain_level) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    sustain_level = clamp_f(sustain_level, 0.0f, 1.0f);
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].envelope.sustain_level = sustain_level;
    pthread_mutex_unlock(&seq->mutex);
    
    log_info("SEQUENCER", "Player %d: Sustain %.0f%%", player_id, sustain_level * 100);
}

void image_sequencer_set_release(ImageSequencer *seq, int player_id, float release_ratio) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    release_ratio = clamp_f(release_ratio, 0.0f, 1.0f);
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->release_ratio = release_ratio;
    
    // Ensure attack + decay + release doesn't exceed 1.0
    float total = env->attack_ratio + env->decay_ratio + env->release_ratio;
    if (total > 1.0f) {
        float scale = 1.0f / total;
        env->attack_ratio *= scale;
        env->decay_ratio *= scale;
        env->release_ratio *= scale;
    }
    pthread_mutex_unlock(&seq->mutex);
    
    log_info("SEQUENCER", "Player %d: Release %.0f%%", player_id, release_ratio * 100);
}

// Global Control
void image_sequencer_set_enabled(ImageSequencer *seq, int enabled) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->enabled = enabled;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "Sequencer %s", enabled ? "ENABLED" : "DISABLED");
}

void image_sequencer_set_blend_mode(ImageSequencer *seq, BlendMode mode) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->blend_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
    
    const char *mode_names[] = {"MIX", "ADD", "SCREEN", "MASK"};
    if (mode >= 0 && mode <= 3) {
        log_info("SEQUENCER", "Blend mode: %s", mode_names[mode]);
    }
}


void image_sequencer_set_bpm(ImageSequencer *seq, float bpm) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->bpm = clamp_f(bpm, 60.0f, 240.0f);
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "BPM: %.0f", bpm);
}

void image_sequencer_enable_midi_sync(ImageSequencer *seq, int enable) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->midi_clock_sync = enable;
    pthread_mutex_unlock(&seq->mutex);
    log_info("SEQUENCER", "MIDI sync: %s", enable ? "ON" : "OFF");
}

// MIDI Clock Integration
void image_sequencer_midi_clock_tick(ImageSequencer *seq) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->last_clock_us = get_time_us();
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_midi_clock_start(ImageSequencer *seq) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->last_clock_us = get_time_us();
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_midi_clock_stop(ImageSequencer *seq) {
    (void)seq;
    // Could pause all players or reset clock
}

/* ============================================================================
 * MAIN PROCESSING FUNCTION - NEW RGB-BASED IMPLEMENTATION
 * ============================================================================ */

int image_sequencer_process_frame(
    ImageSequencer *seq,
    const uint8_t *live_R,
    const uint8_t *live_G,
    const uint8_t *live_B,
    uint8_t *output_R,
    uint8_t *output_G,
    uint8_t *output_B
) {
    if (!seq || !live_R || !live_G || !live_B || !output_R || !output_G || !output_B) {
        return -1;
    }
    
    pthread_mutex_lock(&seq->mutex);
    
    uint64_t start_time = get_time_us();
    
    // DEBUG: Log sequencer state
    #ifdef DEBUG_SEQUENCER_STATE
    static int state_log_counter = 0;
    if (++state_log_counter % 1000 == 0) {
        log_debug("SEQUENCER", "STATE: enabled=%d, num_players=%d", 
                  seq->enabled, seq->num_players);
    }
    #endif
    
    /* If disabled, just pass through */
    if (!seq->enabled) {
        int nb_pixels = get_cis_pixels_nb();
        memcpy(output_R, live_R, nb_pixels);
        memcpy(output_G, live_G, nb_pixels);
        memcpy(output_B, live_B, nb_pixels);
        pthread_mutex_unlock(&seq->mutex);
        return 0;
    }
    
    int has_active_players = 0;
    int nb_pixels;
    float *accum_R;
    float *accum_G;
    float *accum_B;
    int i;
    
    nb_pixels = get_cis_pixels_nb();
    
    /* Initialize output accumulators (float for additive blending) */
    accum_R = (float *)calloc(nb_pixels, sizeof(float));
    accum_G = (float *)calloc(nb_pixels, sizeof(float));
    accum_B = (float *)calloc(nb_pixels, sizeof(float));
    
    if (!accum_R || !accum_G || !accum_B) {
        pthread_mutex_unlock(&seq->mutex);
        free(accum_R); free(accum_G); free(accum_B);
        return -1;
    }
    
    /* Process each player */
    for (i = 0; i < seq->num_players; i++) {
        SequencePlayer *player = &seq->players[i];
        
        /* Handle recording - Ring buffer mode (additive with wrap-around) */
        /* Record if in RECORDING or RECORDING_PLAYING state */
        if (player->state == PLAYER_STATE_RECORDING || player->state == PLAYER_STATE_RECORDING_PLAYING) {
            /* Calculate write index using modulo for ring buffer behavior */
            int write_index = player->recorded_frames % player->buffer_capacity;
            
            /* Write frame at calculated index */
            memcpy(player->frames[write_index].buffer_R, live_R, nb_pixels);
            memcpy(player->frames[write_index].buffer_G, live_G, nb_pixels);
            memcpy(player->frames[write_index].buffer_B, live_B, nb_pixels);
            player->frames[write_index].timestamp_us = get_time_us();
            
            /* Increment recorded_frames counter */
            player->recorded_frames++;
            
            /* Clamp recorded_frames to buffer_capacity for playback purposes */
            /* This ensures playback never tries to read beyond buffer_capacity */
            if (player->recorded_frames > player->buffer_capacity) {
                player->recorded_frames = player->buffer_capacity;
            }
        }
        
        /* Handle playback */
        /* Play if in PLAYING or RECORDING_PLAYING state */
        if (player->state == PLAYER_STATE_PLAYING || player->state == PLAYER_STATE_RECORDING_PLAYING) {
            if (player->recorded_frames == 0) continue;
            
            // Calculate normalized position for ASR envelope
            float normalized_pos = 0.0f;
            if (player->recorded_frames > 0) {
                normalized_pos = player->playback_position / (float)player->recorded_frames;
            }
            
            // Calculate envelope level based on position
            float env_level = calculate_adsr_level(&player->envelope, normalized_pos);
            
            #ifdef DEBUG_SEQUENCER_PLAYBACK
            if (i == 0 && (int)player->playback_position % 100 == 0) {  // Log every 100 frames for player 0
                log_debug("SEQUENCER", "Player %d: pos=%.1f/%d (%.1f%%), env=%.3f, blend=%.2f",
                          i, player->playback_position, player->recorded_frames,
                          normalized_pos * 100.0f, env_level, player->exposure);
            }
            #endif
            
            // Get current frame with interpolation
            int frame_idx = (int)player->playback_position;
            float frac = player->playback_position - (float)frame_idx;
            
            // Handle loop modes at sequence boundaries
            if (frame_idx >= player->recorded_frames) {
                // Reached end of sequence (forward playback)
                if (player->loop_mode == LOOP_MODE_SIMPLE) {
                    frame_idx = 0;
                    player->playback_position = 0.0f;
                } else if (player->loop_mode == LOOP_MODE_PINGPONG) {
                    player->playback_direction *= -1;
                    frame_idx = player->recorded_frames - 1;
                    player->playback_position = (float)frame_idx;
                } else { // LOOP_MODE_ONESHOT
                    player->state = PLAYER_STATE_STOPPED;
                    continue;
                }
            } else if (frame_idx < 0) {
                // Reached beginning of sequence (reverse playback)
                if (player->loop_mode == LOOP_MODE_SIMPLE) {
                    frame_idx = player->recorded_frames - 1;
                    player->playback_position = (float)frame_idx;
                } else if (player->loop_mode == LOOP_MODE_PINGPONG) {
                    player->playback_direction *= -1;
                    frame_idx = 0;
                    player->playback_position = 0.0f;
                } else { // LOOP_MODE_ONESHOT
                    player->state = PLAYER_STATE_STOPPED;
                    continue;
                }
            }
            
            /* Get player frame RGB (with interpolation if needed) */
            {
                uint8_t *player_R;
                uint8_t *player_G;
                uint8_t *player_B;
                uint8_t *temp_R;
                uint8_t *temp_G;
                uint8_t *temp_B;
                
                temp_R = NULL;
                temp_G = NULL;
                temp_B = NULL;
                
                if (frac > 0.001f && frame_idx + 1 < player->recorded_frames) {
                    /* Interpolate between two frames */
                    temp_R = (uint8_t *)malloc(nb_pixels);
                    temp_G = (uint8_t *)malloc(nb_pixels);
                    temp_B = (uint8_t *)malloc(nb_pixels);
                    
                    if (temp_R && temp_G && temp_B) {
                        blend_rgb_frames(temp_R, temp_G, temp_B,
                                       player->frames[frame_idx].buffer_R,
                                       player->frames[frame_idx].buffer_G,
                                       player->frames[frame_idx].buffer_B,
                                       player->frames[frame_idx + 1].buffer_R,
                                       player->frames[frame_idx + 1].buffer_G,
                                       player->frames[frame_idx + 1].buffer_B,
                                       frac, nb_pixels);
                        player_R = temp_R;
                        player_G = temp_G;
                        player_B = temp_B;
                    } else {
                        /* Allocation failed, use current frame directly */
                        player_R = player->frames[frame_idx].buffer_R;
                        player_G = player->frames[frame_idx].buffer_G;
                        player_B = player->frames[frame_idx].buffer_B;
                    }
                } else {
                    /* Use current frame directly */
                    player_R = player->frames[frame_idx].buffer_R;
                    player_G = player->frames[frame_idx].buffer_G;
                    player_B = player->frames[frame_idx].buffer_B;
                }
            
                /* Apply envelope, brightness, and blend level with alpha-blending */
                {
                    int p;
                    
                    /* Skip if player is disabled in mix */
                    if (!player->mix_enabled) {
                        /* Advance playback position even if disabled */
                        player->playback_position += player->playback_speed * player->playback_direction;
                        goto skip_player;
                    }
                    
                    /* 🎨 EXPOSURE CONTROL: exposure parameter controls exposure */
                    /* 0% = underexposed (-2 stops), 50% = normal (0 stops), 100% = overexposed (+4 stops) */
                    for (p = 0; p < nb_pixels; p++) {
                        /* Step 1: Apply brightness boost */
                        float boosted_R = player_R[p] * player->brightness;
                        float boosted_G = player_G[p] * player->brightness;
                        float boosted_B = player_B[p] * player->brightness;
                        
                        /* Clamp after brightness */
                        boosted_R = (boosted_R > 255.0f) ? 255.0f : boosted_R;
                        boosted_G = (boosted_G > 255.0f) ? 255.0f : boosted_G;
                        boosted_B = (boosted_B > 255.0f) ? 255.0f : boosted_B;
                        
                        /* Step 2: Apply EXTREME exposure control via exposure parameter */
                        /* Map exposure: 0.0 → 0.1x (très sous-exposé), 0.5 → 1.0x (normal), 1.0 → 16.0x (complètement cramé) */
                        float exposure_mult;
                        if (player->exposure < 0.5f) {
                            /* 0% to 50%: très sous-exposé → normal (0.1x to 1.0x) */
                            exposure_mult = 0.1f + (player->exposure * 2.0f) * 0.9f;
                        } else {
                            /* 50% to 100%: normal → complètement cramé (1.0x to 16.0x) */
                            exposure_mult = 1.0f + ((player->exposure - 0.5f) * 2.0f) * 15.0f;
                        }
                        
                        float exposed_R = boosted_R * exposure_mult;
                        float exposed_G = boosted_G * exposure_mult;
                        float exposed_B = boosted_B * exposure_mult;
                        
                        /* Clamp after exposure (creates the "blown out" white effect) */
                        exposed_R = (exposed_R > 255.0f) ? 255.0f : exposed_R;
                        exposed_G = (exposed_G > 255.0f) ? 255.0f : exposed_G;
                        exposed_B = (exposed_B > 255.0f) ? 255.0f : exposed_B;
                        
                        /* Step 3: Apply envelope for temporal control */
                        float enveloped_R = exposed_R * env_level;
                        float enveloped_G = exposed_G * env_level;
                        float enveloped_B = exposed_B * env_level;
                        
                        /* Step 4: Apply player_mix with multiplicative masking */
                        /* player_mix = 0.0 → 100% player (no masking) */
                        /* player_mix = 1.0 → 100% multiplicative mask (player filters general) */
                        float mask_R = enveloped_R / 255.0f;  // Normalize to [0, 1]
                        float mask_G = enveloped_G / 255.0f;
                        float mask_B = enveloped_B / 255.0f;
                        
                        /* Multiplicative masking: player acts as color filter on general */
                        float masked_R = live_R[p] * mask_R;
                        float masked_G = live_G[p] * mask_G;
                        float masked_B = live_B[p] * mask_B;
                        
                        /* Crossfade between pure player and masked general */
                        float final_R = enveloped_R * (1.0f - player->player_mix) + masked_R * player->player_mix;
                        float final_G = enveloped_G * (1.0f - player->player_mix) + masked_G * player->player_mix;
                        float final_B = enveloped_B * (1.0f - player->player_mix) + masked_B * player->player_mix;
                        
                        /* Step 5: Accumulate to output */
                        accum_R[p] += final_R;
                        accum_G[p] += final_G;
                        accum_B[p] += final_B;
                    }
                    
                    has_active_players = 1;
                    
                    /* Advance playback position */
                    player->playback_position += player->playback_speed * player->playback_direction;
                }
                
                skip_player:
                
                /* Free temporary interpolation buffers if allocated */
                if (temp_R) free(temp_R);
                if (temp_G) free(temp_G);
                if (temp_B) free(temp_B);
            }
        }
    }
    
    /* Write accumulated output (no global mix, each player has its own player_mix) */
    if (has_active_players) {
        int p;
        
        /* Direct output from accumulator (players already mixed with general via player_mix) */
        for (p = 0; p < nb_pixels; p++) {
            /* Clamp and write output */
            output_R[p] = clamp_u8((int)accum_R[p]);
            output_G[p] = clamp_u8((int)accum_G[p]);
            output_B[p] = clamp_u8((int)accum_B[p]);
        }
    } else {
        /* No active players, just pass through live */
        memcpy(output_R, live_R, nb_pixels);
        memcpy(output_G, live_G, nb_pixels);
        memcpy(output_B, live_B, nb_pixels);
    }
    
    /* Cleanup */
    free(accum_R);
    free(accum_G);
    free(accum_B);
    
    // Update statistics
    seq->frames_processed++;
    uint64_t process_time = get_time_us() - start_time;
    seq->total_process_time_us += process_time;
    
    #ifdef DEBUG_SEQUENCER_PERFORMANCE
    if (seq->frames_processed % 1000 == 0) {  // Log every 1000 frames
        float avg_time = (float)seq->total_process_time_us / (float)seq->frames_processed;
        log_debug("SEQUENCER", "PERF: Frames: %llu, Avg time: %.2f µs, Active players: %d",
                  (unsigned long long)seq->frames_processed, avg_time, has_active_players);
    }
    #endif
    
    pthread_mutex_unlock(&seq->mutex);
    return 0;
}

// MIDI callback registration
void image_sequencer_register_midi_callbacks(ImageSequencer *seq) {
    (void)seq;
    // TODO: Register MIDI callbacks for transport control
}

// Statistics and debugging
void image_sequencer_get_stats(ImageSequencer *seq, 
                               uint64_t *frames_processed, 
                               float *avg_process_time_us) {
    if (!seq) return;
    
    pthread_mutex_lock(&seq->mutex);
    
    if (frames_processed) {
        *frames_processed = seq->frames_processed;
    }
    
    if (avg_process_time_us) {
        if (seq->frames_processed > 0) {
            *avg_process_time_us = (float)seq->total_process_time_us / (float)seq->frames_processed;
        } else {
            *avg_process_time_us = 0.0f;
        }
    }
    
    pthread_mutex_unlock(&seq->mutex);
}

PlayerState image_sequencer_get_player_state(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) {
        return PLAYER_STATE_IDLE;
    }
    
    pthread_mutex_lock(&seq->mutex);
    PlayerState state = seq->players[player_id].state;
    pthread_mutex_unlock(&seq->mutex);
    
    return state;
}

void image_sequencer_print_status(ImageSequencer *seq) {
    if (!seq) return;
    
    pthread_mutex_lock(&seq->mutex);
    
    printf("\n========== IMAGE SEQUENCER STATUS ==========\n");
    printf("Enabled: %s\n", seq->enabled ? "YES" : "NO");
    printf("Blend Mode: %d\n", seq->blend_mode);
    printf("BPM: %.1f\n", seq->bpm);
    printf("MIDI Sync: %s\n", seq->midi_clock_sync ? "ON" : "OFF");
    printf("Frames Processed: %llu\n", (unsigned long long)seq->frames_processed);
    
    printf("\n--- Players Status ---\n");
    for (int i = 0; i < seq->num_players; i++) {
        SequencePlayer *player = &seq->players[i];
        const char *state_str = "UNKNOWN";
        switch (player->state) {
            case PLAYER_STATE_IDLE: state_str = "IDLE"; break;
            case PLAYER_STATE_RECORDING: state_str = "RECORDING"; break;
            case PLAYER_STATE_READY: state_str = "READY"; break;
            case PLAYER_STATE_PLAYING: state_str = "PLAYING"; break;
            case PLAYER_STATE_STOPPED: state_str = "STOPPED"; break;
            case PLAYER_STATE_MUTED: state_str = "MUTED"; break;
            case PLAYER_STATE_RECORDING_PLAYING: state_str = "RECORDING+PLAYING"; break;
        }
        printf("Player %d: %s (%d frames)\n", i, state_str, player->recorded_frames);
    }
    printf("============================================\n\n");
    
    pthread_mutex_unlock(&seq->mutex);
}
