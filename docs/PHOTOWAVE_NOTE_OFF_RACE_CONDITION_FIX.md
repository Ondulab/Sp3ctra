# LuxWave Note Off Race Condition Fix

**Date:** 2025-11-24  
**Status:** ✅ FIXED  
**Severity:** Medium (causes stuck notes and WARNING messages)

## Problem Description

### Symptom
LuxWave synthesis was experiencing "stuck notes" where Note Off messages would fail to find the corresponding voice, resulting in WARNING messages:

```
SYNTH_POLY: WARNING - Note Off 62: No voice found (neither active nor idle)!
SYNTH_POLY: WARNING - Note Off 69: No voice found (neither active nor idle)!
SYNTH_POLY: WARNING - Note Off 67: No voice found (neither active nor idle)!
```

**Note:** LuxWave uses the polyphonic synthesis system (`synth_luxsynth`) internally for voice management, which is why the warnings show "SYNTH_POLY" prefix.

### Root Cause
Race condition between ADSR envelope reaching IDLE state and Note Off message arrival:

1. **Voice becomes IDLE** (ADSR Release phase completes)
   - `synth_luxwave_process()` detects `vol_adsr < MIN_AUDIBLE_AMPLITUDE && state == IDLE`
   - Sets `voice->active = false`
   - **BUT** `voice->midi_note` was NOT cleared

2. **Note Off arrives** (slightly delayed)
   - `synth_luxwave_note_off()` searches for voice with matching `midi_note`
   - **Only searches ACTIVE voices** (not in RELEASE or IDLE)
   - **Fails to find the voice** because it's already IDLE
   - Results in WARNING message

### Why This Happens
With very short ADSR Release times (e.g., 0.2s), the envelope can reach IDLE state before the MIDI Note Off message is processed, especially under high polyphony or rapid note sequences.

## Solution Implemented

### Two-Phase Fix

#### Phase 1: Preserve `midi_note` in IDLE State
Modified `synth_luxwave_process()` to NOT clear `midi_note` when voice becomes IDLE:

```c
if (vol_adsr < MIN_AUDIBLE_AMPLITUDE && voice->volume_adsr.state == ADSR_STATE_IDLE) {
    voice->active = false;
    // NOTE: midi_note is intentionally NOT cleared here to allow late Note Off messages
    // It will be cleared when the Note Off is processed in synth_luxwave_note_off()
    continue;
}
```

#### Phase 2: Search RELEASE and IDLE Voices (Grace Period)
Modified `synth_luxwave_note_off()` to search RELEASE voices first, then IDLE voices:

```c
// Priority 1: Find the OLDEST ACTIVE voice with this note number (not in RELEASE or IDLE)
for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
    if (state->voices[i].midi_note == note &&
        state->voices[i].active &&
        state->voices[i].volume_adsr.state != ADSR_STATE_IDLE &&
        state->voices[i].volume_adsr.state != ADSR_STATE_RELEASE) {
        // Find oldest voice...
    }
}

// Priority 2: If no active voice found, search in RELEASE voices (duplicate/late Note Off)
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
            oldest_voice_idx = i;
            log_debug("LUXWAVE", "Duplicate Note Off %d handled via RELEASE voice %d (already releasing)", note, i);
            break;
        }
    }
}

// Priority 3: If still not found, search in IDLE voices (grace period for very late Note Off)
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].volume_adsr.state == ADSR_STATE_IDLE) {
            oldest_voice_idx = i;
            log_debug("LUXWAVE", "Late Note Off %d handled via IDLE voice %d (grace period)", note, i);
            break;
        }
    }
}

// Clear midi_note AFTER processing Note Off
if (oldest_voice_idx != -1) {
    if (state->voices[oldest_voice_idx].volume_adsr.state != ADSR_STATE_IDLE) {
        adsr_trigger_release(&state->voices[oldest_voice_idx].volume_adsr);
        adsr_trigger_release(&state->voices[oldest_voice_idx].filter_adsr);
    }
    state->voices[oldest_voice_idx].midi_note = 0;  // Clear AFTER processing
}
```

### Key Design Decisions

1. **Grace Period Approach**: RELEASE and IDLE voices retain their `midi_note` until Note Off is processed
2. **Three-Priority Search**: 
   - Priority 1: Active voices (ATTACK/DECAY/SUSTAIN)
   - Priority 2: RELEASE voices (handles duplicate Note Offs)
   - Priority 3: IDLE voices (handles very late Note Offs)
3. **Duplicate Note Off Handling**: If a voice is already in RELEASE, the duplicate Note Off is silently handled
4. **Diagnostic Logging**: Late/duplicate Note Offs are logged at DEBUG level for monitoring
5. **Enhanced Warnings**: If no voice is found, detailed voice state is logged

## Files Modified

- `src/synthesis/luxwave/synth_luxwave.c`
  - Modified `synth_luxwave_process()` to preserve `midi_note` in IDLE voices
  - Modified `synth_luxwave_note_off()` to search IDLE voices and clear `midi_note` after processing

## Testing

### Test Scenario
1. Play rapid note sequences with short Release time (0.2s)
2. Monitor for WARNING messages
3. Verify no stuck notes

### Expected Behavior
- ✅ No "WARNING - Note Off X: No voice found" messages
- ✅ All notes release properly
- ✅ DEBUG messages show:
  - "Duplicate Note Off X handled via RELEASE voice Y" for duplicate Note Offs
  - "Late Note Off X handled via IDLE voice Y" for very late Note Offs

### Performance Impact
- **Negligible**: Only adds one additional loop iteration in rare cases
- **RT-Safe**: No allocations, no blocking operations
- **Deterministic**: O(N) where N = number of voices (8)

## Related Issues

This fix is identical to the one applied to the polyphonic synthesis system:
- See `docs/LUXSYNTH_NOTE_OFF_RACE_CONDITION_FIX.md`

Both systems share the same underlying voice management architecture, so the same race condition existed in both.

## Verification

To verify the fix is working:

```bash
# Run with DEBUG logging enabled
./build/Sp3ctra

# Play rapid note sequences
# Monitor logs for:
# - Absence of WARNING messages
# - Presence of "Late Note Off X handled via IDLE voice Y" (DEBUG level)
```

## Future Considerations

1. **Unified Voice Management**: Consider extracting common voice management logic into a shared module
2. **Configurable Grace Period**: Could make the grace period duration configurable
3. **Statistics**: Track frequency of late Note Offs for performance monitoring

## Conclusion

The race condition has been eliminated by implementing a grace period where IDLE voices retain their `midi_note` until the Note Off is processed. This ensures that late-arriving Note Off messages can still find and properly clean up their corresponding voices.

**Status:** ✅ FIXED and TESTED
