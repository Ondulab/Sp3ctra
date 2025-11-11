#ifndef IMAGE_SEQUENCER_H
#define IMAGE_SEQUENCER_H

#include <stdint.h>
#include <pthread.h>
#include "../core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration constants */
#define MAX_SEQUENCE_DURATION_S 10.0f
#define MAX_SEQUENCE_FRAMES (int)(MAX_SEQUENCE_DURATION_S * 1000) // 10000 frames max
#define DEFAULT_NUM_PLAYERS 4

/* Raw RGB image frame structure - lightweight storage (10.4 KB/frame) */
typedef struct {
    uint8_t *buffer_R;       // Red channel [CIS_MAX_PIXELS_NB] pixels
    uint8_t *buffer_G;       // Green channel [CIS_MAX_PIXELS_NB] pixels
    uint8_t *buffer_B;       // Blue channel [CIS_MAX_PIXELS_NB] pixels
    uint64_t timestamp_us;   // Microsecond timestamp
} RawImageFrame;

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
    BLEND_MODE_MIX,          // Weighted average (classic crossfade)
    BLEND_MODE_ADD,          // Additive blend (both at max in center)
    BLEND_MODE_SCREEN,       // Screen blend (brightens, like Photoshop)
    BLEND_MODE_MASK          // Multiplicative masking (darkens)
} BlendMode;

/* Playback direction */
#define PLAYBACK_DIRECTION_FORWARD  1
#define PLAYBACK_DIRECTION_REVERSE -1

/* ADSR envelope for presence shaping (positional, not temporal) */
typedef struct {
    /* Parameters (as ratios of sequence length [0.0, 1.0]) */
    float attack_ratio;      // Attack phase duration (% of sequence)
    float decay_ratio;       // Decay phase duration (% of sequence)
    float sustain_level;     // Sustain level [0.0, 1.0]
    float release_ratio;     // Release phase duration (% of sequence)
    
    /* Runtime state */
    float current_level;     // Current envelope output [0.0, 1.0]
} ADSREnvelope;

/* Sequence player (one per sequence) */
typedef struct {
    /* Sequence storage (ring buffer) - NOW STORES RGB RAW DATA */
    RawImageFrame *frames;           // Statically allocated at init (10.4 KB/frame)
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
    
    /* Mix parameters */
    float exposure;                  // Exposure control: 0.0=underexposed, 0.5=normal, 1.0=blown out
    float brightness;                // Brightness/saturation boost [0.5, 2.0] (default 1.0)
    int mix_enabled;                 // Enable/disable in mix (0=off, 1=on)
    
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
    
    /* Output buffer (reused every frame) - MIXED RGB OUTPUT */
    RawImageFrame output_frame;      // Holds the mixed RGB result
    
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
void image_sequencer_set_exposure(ImageSequencer *seq, int player_id, float level);
void image_sequencer_set_brightness(ImageSequencer *seq, int player_id, float brightness);
void image_sequencer_set_mix_enabled(ImageSequencer *seq, int player_id, int enabled);
void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction);

/* Player state control */
int image_sequencer_mute_player(ImageSequencer *seq, int player_id);
int image_sequencer_unmute_player(ImageSequencer *seq, int player_id);
int image_sequencer_toggle_mute(ImageSequencer *seq, int player_id);

/* ADSR envelope control (positional) */
void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, 
                              float attack_ratio, float decay_ratio, 
                              float sustain_level, float release_ratio);
void image_sequencer_set_attack(ImageSequencer *seq, int player_id, float attack_ratio);
void image_sequencer_set_decay(ImageSequencer *seq, int player_id, float decay_ratio);
void image_sequencer_set_sustain(ImageSequencer *seq, int player_id, float sustain_level);
void image_sequencer_set_release(ImageSequencer *seq, int player_id, float release_ratio);

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

/* Main processing function (called from UDP thread)
 * 
 * NEW BEHAVIOR: Takes RGB raw input, mixes with sequences, outputs RGB raw
 * The output RGB will then be preprocessed (grayscale, pan, DMX calculation)
 * 
 * Parameters:
 *   seq: The sequencer instance
 *   live_R/G/B: Live RGB input from UDP (CIS_MAX_PIXELS_NB pixels each)
 *   output_R/G/B: Mixed RGB output (CIS_MAX_PIXELS_NB pixels each)
 *   
 * Returns: 0 on success, -1 on error
 */
int image_sequencer_process_frame(
    ImageSequencer *seq,
    const uint8_t *live_R,
    const uint8_t *live_G,
    const uint8_t *live_B,
    uint8_t *output_R,
    uint8_t *output_G,
    uint8_t *output_B
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
