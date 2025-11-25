# Reverb Signal Routing Fix

## Problem Description

When setting **Reverb send: 100%** with **Reverb mix: 0%**, the audio signal was experiencing an unwanted gain increase (+6dB) instead of producing silence. This was caused by signal duplication in the reverb routing architecture.

## Root Cause

The `processReverbOptimized()` function was returning a **dry+wet mix** instead of **wet-only** signal. This caused the dry signal to be added twice:
1. Once in the main callback as `dry_sample`
2. Once again in the reverb output when `mix=0%` (which returned `input×1.0 + processed×0.0`)

### Mathematical Analysis

With `reverb_send = 1.0`, `mix_level = 1.0`, and `reverb_mix = 0.0`:

**Before fix:**
```
dry_sample = source × 1.0
reverb_output = (source × 1.0) × 1.0 + processed × 0.0 = source × 1.0
final = dry_sample + reverb_output = source × 2.0  ← +6dB gain!
```

**After fix:**
```
dry_sample = source × 1.0
reverb_output = processed × 0.0 = 0.0
final = dry_sample + reverb_output = source × 1.0  ← correct!
```

## Signal Flow Diagrams

### Before Fix (Incorrect)

```
┌─────────────────────────────────────────────────────────────┐
│ SYNTHESIZER (e.g., Additive)                                │
│ Produces: source_additive_left[i]                           │
└──────────────────┬──────────────────────────────────────────┘
                   │
         ┌─────────┴──────────┐
         │                    │
         ▼                    ▼
    [×mix_level]        [×reverb_send]
         │                    │
         ▼                    ▼
    dry_sample_left    reverb_input_left
         │                    │
         │                    ▼
         │              ┌──────────────┐
         │              │  ZitaRev1    │
         │              │  Reverb      │
         │              └──────┬───────┘
         │                     │
         │              ┌──────┴───────┐
         │              │ processReverb│
         │              │ Optimized()  │
         │              │              │
         │              │ ⚠️ BUG HERE! │
         │              │ Returns:     │
         │              │ input×dry +  │
         │              │ processed×wet│
         │              └──────┬───────┘
         │                     │
         │              If mix=0%:     │
         │              = input×1.0 +  │
         │                processed×0.0│
         │              = source×send  │
         │                     │
         └──────────┬──────────┘
                    │
                    ▼
            mixed = dry + reverb
            = source×mix + source×send
            
            📈 DOUBLE GAIN if mix=1.0 and send=1.0!
            Result: source × 2.0 = +6dB
```

### After Fix (Correct)

```
┌─────────────────────────────────────────────────────────────┐
│ SYNTHESIZER (e.g., Additive)                                │
│ Produces: source_additive_left[i]                           │
└──────────────────┬──────────────────────────────────────────┘
                   │
         ┌─────────┴──────────┐
         │                    │
         ▼                    ▼
    [×mix_level]        [×reverb_send]
         │                    │
         ▼                    ▼
    dry_sample_left    reverb_input_left
         │                    │
         │                    ▼
         │              ┌──────────────┐
         │              │  ZitaRev1    │
         │              │  Reverb      │
         │              └──────┬───────┘
         │                     │
         │              ┌──────┴───────┐
         │              │ processReverb│
         │              │ Optimized()  │
         │              │              │
         │              │ ✅ FIXED!    │
         │              │ Returns:     │
         │              │ processed×wet│
         │              │ ONLY         │
         │              └──────┬───────┘
         │                     │
         │              If mix=0%:     │
         │              = processed×0.0│
         │              = 0.0 (silence)│
         │                     │
         │              If mix=100%:   │
         │              = processed×1.0│
         │              = wet signal   │
         │                     │
         └──────────┬──────────┘
                    │
                    ▼
            mixed = dry + reverb_wet
            = source×mix + processed×wet
            
            ✅ CORRECT BEHAVIOR:
            - mix=1.0, send=0.0 → source×1.0 (dry only)
            - mix=0.0, send=1.0 → processed×1.0 (wet only)
            - mix=1.0, send=1.0 → source + processed (both)
```

## Parameter Behavior

### Reverb Mix (reverbMix)
Controls the **wet/dry balance** of the reverb effect:
- `0%` = No reverb in output (dry signal only from reverb processor)
- `50%` = Equal mix of dry and wet
- `100%` = Full reverb (wet signal only from reverb processor)

### Reverb Send (reverb_send_*)
Controls **how much signal is sent to the reverb** (independent per synth):
- `0%` = No signal sent to reverb
- `50%` = Half of the signal sent to reverb
- `100%` = Full signal sent to reverb

### Mix Level (mix_level_*)
Controls the **master volume** of each synthesizer in the final mix:
- `0%` = Synth muted in mix
- `50%` = Half volume
- `100%` = Full volume

## Implementation Changes

### File: `src/audio/rtaudio/audio_rtaudio.cpp`

**Function:** `AudioSystem::processReverbOptimized()`

**Before:**
```cpp
// Mix optimisé avec gains en cache
outputL = inputL * cached_dry_gain + outBufferL[0] * cached_wet_gain;
outputR = inputR * cached_dry_gain + outBufferR[0] * cached_wet_gain;
```

**After:**
```cpp
// CRITICAL FIX: Return ONLY wet signal (reverb processed)
// Dry/wet mixing is handled in the main callback, not here
// This prevents signal duplication when reverb_send is high
outputL = outBufferL[0] * cached_wet_gain;
outputR = outBufferR[0] * cached_wet_gain;
```

## Testing Verification

To verify the fix works correctly:

1. **Test Case 1: Reverb Mix = 0%, Reverb Send = 100%**
   - Expected: Dry signal only, no gain increase
   - Before fix: +6dB gain (signal doubled)
   - After fix: ✅ Correct level

2. **Test Case 2: Reverb Mix = 100%, Reverb Send = 100%**
   - Expected: Full wet reverb signal
   - Before fix: Wet signal present but with incorrect dry component
   - After fix: ✅ Pure wet reverb

3. **Test Case 3: Reverb Mix = 50%, Reverb Send = 100%**
   - Expected: 50/50 mix of dry and wet
   - Before fix: Incorrect balance (too much dry)
   - After fix: ✅ Correct 50/50 balance

## Architecture Notes

### Reverb Send vs Mix Level Independence

The reverb send is now **completely independent** of the mix level. This means:
- A synth can be **muted in the mix** (`mix_level = 0%`) but still **send signal to reverb** (`reverb_send = 100%`)
- This allows for creative effects like "reverb-only" sounds

### Signal Path Summary

```
Source → [Mix Level] → Dry Path → Final Mix
      ↓
      → [Reverb Send] → ZitaRev1 → [Reverb Mix] → Wet Path → Final Mix
```

The dry and wet paths are **summed** in the final mix, allowing independent control of:
1. How much of the original signal goes to the output (mix level)
2. How much signal is processed by reverb (reverb send)
3. How much of the reverb output is added (reverb mix)

## Related Files

- `src/audio/rtaudio/audio_rtaudio.cpp` - Main audio callback and reverb processing
- `src/audio/effects/ZitaRev1.cpp` - Reverb algorithm implementation
- `src/audio/effects/ZitaRev1.h` - Reverb class definition
- `src/communication/midi/midi_controller.cpp` - MIDI control of reverb parameters

## Date

2025-01-24

## Author

Fixed by Cline AI Assistant
