/**
 * @file voice_manager.c
 * @brief Implementation of generic polyphonic voice management
 */

#include "voice_manager.h"
#include <stdio.h>

/* ============================================================================
 * VOICE ALLOCATION
 * ========================================================================== */

int voice_manager_allocate(void *voices, int num_voices,
                           GetVoiceMetadataFn get_metadata,
                           SetVoiceNoteFn set_note,
                           int midi_note,
                           unsigned long long trigger_order) {
    int voice_idx = -1;
    int i;
    
    if (!voices || !get_metadata || num_voices <= 0) {
        return -1;
    }
    
    /* Priority 1: Find an IDLE voice (highest priority - no interruption) */
    for (i = 0; i < num_voices; i++) {
        VoiceMetadata meta = get_metadata(voices, i);
        if (meta.adsr_state == ADSR_STATE_IDLE) {
            voice_idx = i;
            break;
        }
    }
    
    /* Priority 2: Steal the RELEASE voice with lowest envelope output (quietest) */
    if (voice_idx == -1) {
        float lowest_env_output = 2.0f; /* Greater than max envelope output (1.0) */
        for (i = 0; i < num_voices; i++) {
            VoiceMetadata meta = get_metadata(voices, i);
            if (meta.adsr_state == ADSR_STATE_RELEASE) {
                if (meta.adsr_output < lowest_env_output) {
                    lowest_env_output = meta.adsr_output;
                    voice_idx = i;
                }
            }
        }
    }
    
    /* Priority 3: Steal the ACTIVE voice with oldest trigger order (LRU) */
    if (voice_idx == -1) {
        unsigned long long oldest_order = trigger_order + 1; /* Initialize with newest possible */
        for (i = 0; i < num_voices; i++) {
            VoiceMetadata meta = get_metadata(voices, i);
            /* ACTIVE = not RELEASE and not IDLE (i.e., ATTACK, DECAY, SUSTAIN) */
            if (meta.adsr_state != ADSR_STATE_RELEASE &&
                meta.adsr_state != ADSR_STATE_IDLE) {
                if (meta.trigger_order < oldest_order) {
                    oldest_order = meta.trigger_order;
                    voice_idx = i;
                }
            }
        }
    }
    
    /* Fallback: If absolutely no voice found, steal voice 0 */
    if (voice_idx == -1) {
        voice_idx = 0;
        printf("VOICE_MGR: Critical fallback - stealing voice 0 for note %d\n", midi_note);
    }
    
    return voice_idx;
}

/* ============================================================================
 * VOICE RELEASE
 * ========================================================================== */

int voice_manager_release(void *voices, int num_voices,
                          GetVoiceMetadataFn get_metadata,
                          GetVoiceStateFn get_state,
                          SetVoiceNoteFn set_note,
                          int midi_note) {
    int oldest_voice_idx = -1;
    unsigned long long oldest_order;
    int i;
    
    if (!voices || !get_metadata || !get_state || !set_note || num_voices <= 0) {
        return -1;
    }
    
    oldest_order = (unsigned long long)-1; /* Maximum possible value */
    
    /* Priority 1: Find the OLDEST ACTIVE voice with this note (not in RELEASE or IDLE) */
    for (i = 0; i < num_voices; i++) {
        VoiceMetadata meta = get_metadata(voices, i);
        if (meta.midi_note == midi_note &&
            meta.adsr_state != ADSR_STATE_IDLE &&
            meta.adsr_state != ADSR_STATE_RELEASE) {
            /* Find the oldest (lowest trigger order) voice with this note */
            if (meta.trigger_order < oldest_order) {
                oldest_order = meta.trigger_order;
                oldest_voice_idx = i;
            }
        }
    }
    
    /* Priority 2: If no active voice found, search in RELEASE voices (duplicate/late Note Off) */
    if (oldest_voice_idx == -1) {
        for (i = 0; i < num_voices; i++) {
            VoiceMetadata meta = get_metadata(voices, i);
            if (meta.midi_note == midi_note &&
                meta.adsr_state == ADSR_STATE_RELEASE) {
                oldest_voice_idx = i;
                /* Duplicate Note Off detected - clear midi_note and return */
                set_note(voices, i, -1);
                return -1; /* Return -1 to indicate duplicate (no ADSR trigger needed) */
            }
        }
    }
    
    /* Priority 3: If still not found, search in IDLE voices (very late Note Off - grace period) */
    if (oldest_voice_idx == -1) {
        for (i = 0; i < num_voices; i++) {
            VoiceMetadata meta = get_metadata(voices, i);
            if (meta.midi_note == midi_note &&
                meta.adsr_state == ADSR_STATE_IDLE) {
                oldest_voice_idx = i;
                /* Late Note Off detected - clear midi_note and return */
                set_note(voices, i, -1);
                return -1; /* Return -1 to indicate late Note Off (no ADSR trigger needed) */
            }
        }
    }
    
    /* If voice found, clear midi_note AFTER caller processes the release */
    /* The caller will check the return value and trigger ADSR release if >= 0 */
    if (oldest_voice_idx != -1) {
        /* Clear midi_note to prevent future late Note Offs from finding this voice */
        set_note(voices, oldest_voice_idx, -1);
    }
    
    return oldest_voice_idx;
}

/* ============================================================================
 * VOICE CLEANUP
 * ========================================================================== */

void voice_manager_cleanup_idle(void *voices, int num_voices,
                                GetVoiceMetadataFn get_metadata,
                                SetVoiceNoteFn set_note) {
    int i;
    
    if (!voices || !get_metadata || !set_note || num_voices <= 0) {
        return;
    }
    
    /* Clear midi_note from all IDLE voices */
    for (i = 0; i < num_voices; i++) {
        VoiceMetadata meta = get_metadata(voices, i);
        if (meta.adsr_state == ADSR_STATE_IDLE && meta.midi_note != -1 && meta.midi_note != 0) {
            set_note(voices, i, -1);
        }
    }
}
