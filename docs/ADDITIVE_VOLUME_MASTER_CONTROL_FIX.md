# Additive Volume Master Control Fix

**Date:** 2025-11-25  
**Status:** ✅ Fixed  
**Severity:** High - Audio routing bug  
**Component:** Audio Callback (audio_rtaudio.cpp)

## Problem Description

The additive synthesis volume control (CC37) was not functioning as a master control. When setting:
- `volume=0%` (CC37=0)
- `reverb_send=100%` (CC33=127)

The expected behavior was **complete silence**, but instead audio was still audible through the reverb path.

## Root Cause

In the audio callback (`handleCallback` in `audio_rtaudio.cpp`), the signal routing was incorrect:

### Before (Incorrect)
```cpp
// 1. Dry signal: volume applied
dry_sample_left += source_additive_left[i] * cached_level_additive;

// 2. Reverb signal: volume NOT applied ❌
reverb_input_left += source_additive_left[i] * cached_reverb_send_additive;
```

The reverb send was using the **raw signal** (`source_additive_left[i]`) without applying the volume control (`cached_level_additive`).

**Result:** When `volume=0%`:
- Dry path = 0 (silence) ✅
- Reverb path = 100% of raw signal ❌ **Audio still audible!**

## Solution

Apply the volume control **BEFORE** splitting the signal to dry and reverb paths:

### After (Correct)
```cpp
// 1. Apply volume to create "post-volume" signal
float additive_with_volume_left = source_additive_left[i] * cached_level_additive;
float additive_with_volume_right = source_additive_right[i] * cached_level_additive;

// 2. Route post-volume signal to dry
dry_sample_left += additive_with_volume_left;

// 3. Route post-volume signal to reverb
reverb_input_left += additive_with_volume_left * cached_reverb_send_additive;
```

**Result:** When `volume=0%`:
- Post-volume signal = 0
- Dry path = 0 (silence) ✅
- Reverb path = 0 (silence) ✅ **Complete silence!**

## Signal Flow Diagram

### Before (Incorrect)
```
Raw Signal (100%)
    ├─> Volume (0%) ──> Dry Mix (0%) ──> Output
    └─> Reverb Send (100%) ──> Reverb (100%) ──> Output ❌
```

### After (Correct)
```
Raw Signal (100%)
    └─> Volume (0%) ──> Post-Volume Signal (0%)
            ├─> Dry Mix ──> Output (0%)
            └─> Reverb Send ──> Reverb (0%) ──> Output ✅
```

## Changes Made

### File: `src/audio/rtaudio/audio_rtaudio.cpp`

**Modified sections:**
1. **Additive synthesis routing** (lines ~460-480)
   - Created `additive_with_volume_left` and `additive_with_volume_right` variables
   - Applied volume before routing to dry and reverb

2. **Polyphonic synthesis routing** (lines ~490-500)
   - Created `polyphonic_with_volume` variable
   - Applied volume before routing to dry and reverb

3. **Photowave synthesis routing** (lines ~510-520)
   - Created `photowave_with_volume` variable
   - Applied volume before routing to dry and reverb

4. **Reverb input calculation** (lines ~540-560)
   - Changed to use post-volume signals instead of raw signals
   - Updated comments to reflect the new behavior

## Testing

### Test Case 1: Volume=0%, Reverb Send=100%
**Expected:** Complete silence  
**Before Fix:** Audio audible through reverb ❌  
**After Fix:** Complete silence ✅

### Test Case 2: Volume=50%, Reverb Send=100%
**Expected:** 50% dry + 50% reverb  
**Before Fix:** 50% dry + 100% reverb (too loud) ❌  
**After Fix:** 50% dry + 50% reverb ✅

### Test Case 3: Volume=100%, Reverb Send=0%
**Expected:** 100% dry, no reverb  
**Before Fix:** Works correctly ✅  
**After Fix:** Works correctly ✅

## Impact

- **Additive synthesis:** Volume now correctly controls both dry and reverb paths
- **Polyphonic synthesis:** Same fix applied for consistency
- **Photowave synthesis:** Same fix applied for consistency
- **Performance:** No performance impact (same number of operations)
- **RT-safety:** Maintained (no allocations, no locks in audio callback)

## Related Parameters

- `volume` (CC37): Additive synthesis mix level (0.0-1.0)
  - Now acts as **master volume** before signal split
  
- `reverb_send` (CC33): Additive reverb send amount (0.0-1.0)
  - Now controls how much of the **post-volume** signal goes to reverb

## Semantic Clarification

### Volume (Mix Level)
- **Purpose:** Master volume control for the synthesis engine
- **Range:** 0.0 (silence) to 1.0 (full volume)
- **Applied:** BEFORE signal split to dry/reverb
- **Effect:** Controls overall output level (dry + reverb)

### Reverb Send
- **Purpose:** Controls reverb amount for the synthesis engine
- **Range:** 0.0 (no reverb) to 1.0 (full reverb)
- **Applied:** AFTER volume, on the post-volume signal
- **Effect:** Controls how much of the post-volume signal goes to reverb

## Commit Message

```
fix(audio): apply volume control before reverb send split

- Volume (mix level) now acts as master control before signal routing
- Reverb send now uses post-volume signal instead of raw signal
- Fixes issue where volume=0% + reverb_send=100% was still audible
- Applied same fix to additive, polyphonic, and photowave engines
- Maintains RT-safety and performance characteristics

When volume=0%, both dry and reverb paths are now silent as expected.
```

## References

- MIDI mapping: `midi_mapping.ini`
- MIDI callbacks: `src/communication/midi/midi_callbacks.cpp`
- Audio API: `src/audio/rtaudio/audio_c_api.h`
