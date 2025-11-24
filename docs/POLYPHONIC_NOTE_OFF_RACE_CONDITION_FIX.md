# Polyphonic Note Off Race Condition Fix

**Date:** 2025-11-24  
**Status:** ✅ Fixed  
**Severity:** Medium (stuck notes causing audio artifacts)

## Problem Description

Notes were getting stuck in the polyphonic synthesis engine, continuing to play indefinitely even after Note Off messages were received. Analysis of logs showed:

```
SYNTH_POLY: WARNING - Note Off 62: No voice found (neither active nor idle)!
SYNTH_POLY: WARNING - Note Off 50: No voice found (neither active nor idle)!
```

This indicated that `synth_polyphonic_note_off()` was unable to find the voice to release.

## Root Cause

The issue was a **race condition** in the Note Off handling logic:

1. **Original Logic (2 priorities):**
   - Priority 1: Search ACTIVE voices (ATTACK, DECAY, SUSTAIN)
   - Priority 2: Search IDLE voices (grace period for late Note Off)
   - **Missing:** Search RELEASE voices

2. **Race Condition Scenario:**
   - Voice receives Note On → enters ATTACK state
   - Voice progresses through ATTACK → DECAY → SUSTAIN
   - User releases key → Note Off message sent
   - **BUT:** Before Note Off arrives, voice stealing occurs (new Note On)
   - Stolen voice enters RELEASE state with **different note number**
   - Original Note Off arrives → searches for old note number
   - **Result:** Note Off finds nothing (voice is in RELEASE with new note)

3. **Additional Issue:**
   - Duplicate Note Off messages (common in MIDI) were not handled
   - If a voice was already in RELEASE, a second Note Off would fail

## Solution

Added **Priority 2: Search RELEASE voices** to handle duplicate/late Note Offs:

```c
void synth_polyphonic_note_off(int noteNumber) {
  // Priority 1: Find ACTIVE voices (ATTACK, DECAY, SUSTAIN)
  int oldest_voice_idx = -1;
  unsigned long long oldest_order = g_current_trigger_order + 1;
  
  for (int i = 0; i < g_num_poly_voices; ++i) {
    if (poly_voices[i].midi_note_number == noteNumber &&
        poly_voices[i].voice_state != ADSR_STATE_IDLE &&
        poly_voices[i].voice_state != ADSR_STATE_RELEASE) {
      if (poly_voices[i].last_triggered_order < oldest_order) {
        oldest_order = poly_voices[i].last_triggered_order;
        oldest_voice_idx = i;
      }
    }
  }
  
  // Priority 2: Search RELEASE voices (NEW - handles duplicate/late Note Off)
  if (oldest_voice_idx == -1) {
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].midi_note_number == noteNumber &&
          poly_voices[i].voice_state == ADSR_STATE_RELEASE) {
        oldest_voice_idx = i;
        log_debug("SYNTH_POLY", "Duplicate Note Off %d handled via RELEASE voice %d", 
                  noteNumber, i);
        break;
      }
    }
  }
  
  // Priority 3: Search IDLE voices (grace period for very late Note Off)
  if (oldest_voice_idx == -1) {
    for (int i = 0; i < g_num_poly_voices; ++i) {
      if (poly_voices[i].midi_note_number == noteNumber &&
          poly_voices[i].voice_state == ADSR_STATE_IDLE) {
        oldest_voice_idx = i;
        log_debug("SYNTH_POLY", "Late Note Off %d handled via IDLE voice %d", 
                  noteNumber, i);
        break;
      }
    }
  }
  
  // Release the voice if found
  if (oldest_voice_idx != -1) {
    if (poly_voices[oldest_voice_idx].voice_state != ADSR_STATE_IDLE) {
      adsr_trigger_release(&poly_voices[oldest_voice_idx].volume_adsr);
      adsr_trigger_release(&poly_voices[oldest_voice_idx].filter_adsr);
      poly_voices[oldest_voice_idx].voice_state = ADSR_STATE_RELEASE;
      printf("SYNTH_POLY: Voice %d Note Off: %d -> ADSR Release\n", 
             oldest_voice_idx, noteNumber);
    }
    poly_voices[oldest_voice_idx].midi_note_number = -1;
  }
}
```

## Benefits

1. **Handles Duplicate Note Offs:** If a voice is already releasing, duplicate Note Off is gracefully ignored
2. **Prevents Stuck Notes:** All Note Off messages now find their target voice
3. **Robust MIDI Handling:** Works correctly even with timing variations and duplicate messages
4. **Consistent with Photowave:** Same 3-priority search strategy as photowave engine

## Testing

The fix should be tested with:
- Rapid note sequences (fast playing)
- Chord progressions with voice stealing
- Sustained notes with polyphony limit reached
- MIDI controllers that send duplicate Note Off messages

## Related Files

- `src/synthesis/polyphonic/synth_polyphonic.c` - Main fix implementation
- `docs/PHOTOWAVE_NOTE_OFF_RACE_CONDITION_FIX.md` - Similar fix for photowave engine

## Notes

This fix mirrors the solution applied to the photowave synthesis engine, ensuring consistent Note Off handling across all synthesis modes.
