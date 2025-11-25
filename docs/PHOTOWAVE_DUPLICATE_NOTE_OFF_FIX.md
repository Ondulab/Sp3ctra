# LuxWave Duplicate Note Off Fix

**Date:** 2025-11-25  
**Component:** LuxWave Synthesis Engine  
**File:** `src/synthesis/luxwave/synth_luxwave.c`  
**Issue:** Duplicate MIDI Note Off messages causing spurious warnings

## Problem Description

The LuxWave synthesis engine was generating warnings when receiving duplicate Note Off messages:

```
[WARNING] [LUXWAVE] WARNING - Note Off 39: No voice found (neither active nor idle)!
[WARNING] [LUXWAVE] Voice states: [0:note=0,state=0] [1:note=39,state=2] ...
```

The voice state dump showed that voice 1 had note 39 with state=2 (ADSR_STATE_RELEASE), but the Note Off handler couldn't find it.

## Root Cause

The issue was in the `synth_luxwave_note_off()` function's handling of duplicate Note Off messages:

1. **First Note Off** arrives → finds voice in RELEASE state → processes it → **immediately clears `midi_note = 0`**
2. **Second Note Off** (duplicate MIDI message) arrives → cannot find voice because `midi_note` was cleared → generates warning

The problem was that `midi_note` was being cleared too early (line 808), preventing duplicate Note Off messages from being detected and handled gracefully.

## Solution

Modified the Priority 2 handling (RELEASE voices) in `synth_luxwave_note_off()`:

### Before
```c
// Priority 2: If no active voice found, search in RELEASE voices
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
            oldest_voice_idx = i;
            log_debug("LUXWAVE", "Duplicate Note Off %d handled via RELEASE voice %d", note, i);
            break;
        }
    }
}

// Process the Note Off
if (oldest_voice_idx != -1) {
    if (state->voices[oldest_voice_idx].volume_adsr.state != ADSR_STATE_IDLE) {
        adsr_trigger_release(&state->voices[oldest_voice_idx].volume_adsr);
        adsr_trigger_release(&state->voices[oldest_voice_idx].filter_adsr);
    }
    
    // Clear midi_note AFTER processing Note Off
    state->voices[oldest_voice_idx].midi_note = 0;  // ← PROBLEM: cleared too early
}
```

### After
```c
// Priority 2: If no active voice found, search in RELEASE voices
if (oldest_voice_idx == -1) {
    for (i = 0; i < NUM_LUXWAVE_VOICES; i++) {
        if (state->voices[i].midi_note == note &&
            state->voices[i].volume_adsr.state == ADSR_STATE_RELEASE) {
            oldest_voice_idx = i;
            log_debug("LUXWAVE", "Duplicate Note Off %d handled via RELEASE voice %d", note, i);
            // NOTE: Do NOT clear midi_note here - allows future duplicates to be detected
            return; // ← Early return - duplicate Note Off, nothing to do
        }
    }
}

// Priority 3: Enhanced with debug logging
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

// Process the Note Off
if (oldest_voice_idx != -1) {
    // Only trigger release if not already IDLE or RELEASE
    if (state->voices[oldest_voice_idx].volume_adsr.state != ADSR_STATE_IDLE &&
        state->voices[oldest_voice_idx].volume_adsr.state != ADSR_STATE_RELEASE) {
        adsr_trigger_release(&state->voices[oldest_voice_idx].volume_adsr);
        adsr_trigger_release(&state->voices[oldest_voice_idx].filter_adsr);
    }
    
    // Clear midi_note AFTER processing Note Off
    state->voices[oldest_voice_idx].midi_note = 0;
}
```

## Key Changes

1. **Early Return for Duplicates**: When a duplicate Note Off is detected (Priority 2), the function now returns immediately without clearing `midi_note`. This allows subsequent duplicates to also be detected and handled silently.

2. **Enhanced State Check**: Added check to prevent triggering release on voices already in RELEASE state (defensive programming).

3. **Improved Debug Logging**: Added debug log for late Note Off messages handled via IDLE voices (Priority 3).

## Behavior

### Priority 1: Active Voices
- Finds the oldest active voice (not in RELEASE or IDLE)
- Triggers ADSR release
- Clears `midi_note` after processing

### Priority 2: RELEASE Voices (Duplicates)
- Detects duplicate Note Off messages
- Logs debug message
- **Returns early without clearing `midi_note`**
- Allows future duplicates to be detected

### Priority 3: IDLE Voices (Grace Period)
- Handles very late Note Off messages
- Logs debug message
- Clears `midi_note` after processing

## Benefits

1. **Eliminates Spurious Warnings**: Duplicate Note Off messages are now handled gracefully with debug logging instead of warnings
2. **Maintains Grace Period**: Late Note Off messages (Priority 3) still work correctly
3. **RT-Safe**: No additional allocations or blocking operations
4. **Defensive**: Added state checks prevent redundant ADSR triggers

## Testing

Compile and test with MIDI controller that may send duplicate Note Off messages:

```bash
make clean && make
./build/Sp3ctra
```

Expected behavior:
- No warnings for duplicate Note Off messages
- Debug logs show "Duplicate Note Off X handled via RELEASE voice Y"
- Audio playback remains smooth and glitch-free

## Related Documentation

- `docs/LUXWAVE_NOTE_OFF_FIX.md` - Original Note Off handling improvements
- `docs/LUXWAVE_NOTE_OFF_RACE_CONDITION_FIX.md` - Race condition fixes
- `docs/LUXWAVE_NOTE_OFF_ACTIVE_FLAG_FIX.md` - Active flag handling
- `docs/LUXWAVE_RT_OPTIMIZATION.md` - RT-safe optimizations

## Notes

- This fix is complementary to the existing grace period mechanism (Priority 3)
- The `midi_note` field is now only cleared when processing Priority 1 (active) or Priority 3 (IDLE) voices
- Priority 2 (RELEASE) voices keep their `midi_note` to allow duplicate detection
- The voice will eventually reach IDLE state naturally, at which point a late Note Off can still be processed via Priority 3
