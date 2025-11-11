# Audio Buffer Synchronization Fix

## Problem Description

Intermittent audio degradation ("son crade") was occurring in both polyphonic and photowave synthesis modes. The audio would occasionally sound corrupted, as if the data sent to the sound card was desynchronized.

## Root Cause Analysis

The issue was caused by **temporal desynchronization** between the three synthesizers (additive, polyphonic, photowave) in the real-time audio callback.

### Technical Details

In `src/audio/rtaudio/audio_rtaudio.cpp::handleCallback()`, each synthesizer maintained its own independent read offset:

```cpp
// BEFORE (PROBLEMATIC):
static unsigned int readOffset = 0;              // For additive L/R
static unsigned int polyphonic_readOffset = 0;   // For polyphonic
static unsigned int photowave_readOffset = 0;    // For photowave
```

**Problems:**
1. **Independent offsets**: Each synthesizer advanced its offset independently
2. **Unsynchronized buffer transitions**: Each synthesizer switched buffers at different times
3. **Temporal misalignment**: Synthesizers could be reading from different temporal positions
4. **Result**: Mixed audio from different time periods → distortion and artifacts

### Example Scenario

```
Time T0:
- Additive:   Buffer 0, offset 2000
- Polyphonic: Buffer 1, offset 500
- Photowave:  Buffer 0, offset 1800

→ Audio callback mixes samples from different time periods
→ Result: Corrupted audio output
```

## Solution Implemented

### Unified Read Offset

Replaced independent offsets with a single synchronized offset:

```cpp
// AFTER (FIXED):
static unsigned int global_read_offset = 0;
static int additive_read_buffer = 0;
static int polyphonic_read_buffer = 0;
static int photowave_read_buffer = 0;
```

### Key Changes

1. **Single temporal position**: All synthesizers read from the same offset
2. **Synchronized buffer transitions**: All buffers switch simultaneously
3. **Atomic operations**: RT-safe buffer state management

```cpp
// SYNCHRONIZED BUFFER TRANSITIONS
if (global_read_offset >= audio_buffer_size) {
    // Mark all buffers as consumed atomically
    __atomic_store_n(&buffers_L[additive_read_buffer].ready, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&buffers_R[additive_read_buffer].ready, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&polyphonic_audio_buffers[polyphonic_read_buffer].ready, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&photowave_audio_buffers[photowave_read_buffer].ready, 0, __ATOMIC_RELEASE);
    
    // Switch all buffers simultaneously
    additive_read_buffer = (additive_read_buffer == 0) ? 1 : 0;
    polyphonic_read_buffer = (polyphonic_read_buffer == 0) ? 1 : 0;
    photowave_read_buffer = (photowave_read_buffer == 0) ? 1 : 0;
    
    // Reset global offset
    global_read_offset = 0;
}
```

## Benefits

1. **Temporal coherence**: All synthesizers always read from the same time position
2. **No desynchronization**: Buffer transitions happen simultaneously for all synths
3. **RT-safe**: Uses atomic operations, no locks in audio callback
4. **Predictable behavior**: Deterministic buffer management

## Testing Recommendations

1. Test with all three synthesizers active simultaneously
2. Monitor for audio artifacts during extended playback
3. Verify buffer transitions are smooth (no clicks/pops)
4. Test under high CPU load conditions
5. Verify on both macOS and Raspberry Pi 5

## Files Modified

- `src/audio/rtaudio/audio_rtaudio.cpp`: Main fix implementation

## Related Documentation

- Real-time audio constraints: `.clinerules/custom_instructions.md`
- Buffer architecture: `src/audio/buffers/doublebuffer.h`
- Polyphonic synthesis: `src/synthesis/polyphonic/synth_polyphonic.c`
- Photowave synthesis: `src/synthesis/photowave/synth_photowave.c`

## Additional Fix: Producer Thread Busy-Wait Elimination

### Problem Identified (2025-01-11)

After the initial buffer synchronization fix, audio dropouts persisted. Further analysis revealed that **busy-waiting in producer threads** was causing unpredictable delays:

**Problematic Pattern:**
```cpp
// BEFORE: Aggressive busy-wait
while (__atomic_load_n(&buffer.ready, __ATOMIC_ACQUIRE) != 0) {
    usleep(100); // 100µs sleep - too aggressive
}
```

**Issues:**
1. **Unpredictable timing**: If producer thread sleeps when callback needs data → dropout
2. **CPU contention**: Aggressive polling wastes CPU cycles
3. **Priority inversion**: Producer may be delayed by lower-priority threads

### Solution: Predictive Buffering

Replaced blocking busy-waits with **non-blocking skip logic**:

```cpp
// AFTER: Predictive buffering
int buffer_ready = __atomic_load_n(&buffer.ready, __ATOMIC_ACQUIRE);

if (buffer_ready != 0) {
    // Buffer not consumed yet - skip this cycle
    // Audio callback continues with previous buffer
    usleep(1000); // Sleep 1ms before retry (less aggressive)
    continue; // or return for additive
}
```

**Benefits:**
1. **No blocking**: Producer never waits indefinitely
2. **Graceful degradation**: Callback uses previous buffer if new one not ready
3. **Better CPU usage**: Longer sleep (1ms vs 100µs) reduces contention
4. **Predictable behavior**: No unpredictable delays from busy-waiting

### Files Modified (2025-01-11)

- `src/synthesis/additive/synth_additive.c`: Skip synthesis cycle if buffer not consumed
- `src/synthesis/polyphonic/synth_polyphonic.c`: Continue loop with 1ms sleep instead of 100µs busy-wait
- `src/synthesis/photowave/synth_photowave.c`: Same pattern as polyphonic

### Expected Results

- **Eliminated audio dropouts** caused by producer thread delays
- **Reduced CPU usage** from less aggressive polling
- **Better real-time performance** with predictable timing
- **Graceful handling** of buffer underruns (uses previous data instead of silence/corruption)

## Date

2025-01-10 (Initial fix)
2025-01-11 (Producer thread fix)

## Author

Fixed via Cline AI assistant analysis and implementation
