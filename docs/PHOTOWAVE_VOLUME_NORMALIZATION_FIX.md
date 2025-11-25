# LuxWave Volume Normalization Fix

**Date:** 2025-11-18  
**Issue:** Volume fluctuations when releasing notes in polyphonic chords  
**Status:** ✅ Fixed

## Problem Description

When playing chords in LuxWave mode and releasing all fingers except one note, the remaining note's volume would briefly fluctuate and then increase abnormally. This created an unpleasant audible artifact during chord releases.

## Root Cause Analysis

The issue was caused by **dynamic voice normalization** in the audio processing loop:

```c
// PROBLEMATIC CODE (removed)
int active_voices = 0;
for (v = 0; v < NUM_LUXWAVE_VOICES; v++) {
    // ... process voice ...
    active_voices++;
}

// Dynamic normalization based on active voice count
if (active_voices > 1) {
    master_sum /= sqrtf((float)active_voices);
}
```

### Why This Was Problematic

1. **Voice counting included RELEASE phase**: A voice was counted as "active" as long as its ADSR envelope was above `MIN_AUDIBLE_AMPLITUDE` (0.001), even during the RELEASE phase.

2. **Dynamic normalization factor**: When playing a 4-note chord:
   - All 4 notes pressed: `active_voices = 4` → normalization factor = `sqrt(4) = 2.0`
   - 3 notes released: `active_voices` gradually decreases from 4 → 3 → 2 → 1
   - The normalization factor changes dynamically, causing the remaining note's volume to increase artificially

3. **Audible artifact**: The volume change was perceptible as a "swell" or "pump" effect on the sustaining note.

## Solution

Aligned LuxWave's volume handling with the **LuxSynth synthesis mode**, which uses a simpler and more stable approach:

```c
// NEW CODE (fixed)
for (v = 0; v < NUM_LUXWAVE_VOICES; v++) {
    // ... process voice ...
    master_sum += final_sample;
}

// Apply master amplitude (no dynamic normalization)
master_sum *= amplitude;

// Hard clipping to prevent overflow
master_sum = clamp_float(master_sum, -1.0f, 1.0f);
```

### Key Changes

1. **Removed dynamic normalization**: No more `sqrt(active_voices)` division
2. **Fixed amplitude scaling**: Uses constant `amplitude` parameter (default 0.5, controllable via MIDI CC7)
3. **Simple hard clipping**: Prevents overflow with straightforward clamping to ±1.0

## Benefits

- ✅ **No volume fluctuations**: Volume remains stable when releasing notes
- ✅ **Consistent behavior**: Matches polyphonic mode's proven approach
- ✅ **Simpler code**: Removed unnecessary complexity
- ✅ **Better predictability**: Volume is now solely controlled by ADSR envelopes and the fixed amplitude parameter

## Testing Recommendations

1. **Chord release test**: Play a 4-note chord, release 3 notes, verify the remaining note's volume stays constant
2. **Polyphony stress test**: Play all 8 voices simultaneously, verify no clipping or distortion
3. **MIDI CC7 test**: Verify amplitude control via MIDI CC7 (Volume) works correctly
4. **Comparison test**: Compare behavior with polyphonic mode to ensure consistency

## Technical Details

### Modified File
- `src/synthesis/luxwave/synth_luxwave.c`

### Modified Function
- `synth_luxwave_process()` (lines ~750-830)

### Removed Code
- `int active_voices` counter
- `active_voices++` increment
- Dynamic normalization: `if (active_voices > 1) { master_sum /= sqrtf((float)active_voices); }`

### Preserved Behavior
- ADSR envelope processing (unchanged)
- Voice stealing algorithm (unchanged)
- Filter modulation (unchanged)
- LFO vibrato (unchanged)
- All other synthesis parameters (unchanged)

## Related Documentation

- [LuxWave Synthesis Specification](LUXWAVE_SYNTHESIS_SPECIFICATION.md)
- [LuxSynth Buffer Timeout Fix](LUXSYNTH_BUFFER_TIMEOUT_FIX.md)
- [Audio Buffer Sync Fix](AUDIO_BUFFER_SYNC_FIX.md)

## Notes

This fix demonstrates the importance of **consistency across synthesis modes**. The polyphonic mode's simpler approach proved more robust than the "clever" dynamic normalization, which introduced unintended side effects.

**Lesson learned**: Sometimes the simplest solution is the best solution. Dynamic normalization seemed like a good idea to prevent clipping, but it created worse problems than it solved. Fixed amplitude scaling with hard clipping is more predictable and reliable.
