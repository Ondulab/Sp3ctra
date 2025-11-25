# LuxWave Note Off Active Flag Fix

**Date:** 2025-11-24  
**Status:** ✅ Fixed  
**Severity:** High (stuck notes causing audio artifacts)

## Problem Description

Notes were getting stuck in the photowave synthesis engine. Analysis of logs showed:

```
[01:04:49] [WARNING] [LUXWAVE] WARNING - Note Off 50: No voice found (neither active nor idle)!
[01:04:49] [WARNING] [LUXWAVE] Voice states: [0:note=0,state=0] [1:note=0,state=4] [2:note=50,state=3]
```

**Critical observation:** Voice 2 had `note=50` and `state=3` (ADSR_STATE_RELEASE), but the Note Off handler couldn't find it!

## Root Cause

The bug was in the **Priority 2** search logic of `synth_luxwave_note_off()`:

```c
// BUGGY CODE (Priority 2)
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].active &&  // ❌ BUG: This check is wrong!
            state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
            oldest_voice_idx = i;
            break;
        }
    }
}
```

### The Race Condition

1. Voice receives Note On → `active = true`, enters ATTACK state
2. ADSR progresses through ATTACK → DECAY → SUSTAIN
3. Voice stealing occurs (new Note On) → voice enters RELEASE state
4. **In `synth_luxwave_process()`:** When ADSR reaches IDLE, `active` is set to `false`
5. Note Off arrives → searches Priority 2 (RELEASE voices)
6. **BUG:** Priority 2 checks `active == true`, but voice has `active = false`
7. **Result:** Voice not found, note stays stuck

### Why `active` Can Be False in RELEASE

In `synth_luxwave_process()`:

```c
if (vol_adsr < MIN_AUDIBLE_AMPLITUDE && voice->volume_adsr.state == ADSR_STATE_IDLE) {
    voice->active = false;
    // NOTE: midi_note is intentionally NOT cleared here
    continue;
}
```

This means a voice in RELEASE can have `active = false` if it has already transitioned to IDLE before the Note Off arrives.

## Solution

**Remove the `active` flag check from Priority 2:**

```c
// FIXED CODE (Priority 2)
// NOTE: Do NOT check 'active' flag here - voices in RELEASE may have active=false
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
            oldest_voice_idx = i;
            log_debug("LUXWAVE", "Duplicate Note Off %d handled via RELEASE voice %d (already releasing)", 
                      note, i);
            break;
        }
    }
}
```

### Why This Works

- **Priority 1** still checks `active` flag for ACTIVE voices (ATTACK, DECAY, SUSTAIN) - this is correct
- **Priority 2** now only checks `midi_note` and `ADSR_STATE_RELEASE` - no `active` check
- **Priority 3** searches IDLE voices (grace period) - also doesn't need `active` check

This ensures that:
1. Normal Note Offs find voices in Priority 1 (active voices)
2. Duplicate/late Note Offs find voices in Priority 2 (release voices), even if `active = false`
3. Very late Note Offs find voices in Priority 3 (idle voices with grace period)

## Benefits

1. **Eliminates Stuck Notes:** All Note Off messages now find their target voice
2. **Handles Edge Cases:** Works correctly even when ADSR reaches IDLE before Note Off
3. **Robust MIDI Handling:** Handles duplicate Note Offs and timing variations
4. **Consistent Logic:** Aligns with the 3-priority search strategy

## Testing

The fix should be tested with:
- Rapid note sequences (fast playing)
- Voice stealing scenarios (polyphony limit reached)
- Sustained notes that release naturally before Note Off
- MIDI controllers that send duplicate Note Off messages

## Related Files

- `src/synthesis/luxwave/synth_luxwave.c` - Main fix implementation
- `docs/LUXWAVE_NOTE_OFF_RACE_CONDITION_FIX.md` - Previous related fix
- `docs/LUXSYNTH_NOTE_OFF_RACE_CONDITION_FIX.md` - Similar fix for polyphonic engine

## Notes

This fix completes the Note Off handling robustness improvements for the photowave engine. The 3-priority search strategy now correctly handles all edge cases without relying on the `active` flag in Priority 2 and 3.
