# Photowave Note Off Behavior Fix

**Date:** 2025-11-24  
**Status:** ✅ Fixed  
**Priority:** High  
**Component:** Photowave Synthesis Engine

## Problem Description

### Symptom
When rapidly retriggering the same MIDI note in Photowave mode (playing the same note multiple times before the release phase completes), a single Note Off event would incorrectly release **all instances** of that note simultaneously, instead of releasing only the oldest instance.

### Root Cause
The `synth_photowave_note_off()` function had an inconsistent implementation compared to the Polyphonic mode:

**Photowave (incorrect behavior):**
```c
// Released ALL voices playing the same note
for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
    if (state->voices[i].midi_note == note && ...) {
        adsr_trigger_release(&state->voices[i].volume_adsr);
        adsr_trigger_release(&state->voices[i].filter_adsr);
    }
}
```

**Polyphonic (correct behavior):**
```c
// Only released the OLDEST voice playing that note
int oldest_voice_idx = -1;
unsigned long long oldest_order = state->current_trigger_order + 1;

for (i = 0; i < g_num_poly_voices; ++i) {
    if (poly_voices[i].midi_note == noteNumber && ...) {
        if (poly_voices[i].last_triggered_order < oldest_order) {
            oldest_order = poly_voices[i].last_triggered_order;
            oldest_voice_idx = i;
        }
    }
}
// Only release oldest_voice_idx
```

### Impact
- **Musical Expression:** Rapid note retriggering (common in percussive playing styles) would cause all note instances to stop simultaneously, creating an unnatural and abrupt sound cutoff
- **Polyphony Management:** Voice stealing algorithm couldn't work properly since multiple voices would be released at once
- **Consistency:** Behavior was inconsistent between Photowave and Polyphonic synthesis modes

## Solution

### Implementation
Modified `synth_photowave_note_off()` to match the Polyphonic mode behavior:

1. **Find the oldest voice** playing the requested note (using `trigger_order`)
2. **Release only that voice**, allowing other instances to continue their natural envelope progression
3. **Maintain voice independence** for proper polyphonic behavior

### Code Changes

**File:** `src/synthesis/photowave/synth_photowave.c`

**Function:** `synth_photowave_note_off()`

```c
void synth_photowave_note_off(PhotowaveState *state, uint8_t note) {
    int i;
    int oldest_voice_idx = -1;
    unsigned long long oldest_order = state->current_trigger_order + 1;
    
    if (!state) return;
    
    // Find the OLDEST voice with this note number that is not already in RELEASE or IDLE
    // This ensures we only release the first instance of the note, not all of them
    for (i = 0; i < NUM_PHOTOWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].active &&
            state->voices[i].volume_adsr.state != ADSR_STATE_IDLE &&
            state->voices[i].volume_adsr.state != ADSR_STATE_RELEASE) {
            // Find the oldest (lowest trigger order) voice with this note
            if (state->voices[i].trigger_order < oldest_order) {
                oldest_order = state->voices[i].trigger_order;
                oldest_voice_idx = i;
            }
        }
    }
    
    // Only release the oldest voice found
    if (oldest_voice_idx != -1) {
        adsr_trigger_release(&state->voices[oldest_voice_idx].volume_adsr);
        adsr_trigger_release(&state->voices[oldest_voice_idx].filter_adsr);
        
        log_debug("PHOTOWAVE", "Note Off: voice=%d, note=%d", oldest_voice_idx, note);
    }
}
```

## Benefits

1. **Consistent Behavior:** Photowave and Polyphonic modes now handle Note Off events identically
2. **Natural Sound:** Retriggered notes can overlap naturally with proper envelope progression
3. **Proper Voice Management:** Voice stealing algorithm works correctly with independent voice control
4. **MIDI Compliance:** Follows standard MIDI polyphonic behavior where each Note On/Off pair affects a single voice instance

## Testing Recommendations

### Test Cases

1. **Rapid Retriggering:**
   - Play the same note rapidly (e.g., 5-10 times per second)
   - Verify each note instance releases independently
   - Expected: Smooth overlapping decay tails

2. **Sustained Notes:**
   - Hold a note, then play it again while still holding
   - Release the first note
   - Expected: Second note continues playing

3. **Polyphonic Chords:**
   - Play a chord with repeated notes (e.g., C-E-G-C)
   - Release notes in different orders
   - Expected: Each note releases independently

4. **Voice Stealing:**
   - Play more notes than available voices (>4 for Photowave)
   - Verify oldest voices are stolen correctly
   - Expected: No abrupt cutoffs, smooth voice transitions

### Performance Verification

- Monitor CPU usage during rapid retriggering
- Verify no increase in buffer timeouts
- Check ADSR envelope progression is smooth
- Confirm no audio glitches or clicks

## Related Documentation

- `docs/PHOTOWAVE_SYNTHESIS_SPECIFICATION.md` - Photowave synthesis architecture
- `docs/PHOTOWAVE_RACE_CONDITION_FIX.md` - Related buffer management fix
- `docs/MIDI_SYSTEM_SPECIFICATION.md` - MIDI implementation details
- `src/synthesis/polyphonic/synth_polyphonic.c` - Reference implementation

## Notes

- This fix aligns with standard MIDI polyphonic behavior
- The `trigger_order` mechanism is essential for proper voice management
- Both synthesis modes now share identical Note Off logic
- No changes required to ADSR envelope implementation
- RT-safe implementation (no allocations, no blocking operations)

## Commit Reference

This fix addresses the inconsistency between Photowave and Polyphonic Note Off handling, ensuring proper polyphonic behavior across all synthesis modes.
