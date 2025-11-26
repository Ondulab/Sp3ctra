/**
 * @file voice_manager.h
 * @brief Generic polyphonic voice management for synthesis engines
 * 
 * This module provides a unified voice allocation/release system that can be
 * used by different synthesis engines (LuxWave, LuxSynth, etc.) to manage
 * polyphonic voices consistently and avoid race conditions.
 * 
 * Key features:
 * - RT-safe voice allocation with 3-priority system (IDLE → RELEASE quietest → ACTIVE oldest)
 * - RT-safe voice release with grace period for late Note Off messages
 * - Automatic cleanup of IDLE voices to prevent stuck notes
 * - Engine-agnostic design using callbacks for flexibility
 * 
 * @note All functions are RT-safe (no allocations, no blocking operations)
 */

#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "synth_common.h"  /* For AdsrState enum */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VOICE METADATA STRUCTURE
 * ========================================================================== */

/**
 * @brief Generic voice metadata for polyphonic voice management
 * 
 * This structure contains ONLY the metadata needed for voice allocation,
 * not the synthesis-specific data (oscillators, filters, etc.)
 * 
 * Each synthesis engine provides callbacks to extract this metadata from
 * their own voice structures.
 */
typedef struct {
    int midi_note;              /**< MIDI note number (-1 = inactive, 0-127 = active) */
    unsigned long long trigger_order;  /**< Trigger order for LRU voice stealing */
    AdsrState adsr_state;       /**< Current ADSR state (for priority detection) */
    float adsr_output;          /**< Current envelope output (for quietest-release detection) */
} VoiceMetadata;

/* ============================================================================
 * CALLBACK FUNCTION TYPES
 * ========================================================================== */

/**
 * @brief Callback to extract voice metadata from engine-specific voice structure
 * 
 * @param voices Pointer to engine's voice array
 * @param voice_idx Index of the voice to query
 * @return VoiceMetadata structure with current voice state
 */
typedef VoiceMetadata (*GetVoiceMetadataFn)(void *voices, int voice_idx);

/**
 * @brief Callback to set MIDI note number in engine-specific voice structure
 * 
 * @param voices Pointer to engine's voice array
 * @param voice_idx Index of the voice to modify
 * @param midi_note MIDI note number to set (-1 = clear, 0-127 = active note)
 */
typedef void (*SetVoiceNoteFn)(void *voices, int voice_idx, int midi_note);

/**
 * @brief Callback to get ADSR state from engine-specific voice structure
 * 
 * @param voices Pointer to engine's voice array
 * @param voice_idx Index of the voice to query
 * @return Current ADSR state
 */
typedef AdsrState (*GetVoiceStateFn)(void *voices, int voice_idx);

/* ============================================================================
 * VOICE ALLOCATION
 * ========================================================================== */

/**
 * @brief Allocate a voice for a new MIDI note using 3-priority system
 * 
 * Priority order:
 * 1. Find an IDLE voice (highest priority - no interruption)
 * 2. Steal the RELEASE voice with lowest envelope output (quietest)
 * 3. Steal the ACTIVE voice with oldest trigger order (LRU)
 * 
 * This function does NOT modify the voice structure directly - it only returns
 * the index. The caller must initialize the voice (frequency, ADSR, etc.)
 * 
 * @param voices Pointer to engine's voice array
 * @param num_voices Number of voices in the array
 * @param get_metadata Callback to extract voice metadata
 * @param set_note Callback to set MIDI note (used for logging only)
 * @param midi_note MIDI note number to allocate (0-127)
 * @param trigger_order Current global trigger order counter
 * @return Index of allocated voice (0 to num_voices-1), or -1 if allocation failed
 * 
 * @note RT-safe: no allocations, no blocking operations
 */
int voice_manager_allocate(void *voices, int num_voices,
                           GetVoiceMetadataFn get_metadata,
                           SetVoiceNoteFn set_note,
                           int midi_note,
                           unsigned long long trigger_order);

/* ============================================================================
 * VOICE RELEASE
 * ========================================================================== */

/**
 * @brief Release a voice for a MIDI Note Off with grace period for late messages
 * 
 * Priority order:
 * 1. Find the OLDEST ACTIVE voice with this note (not in RELEASE or IDLE)
 * 2. Find a RELEASE voice with this note (duplicate/late Note Off)
 * 3. Find an IDLE voice with this note (very late Note Off - grace period)
 * 
 * This function does NOT trigger ADSR release - it only finds the voice.
 * The caller must call adsr_trigger_release() on the returned voice.
 * 
 * After processing, the function clears the midi_note to prevent future
 * late Note Offs from finding this voice again.
 * 
 * @param voices Pointer to engine's voice array
 * @param num_voices Number of voices in the array
 * @param get_metadata Callback to extract voice metadata
 * @param get_state Callback to get ADSR state
 * @param set_note Callback to set MIDI note (clears after processing)
 * @param midi_note MIDI note number to release (0-127)
 * @return Index of voice to release (0 to num_voices-1), or -1 if not found
 * 
 * @note RT-safe: no allocations, no blocking operations
 */
int voice_manager_release(void *voices, int num_voices,
                          GetVoiceMetadataFn get_metadata,
                          GetVoiceStateFn get_state,
                          SetVoiceNoteFn set_note,
                          int midi_note);

/* ============================================================================
 * VOICE CLEANUP
 * ========================================================================== */

/**
 * @brief Clean up IDLE voices by clearing their MIDI note numbers
 * 
 * This function should be called periodically (e.g., in the audio processing loop)
 * to clear midi_note from voices that have reached IDLE state. This prevents
 * stuck notes and ensures voices are ready for reallocation.
 * 
 * @param voices Pointer to engine's voice array
 * @param num_voices Number of voices in the array
 * @param get_metadata Callback to extract voice metadata
 * @param set_note Callback to set MIDI note (clears IDLE voices)
 * 
 * @note RT-safe: no allocations, no blocking operations
 * @note This function is typically called once per audio buffer in the processing loop
 */
void voice_manager_cleanup_idle(void *voices, int num_voices,
                                GetVoiceMetadataFn get_metadata,
                                SetVoiceNoteFn set_note);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_MANAGER_H */
