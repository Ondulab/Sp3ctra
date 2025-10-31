#ifndef IMAGE_SEQUENCER_H
#define IMAGE_SEQUENCER_H

#include <stdint.h>
#include <pthread.h>
#include "../processing/image_preprocessor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration constants */
#define MAX_SEQUENCE_DURATION_S 10.0f
#define MAX_SEQUENCE_FRAMES (int)(MAX_SEQUENCE_DURATION_S * 1000) // 10000 frames max
#define DEFAULT_NUM_PLAYERS 5

/* Player state machine */
typedef enum {
    PLAYER_STATE_IDLE,       // No sequence loaded
    PLAYER_STATE_RECORDING,  // Recording from live
    PLAYER_STATE_READY,      // Sequence loaded, ready to play
    PLAYER_STATE_PLAYING,    // Active playback
    PLAYER_STATE_STOPPED,    // Paused but still in mix (frame frozen)
    PLAYER_STATE_MUTED       // Muted, removed from mix
} PlayerState;

/* Loop modes */
typedef enum {
    LOOP_MODE_SIMPLE,        // A→B→A→B...
    LOOP_MODE_PINGPONG,      // A→B→A→B→A...
    LOOP_MODE_ONESHOT        // A→B→[STOP]
} LoopMode;

/* Trigger modes */
typedef enum {
    TRIGGER_MODE_MANUAL,     // Manual start via MIDI/API
    TRIGGER_MODE_AUTO,       // Auto-start after recording
    TRIGGER_MODE_SYNC        // Sync to MIDI clock (quantized)
} TriggerMode;

/* Blend modes for mixing sequences */
typedef enum {
    BLEND_MODE_MIX,          // Weighted average
    BLEND_MODE_CROSSFADE,    // Linear interpolation
    BLEND_MODE_OVERLAY,      // Additive with clipping
    BLEND_MODE_MASK          // Multiplicative masking
} BlendMode;

/* Playback direction */
#define PLAYBACK_DIRECTION_FORWARD  1
#define PLAYBACK_DIRECTION_REVERSE -1

/* ADSR envelope for volume shaping */
typedef struct {
    /* Parameters (in milliseconds) */
    float attack_ms;
    float decay_ms;
    float sustain_level;     // [0.0, 1.0]
    float release_ms;
    
    /* Runtime state */
    float current_level;     // Current envelope output [0.0, 1.0]
    uint64_t trigger_time_us;
    uint64_t release_time_us;
    int is_triggered;        // 1 = attack/sustain phase, 0 = release phase
} ADSREnvelope;

/* Sequence player (one per sequence) */
typedef struct {
    /* Sequence storage (ring buffer) */
    PreprocessedImageData *frames;  // Statically allocated at init
    int buffer_capacity;             // Max frames (e.g., 5000 for 5s @ 1000fps)
    int recorded_frames;             // Actual recorded frames
    
    /* Playback control */
    float playback_position;         // Current position (float for fractional speeds)
    float playback_speed;            // Speed multiplier [0.1, 10.0]
    int playback_offset;             // Start offset in frames
    int playback_direction;          // 1 = forward, -1 = backward
    
    /* State and modes */
    PlayerState state;
    LoopMode loop_mode;
    TriggerMode trigger_mode;
    
    /* Envelope */
    ADSREnvelope envelope;
    
    /* Mix level */
    float blend_level;               // Player's contribution to mix [0.0, 1.0]
    
} SequencePlayer;

/* Main sequencer structure */
typedef struct {
    /* Players array */
    SequencePlayer *players;         // Array of players (static allocation)
    int num_players;                 // Number of players (e.g., 5)
    
    /* Global mix control */
    BlendMode blend_mode;            // Current blending mode
    float live_mix_level;            // Live input mix level [0.0, 1.0]
    
    /* MIDI clock sync */
    float bpm;                       // Current BPM (from MIDI or manual)
    int midi_clock_sync;             // 1 = sync to MIDI clock, 0 = free-running
    uint64_t last_clock_us;          // Last MIDI clock tick timestamp
    
    /* Output buffer (reused every frame) */
    PreprocessedImageData output_frame;
    
    /* Thread safety */
    pthread_mutex_t mutex;           // Protects all state (lightweight, < 10us)
    
    /* Statistics */
    uint64_t frames_processed;
    uint64_t total_process_time_us;
    
    /* Configuration */
    float max_duration_s;            // Max duration per sequence
    int enabled;                     // Module enable/disable flag
    
} ImageSequencer;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/* Initialization and cleanup */
ImageSequencer* image_sequencer_create(int num_players, float max_duration_s);
void image_sequencer_destroy(ImageSequencer *seq);

/* Player control - Recording */
int image_sequencer_start_recording(ImageSequencer *seq, int player_id);
int image_sequencer_stop_recording(ImageSequencer *seq, int player_id);

/* Player control - Playback */
int image_sequencer_start_playback(ImageSequencer *seq, int player_id);
int image_sequencer_stop_playback(ImageSequencer *seq, int player_id);
int image_sequencer_toggle_playback(ImageSequencer *seq, int player_id);

/* Player parameters */
void image_sequencer_set_speed(ImageSequencer *seq, int player_id, float speed);
void image_sequencer_set_offset(ImageSequencer *seq, int player_id, int offset_frames);
void image_sequencer_set_loop_mode(ImageSequencer *seq, int player_id, LoopMode mode);
void image_sequencer_set_trigger_mode(ImageSequencer *seq, int player_id, TriggerMode mode);
void image_sequencer_set_blend_level(ImageSequencer *seq, int player_id, float level);
void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction);

/* Player state control */
int image_sequencer_mute_player(ImageSequencer *seq, int player_id);
int image_sequencer_unmute_player(ImageSequencer *seq, int player_id);
int image_sequencer_toggle_mute(ImageSequencer *seq, int player_id);

/* ADSR control */
void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, 
                              float attack_ms, float decay_ms, 
                              float sustain_level, float release_ms);
void image_sequencer_trigger_envelope(ImageSequencer *seq, int player_id);
void image_sequencer_release_envelope(ImageSequencer *seq, int player_id);

/* Global control */
void image_sequencer_set_enabled(ImageSequencer *seq, int enabled);
void image_sequencer_set_blend_mode(ImageSequencer *seq, BlendMode mode);
void image_sequencer_set_live_mix_level(ImageSequencer *seq, float level);
void image_sequencer_set_bpm(ImageSequencer *seq, float bpm);
void image_sequencer_enable_midi_sync(ImageSequencer *seq, int enable);

/* MIDI clock integration */
void image_sequencer_midi_clock_tick(ImageSequencer *seq);
void image_sequencer_midi_clock_start(ImageSequencer *seq);
void image_sequencer_midi_clock_stop(ImageSequencer *seq);

/* Main processing function (called from UDP thread or dedicated thread) */
int image_sequencer_process_frame(
    ImageSequencer *seq,
    const PreprocessedImageData *live_input,
    PreprocessedImageData *output
);

/* MIDI callback registration */
void image_sequencer_register_midi_callbacks(ImageSequencer *seq);

/* Statistics and debugging */
void image_sequencer_get_stats(ImageSequencer *seq, 
                               uint64_t *frames_processed, 
                               float *avg_process_time_us);
PlayerState image_sequencer_get_player_state(ImageSequencer *seq, int player_id);
void image_sequencer_print_status(ImageSequencer *seq);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_SEQUENCER_H */
