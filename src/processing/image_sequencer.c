#include "image_sequencer.h"
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
static float calculate_adsr_level(ADSREnvelope *env, uint64_t current_time);
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

// Calculate ADSR envelope level
static float calculate_adsr_level(ADSREnvelope *env, uint64_t current_time) {
    if (!env->is_triggered) {
        return 0.0f;
    }
    
    uint64_t elapsed_us = current_time - env->trigger_time_us;
    float elapsed_ms = elapsed_us / 1000.0f;
    
    // Attack phase
    if (elapsed_ms < env->attack_ms) {
        float t = elapsed_ms / env->attack_ms;
        env->current_level = lerp(0.0f, 1.0f, t);
        return env->current_level;
    }
    
    // Decay phase
    elapsed_ms -= env->attack_ms;
    if (elapsed_ms < env->decay_ms) {
        float t = elapsed_ms / env->decay_ms;
        env->current_level = lerp(1.0f, env->sustain_level, t);
        return env->current_level;
    }
    
    // Sustain phase (until release)
    if (env->release_time_us == 0) {
        env->current_level = env->sustain_level;
        return env->current_level;
    }
    
    // Release phase
    uint64_t release_elapsed_us = current_time - env->release_time_us;
    float release_elapsed_ms = release_elapsed_us / 1000.0f;
    
    if (release_elapsed_ms < env->release_ms) {
        float t = release_elapsed_ms / env->release_ms;
        float release_start = env->current_level;
        env->current_level = lerp(release_start, 0.0f, t);
        return env->current_level;
    }
    
    // Release complete
    env->is_triggered = 0;
    env->current_level = 0.0f;
    return 0.0f;
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

// Initialize ADSR envelope (disabled by default for immediate response)
static void init_adsr_envelope(ADSREnvelope *env) {
    memset(env, 0, sizeof(ADSREnvelope));
    env->attack_ms = 0.0f;      // No attack - immediate volume
    env->decay_ms = 0.0f;       // No decay
    env->sustain_level = 1.0f;  // Full sustain level
    env->release_ms = 0.0f;     // No release - instant stop
    env->is_triggered = 0;
    env->current_level = 1.0f;  // Start at full level
}

// Initialize sequence player with RGB frame buffers
static void init_sequence_player(SequencePlayer *player, int buffer_capacity) {
    memset(player, 0, sizeof(SequencePlayer));
    player->state = PLAYER_STATE_IDLE;
    player->buffer_capacity = buffer_capacity;
    
    // Allocate array of RawImageFrame structures
    player->frames = (RawImageFrame *)calloc(buffer_capacity, sizeof(RawImageFrame));
    if (!player->frames) {
        fprintf(stderr, "[ERROR] Failed to allocate frame array\n");
        return;
    }
    
    // Allocate RGB buffers for each frame
    for (int i = 0; i < buffer_capacity; i++) {
        player->frames[i].buffer_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
        player->frames[i].buffer_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
        player->frames[i].buffer_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
        
        if (!player->frames[i].buffer_R || !player->frames[i].buffer_G || !player->frames[i].buffer_B) {
            fprintf(stderr, "[ERROR] Failed to allocate RGB buffers for frame %d\n", i);
            return;
        }
    }
    
    player->playback_speed = 1.0f;
    player->playback_direction = 1;
    player->blend_level = 1.0f;
    player->loop_mode = LOOP_MODE_SIMPLE;
    player->trigger_mode = TRIGGER_MODE_MANUAL;
    
    init_adsr_envelope(&player->envelope);
    
    printf("[INFO] Player initialized: %d frames capacity (%.1f MB)\n", 
           buffer_capacity, (buffer_capacity * CIS_MAX_PIXELS_NB * 3) / 1024.0f / 1024.0f);
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
    if (num_players < 1 || num_players > 10) {
        fprintf(stderr, "[ERROR] Invalid number of players: %d\n", num_players);
        return NULL;
    }
    
    ImageSequencer *seq = (ImageSequencer *)calloc(1, sizeof(ImageSequencer));
    if (!seq) {
        fprintf(stderr, "[ERROR] Failed to allocate ImageSequencer\n");
        return NULL;
    }
    
    seq->num_players = num_players;
    seq->max_duration_s = max_duration_s;
    seq->enabled = 0;
    seq->blend_mode = BLEND_MODE_MIX;
    seq->live_mix_level = 0.0f;  // 0.0 = 100% sequencer, 1.0 = 100% live
    seq->bpm = 120.0f;
    
    if (pthread_mutex_init(&seq->mutex, NULL) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize mutex\n");
        free(seq);
        return NULL;
    }
    
    seq->players = (SequencePlayer *)calloc(num_players, sizeof(SequencePlayer));
    if (!seq->players) {
        fprintf(stderr, "[ERROR] Failed to allocate players\n");
        pthread_mutex_destroy(&seq->mutex);
        free(seq);
        return NULL;
    }
    
    // Allocate output frame RGB buffers
    seq->output_frame.buffer_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
    seq->output_frame.buffer_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
    seq->output_frame.buffer_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB);
    
    if (!seq->output_frame.buffer_R || !seq->output_frame.buffer_G || !seq->output_frame.buffer_B) {
        fprintf(stderr, "[ERROR] Failed to allocate output frame buffers\n");
        free(seq->players);
        pthread_mutex_destroy(&seq->mutex);
        free(seq);
        return NULL;
    }
    
    int buffer_capacity = (int)(max_duration_s * 1000);
    for (int i = 0; i < num_players; i++) {
        init_sequence_player(&seq->players[i], buffer_capacity);
    }
    
    printf("[INFO] Image Sequencer created: %d players, %.1fs capacity (%.1f MB total)\n", 
           num_players, max_duration_s,
           (num_players * buffer_capacity * CIS_MAX_PIXELS_NB * 3) / 1024.0f / 1024.0f);
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
    printf("[INFO] Image Sequencer destroyed\n");
}

// Recording Control
int image_sequencer_start_recording(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state != PLAYER_STATE_IDLE && player->state != PLAYER_STATE_READY) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_RECORDING;
    player->recorded_frames = 0;
    player->playback_position = 0.0f;
    
    pthread_mutex_unlock(&seq->mutex);
    printf("[SEQ] Player %d: Started recording\n", player_id);
    return 0;
}

int image_sequencer_stop_recording(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state != PLAYER_STATE_RECORDING) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_READY;
    
    // Auto-play if trigger mode is auto
    if (player->trigger_mode == TRIGGER_MODE_AUTO && player->recorded_frames > 0) {
        player->state = PLAYER_STATE_PLAYING;
        player->playback_position = 0.0f;
        player->envelope.is_triggered = 1;
        player->envelope.trigger_time_us = get_time_us();
        player->envelope.release_time_us = 0;
        printf("[SEQ] Player %d: Stopped recording, auto-playing (%d frames)\n", 
               player_id, player->recorded_frames);
    } else {
        printf("[SEQ] Player %d: Stopped recording, ready (%d frames)\n", 
               player_id, player->recorded_frames);
    }
    
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
    
    player->envelope.is_triggered = 1;
    player->envelope.trigger_time_us = get_time_us();
    player->envelope.release_time_us = 0;
    
    pthread_mutex_unlock(&seq->mutex);
    printf("[SEQ] Player %d: Started playback\n", player_id);
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
    player->envelope.release_time_us = get_time_us();
    
    pthread_mutex_unlock(&seq->mutex);
    printf("[SEQ] Player %d: Stopped playback\n", player_id);
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
}

void image_sequencer_set_offset(ImageSequencer *seq, int player_id, int offset_frames) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (offset_frames >= 0 && offset_frames < player->recorded_frames) {
        player->playback_offset = offset_frames;
        player->playback_position = (float)offset_frames;
    }
    
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_loop_mode(ImageSequencer *seq, int player_id, LoopMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].loop_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_trigger_mode(ImageSequencer *seq, int player_id, TriggerMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].trigger_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_blend_level(ImageSequencer *seq, int player_id, float level) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    level = clamp_f(level, 0.0f, 1.0f);
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].blend_level = level;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].playback_direction = (direction >= 0) ? 1 : -1;
    pthread_mutex_unlock(&seq->mutex);
}

// Player State Control
int image_sequencer_mute_player(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state == PLAYER_STATE_PLAYING) {
        player->state = PLAYER_STATE_MUTED;
        player->envelope.release_time_us = get_time_us();
        pthread_mutex_unlock(&seq->mutex);
        printf("[SEQ] Player %d: Muted\n", player_id);
        return 0;
    }
    
    pthread_mutex_unlock(&seq->mutex);
    return -1;
}

int image_sequencer_unmute_player(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state == PLAYER_STATE_MUTED && player->recorded_frames > 0) {
        player->state = PLAYER_STATE_PLAYING;
        player->envelope.is_triggered = 1;
        player->envelope.trigger_time_us = get_time_us();
        player->envelope.release_time_us = 0;
        pthread_mutex_unlock(&seq->mutex);
        printf("[SEQ] Player %d: Unmuted\n", player_id);
        return 0;
    }
    
    pthread_mutex_unlock(&seq->mutex);
    return -1;
}

int image_sequencer_toggle_mute(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    int is_muted = (seq->players[player_id].state == PLAYER_STATE_MUTED);
    pthread_mutex_unlock(&seq->mutex);
    
    if (is_muted) {
        return image_sequencer_unmute_player(seq, player_id);
    } else {
        return image_sequencer_mute_player(seq, player_id);
    }
}

// ADSR Control
void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, 
                              float attack_ms, float decay_ms, 
                              float sustain_level, float release_ms) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    attack_ms = clamp_f(attack_ms, 0.0f, 5000.0f);
    decay_ms = clamp_f(decay_ms, 0.0f, 5000.0f);
    sustain_level = clamp_f(sustain_level, 0.0f, 1.0f);
    release_ms = clamp_f(release_ms, 0.0f, 10000.0f);
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->attack_ms = attack_ms;
    env->decay_ms = decay_ms;
    env->sustain_level = sustain_level;
    env->release_ms = release_ms;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_trigger_envelope(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->is_triggered = 1;
    env->trigger_time_us = get_time_us();
    env->release_time_us = 0;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_release_envelope(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    ADSREnvelope *env = &seq->players[player_id].envelope;
    env->release_time_us = get_time_us();
    pthread_mutex_unlock(&seq->mutex);
}

// Global Control
void image_sequencer_set_enabled(ImageSequencer *seq, int enabled) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->enabled = enabled;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_blend_mode(ImageSequencer *seq, BlendMode mode) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->blend_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_live_mix_level(ImageSequencer *seq, float level) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->live_mix_level = clamp_f(level, 0.0f, 1.0f);
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_bpm(ImageSequencer *seq, float bpm) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->bpm = clamp_f(bpm, 60.0f, 240.0f);
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_enable_midi_sync(ImageSequencer *seq, int enable) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->midi_clock_sync = enable;
    pthread_mutex_unlock(&seq->mutex);
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
    
    // If disabled, just pass through
    if (!seq->enabled) {
        memcpy(output_R, live_R, CIS_MAX_PIXELS_NB);
        memcpy(output_G, live_G, CIS_MAX_PIXELS_NB);
        memcpy(output_B, live_B, CIS_MAX_PIXELS_NB);
        pthread_mutex_unlock(&seq->mutex);
        return 0;
    }
    
    uint64_t current_time = get_time_us();
    int has_active_players = 0;
    
    // Initialize output accumulators (float for weighted sum)
    float *accum_R = (float *)calloc(CIS_MAX_PIXELS_NB, sizeof(float));
    float *accum_G = (float *)calloc(CIS_MAX_PIXELS_NB, sizeof(float));
    float *accum_B = (float *)calloc(CIS_MAX_PIXELS_NB, sizeof(float));
    float total_weight = 0.0f;
    
    if (!accum_R || !accum_G || !accum_B) {
        pthread_mutex_unlock(&seq->mutex);
        free(accum_R); free(accum_G); free(accum_B);
        return -1;
    }
    
    // Process each player
    for (int i = 0; i < seq->num_players; i++) {
        SequencePlayer *player = &seq->players[i];
        
        // Handle recording
        if (player->state == PLAYER_STATE_RECORDING) {
            if (player->recorded_frames < player->buffer_capacity) {
                memcpy(player->frames[player->recorded_frames].buffer_R, live_R, CIS_MAX_PIXELS_NB);
                memcpy(player->frames[player->recorded_frames].buffer_G, live_G, CIS_MAX_PIXELS_NB);
                memcpy(player->frames[player->recorded_frames].buffer_B, live_B, CIS_MAX_PIXELS_NB);
                player->frames[player->recorded_frames].timestamp_us = current_time;
                player->recorded_frames++;
            } else {
                // Buffer full, auto-stop recording
                player->state = PLAYER_STATE_READY;
                printf("[SEQ] Player %d: Buffer full, stopped recording\n", i);
            }
        }
        
        // Handle playback
        if (player->state == PLAYER_STATE_PLAYING || player->state == PLAYER_STATE_STOPPED) {
            if (player->recorded_frames == 0) continue;
            
            // Calculate envelope level
            float env_level = calculate_adsr_level(&player->envelope, current_time);
            
            // If envelope finished in stopped state, mark as ready
            if (player->state == PLAYER_STATE_STOPPED && env_level == 0.0f) {
                player->state = PLAYER_STATE_READY;
                continue;
            }
            
            // Get current frame with interpolation
            int frame_idx = (int)player->playback_position;
            float frac = player->playback_position - (float)frame_idx;
            
            // Clamp to valid range
            if (frame_idx < 0) frame_idx = 0;
            if (frame_idx >= player->recorded_frames) {
                // Handle loop modes
                if (player->loop_mode == LOOP_MODE_SIMPLE) {
                    frame_idx = 0;
                    player->playback_position = 0.0f;
                } else if (player->loop_mode == LOOP_MODE_PINGPONG) {
                    player->playback_direction *= -1;
                    frame_idx = player->recorded_frames - 1;
                    player->playback_position = (float)frame_idx;
                } else { // LOOP_MODE_ONESHOT
                    player->state = PLAYER_STATE_STOPPED;
                    player->envelope.release_time_us = current_time;
                    continue;
                }
            }
            
            // Get player frame RGB (with interpolation if needed)
            uint8_t *player_R, *player_G, *player_B;
            uint8_t temp_R[CIS_MAX_PIXELS_NB], temp_G[CIS_MAX_PIXELS_NB], temp_B[CIS_MAX_PIXELS_NB];
            
            if (frac > 0.001f && frame_idx + 1 < player->recorded_frames) {
                // Interpolate between two frames
                blend_rgb_frames(temp_R, temp_G, temp_B,
                               player->frames[frame_idx].buffer_R,
                               player->frames[frame_idx].buffer_G,
                               player->frames[frame_idx].buffer_B,
                               player->frames[frame_idx + 1].buffer_R,
                               player->frames[frame_idx + 1].buffer_G,
                               player->frames[frame_idx + 1].buffer_B,
                               frac, CIS_MAX_PIXELS_NB);
                player_R = temp_R;
                player_G = temp_G;
                player_B = temp_B;
            } else {
                // Use current frame directly
                player_R = player->frames[frame_idx].buffer_R;
                player_G = player->frames[frame_idx].buffer_G;
                player_B = player->frames[frame_idx].buffer_B;
            }
            
            // Apply envelope and blend level
            float total_level = env_level * player->blend_level;
            
            // Accumulate into output
            for (int p = 0; p < CIS_MAX_PIXELS_NB; p++) {
                accum_R[p] += player_R[p] * total_level;
                accum_G[p] += player_G[p] * total_level;
                accum_B[p] += player_B[p] * total_level;
            }
            total_weight += total_level;
            
            has_active_players = 1;
            
            // Advance playback position
            player->playback_position += player->playback_speed * player->playback_direction;
        }
    }
    
    // Mix with live input and write output
    if (has_active_players) {
        // Normalize accumulated values and blend with live
        float seq_weight = 1.0f - seq->live_mix_level;
        float live_weight = seq->live_mix_level;
        
        // If we have active players, normalize and blend
        if (total_weight > 0.0f) {
            for (int p = 0; p < CIS_MAX_PIXELS_NB; p++) {
                // Normalize sequencer output
                float norm_R = accum_R[p] / total_weight;
                float norm_G = accum_G[p] / total_weight;
                float norm_B = accum_B[p] / total_weight;
                
                // Mix with live
                float mixed_R = norm_R * seq_weight + live_R[p] * live_weight;
                float mixed_G = norm_G * seq_weight + live_G[p] * live_weight;
                float mixed_B = norm_B * seq_weight + live_B[p] * live_weight;
                
                // Clamp and write output
                output_R[p] = clamp_u8((int)mixed_R);
                output_G[p] = clamp_u8((int)mixed_G);
                output_B[p] = clamp_u8((int)mixed_B);
            }
        } else {
            // No weight, just pass through live
            memcpy(output_R, live_R, CIS_MAX_PIXELS_NB);
            memcpy(output_G, live_G, CIS_MAX_PIXELS_NB);
            memcpy(output_B, live_B, CIS_MAX_PIXELS_NB);
        }
    } else {
        // No active players, just pass through live
        memcpy(output_R, live_R, CIS_MAX_PIXELS_NB);
        memcpy(output_G, live_G, CIS_MAX_PIXELS_NB);
        memcpy(output_B, live_B, CIS_MAX_PIXELS_NB);
    }
    
    // Cleanup
    free(accum_R);
    free(accum_G);
    free(accum_B);
    
    // Update statistics
    seq->frames_processed++;
    uint64_t process_time = get_time_us() - start_time;
    seq->total_process_time_us += process_time;
    
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
    printf("Live Mix Level: %.2f\n", seq->live_mix_level);
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
        }
        printf("Player %d: %s (%d frames)\n", i, state_str, player->recorded_frames);
    }
    printf("============================================\n\n");
    
    pthread_mutex_unlock(&seq->mutex);
}
