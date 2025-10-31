#include "image_sequencer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

// Forward declarations
static void copy_frame(PreprocessedImageData *dst, const PreprocessedImageData *src);
static void blend_frames(PreprocessedImageData *output, const PreprocessedImageData *a, const PreprocessedImageData *b, float blend);
static float calculate_adsr_level(ADSREnvelope *env, uint64_t current_time);
static inline float clamp_f(float value, float min, float max);

// Forward declaration for process function
static void image_sequencer_process_internal(ImageSequencer *seq, PreprocessedImageData *output, const PreprocessedImageData *live_input);

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

// Copy preprocessed data
static void copy_frame(PreprocessedImageData *dst, const PreprocessedImageData *src) {
    memcpy(dst, src, sizeof(PreprocessedImageData));
}

// Interpolate between two frames
static void blend_frames(PreprocessedImageData *output,
                        const PreprocessedImageData *a,
                        const PreprocessedImageData *b,
                        float blend) {
    for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
        output->grayscale[i] = lerp(a->grayscale[i], b->grayscale[i], blend);
    }
    output->contrast_factor = lerp(a->contrast_factor, b->contrast_factor, blend);
}

// Utility: Clamp float value
static inline float clamp_f(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Initialize ADSR envelope
static void init_adsr_envelope(ADSREnvelope *env) {
    memset(env, 0, sizeof(ADSREnvelope));
    env->attack_ms = 100.0f;
    env->decay_ms = 50.0f;
    env->sustain_level = 0.7f;
    env->release_ms = 200.0f;
    env->is_triggered = 0;
    env->current_level = 0.0f;
}

// Initialize sequence player
static void init_sequence_player(SequencePlayer *player, int buffer_capacity) {
    memset(player, 0, sizeof(SequencePlayer));
    player->state = PLAYER_STATE_IDLE;
    player->buffer_capacity = buffer_capacity;
    player->frames = (PreprocessedImageData *)calloc(buffer_capacity, sizeof(PreprocessedImageData));
    
    if (!player->frames) {
        fprintf(stderr, "[ERROR] Failed to allocate frame buffer\n");
        return;
    }
    
    player->playback_speed = 1.0f;
    player->playback_direction = 1;
    player->blend_level = 1.0f;
    player->loop_mode = LOOP_MODE_SIMPLE;
    player->trigger_mode = TRIGGER_MODE_MANUAL;
    
    init_adsr_envelope(&player->envelope);
}

// Public API Implementation

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
    
    int buffer_capacity = (int)(max_duration_s * 1000);
    for (int i = 0; i < num_players; i++) {
        init_sequence_player(&seq->players[i], buffer_capacity);
    }
    
    printf("[INFO] Image Sequencer created: %d players\n", num_players);
    return seq;
}

void image_sequencer_destroy(ImageSequencer *seq) {
    if (!seq) return;
    
    if (seq->players) {
        for (int i = 0; i < seq->num_players; i++) {
            if (seq->players[i].frames) {
                free(seq->players[i].frames);
            }
        }
        free(seq->players);
    }
    
    pthread_mutex_destroy(&seq->mutex);
    free(seq);
    printf("[INFO] Image Sequencer destroyed\n");
}

// Recording Control
int image_sequencer_start_recording(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    // Can only start recording from IDLE or READY state
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
    
    // Can only start from READY, STOPPED, or MUTED
    if (player->state != PLAYER_STATE_READY && 
        player->state != PLAYER_STATE_STOPPED &&
        player->state != PLAYER_STATE_MUTED) {
        pthread_mutex_unlock(&seq->mutex);
        return -1;
    }
    
    if (player->recorded_frames == 0) {
        pthread_mutex_unlock(&seq->mutex);
        return -1; // No frames to play
    }
    
    player->state = PLAYER_STATE_PLAYING;
    player->playback_position = 0.0f;
    
    // Trigger envelope
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
    
    // Trigger release phase
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

// Playback Parameters
void image_sequencer_set_playback_speed(ImageSequencer *seq, int player_id, float speed) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    speed = clamp_f(speed, 0.1f, 10.0f);
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].playback_speed = speed;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].playback_direction = (direction >= 0) ? 1 : -1;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_loop_mode(ImageSequencer *seq, int player_id, LoopMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].loop_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_quantize(ImageSequencer *seq, int player_id, int enabled, int beats) {
    (void)seq; (void)player_id; (void)enabled; (void)beats;
    // TODO: Implement quantization (MIDI sync feature)
}

void image_sequencer_set_blend_level(ImageSequencer *seq, int player_id, float level) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    level = clamp_f(level, 0.0f, 1.0f);
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].blend_level = level;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_auto_play(ImageSequencer *seq, int player_id, int enabled) {
    (void)seq; (void)player_id; (void)enabled;
    // Note: auto_play is handled via trigger_mode (TRIGGER_MODE_AUTO)
}

// Mute Control
int image_sequencer_mute_player(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return -1;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    
    if (player->state == PLAYER_STATE_PLAYING) {
        player->state = PLAYER_STATE_MUTED;
        // Trigger release
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
        // Trigger attack
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

// Envelope Control
void image_sequencer_set_envelope(ImageSequencer *seq, int player_id, float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    // Clamp values to reasonable ranges
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

void image_sequencer_set_enabled(ImageSequencer *seq, int enabled) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->enabled = enabled;
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

void image_sequencer_set_midi_sync(ImageSequencer *seq, int enabled) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->midi_clock_sync = enabled;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_midi_clock_tick(ImageSequencer *seq) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->last_clock_us = get_time_us();
    pthread_mutex_unlock(&seq->mutex);
}

// Additional API functions to match header
void image_sequencer_set_speed(ImageSequencer *seq, int player_id, float speed) {
    image_sequencer_set_playback_speed(seq, player_id, speed);
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

void image_sequencer_set_trigger_mode(ImageSequencer *seq, int player_id, TriggerMode mode) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    seq->players[player_id].trigger_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_set_blend_mode(ImageSequencer *seq, BlendMode mode) {
    if (!seq) return;
    pthread_mutex_lock(&seq->mutex);
    seq->blend_mode = mode;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_enable_midi_sync(ImageSequencer *seq, int enable) {
    image_sequencer_set_midi_sync(seq, enable);
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

int image_sequencer_process_frame(ImageSequencer *seq, const PreprocessedImageData *live_input, PreprocessedImageData *output) {
    if (!seq || !live_input || !output) return -1;
    image_sequencer_process_internal(seq, output, live_input);
    return 0;
}

void image_sequencer_register_midi_callbacks(ImageSequencer *seq) {
    (void)seq;
    // TODO: Register MIDI callbacks for transport control
    // This will be implemented when MIDI integration is needed
}

void image_sequencer_get_stats(ImageSequencer *seq, uint64_t *frames_processed, float *avg_process_time_us) {
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

void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    image_sequencer_set_envelope(seq, player_id, attack_ms, decay_ms, sustain_level, release_ms);
}

void image_sequencer_clear_player(ImageSequencer *seq, int player_id) {
    if (!seq || player_id < 0 || player_id >= seq->num_players) return;
    
    pthread_mutex_lock(&seq->mutex);
    SequencePlayer *player = &seq->players[player_id];
    player->state = PLAYER_STATE_IDLE;
    player->recorded_frames = 0;
    player->playback_position = 0.0f;
    pthread_mutex_unlock(&seq->mutex);
}

void image_sequencer_clear_all(ImageSequencer *seq) {
    if (!seq) return;
    for (int i = 0; i < seq->num_players; i++) {
        image_sequencer_clear_player(seq, i);
    }
}

// Main processing function (internal implementation)
static void image_sequencer_process_internal(ImageSequencer *seq, PreprocessedImageData *output, const PreprocessedImageData *live_input) {
    if (!seq || !output || !live_input) return;
    
    pthread_mutex_lock(&seq->mutex);
    
    // If disabled, just pass through
    if (!seq->enabled) {
        copy_frame(output, live_input);
        pthread_mutex_unlock(&seq->mutex);
        return;
    }
    
    uint64_t current_time = get_time_us();
    int has_active_players = 0;
    
    // Initialize output to zeros
    memset(output, 0, sizeof(PreprocessedImageData));
    
    // Process each player
    for (int i = 0; i < seq->num_players; i++) {
        SequencePlayer *player = &seq->players[i];
        
        // Handle recording
        if (player->state == PLAYER_STATE_RECORDING) {
            if (player->recorded_frames < player->buffer_capacity) {
                copy_frame(&player->frames[player->recorded_frames], live_input);
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
            
            PreprocessedImageData player_frame;
            
            // Interpolate between frames if needed
            if (frac > 0.001f && frame_idx + 1 < player->recorded_frames) {
                blend_frames(&player_frame, 
                           &player->frames[frame_idx],
                           &player->frames[frame_idx + 1],
                           frac);
            } else {
                copy_frame(&player_frame, &player->frames[frame_idx]);
            }
            
            // Apply envelope and blend level
            float total_level = env_level * player->blend_level;
            
            // Mix into output
            for (int p = 0; p < CIS_MAX_PIXELS_NB; p++) {
                output->grayscale[p] += player_frame.grayscale[p] * total_level;
            }
            output->contrast_factor += player_frame.contrast_factor * total_level;
            
            has_active_players = 1;
            
            // Advance playback position
            player->playback_position += player->playback_speed * player->playback_direction;
        }
    }
    
    // Mix with live input
    if (has_active_players) {
        // Normalize and blend with live
        float seq_weight = 1.0f - seq->live_mix_level;
        float live_weight = seq->live_mix_level;
        
        for (int p = 0; p < CIS_MAX_PIXELS_NB; p++) {
            output->grayscale[p] = output->grayscale[p] * seq_weight + 
                                   live_input->grayscale[p] * live_weight;
            output->grayscale[p] = clamp_f(output->grayscale[p], 0.0f, 255.0f);
        }
        output->contrast_factor = output->contrast_factor * seq_weight + 
                                  live_input->contrast_factor * live_weight;
    } else {
        // No active players, just pass through live
        copy_frame(output, live_input);
    }
    
    seq->frames_processed++;
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
