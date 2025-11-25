# LuxSynth Stereo Panning Implementation & Fix

**Date:** 2025-11-25  
**Status:** Diagnostic completed, fix in progress  
**Priority:** High - Feature not working as designed

## Problem Summary

The polyphonic synthesis generates stereo audio with spectral panning (each harmonic positioned independently in stereo field based on image color analysis), but the audio callback reads it as **mono** and duplicates the signal to both L/R channels, completely ignoring the stereo separation.

## Root Cause Analysis

### 1. Buffer Structure Issue (doublebuffer.h)

```c
typedef struct {
    float* data;              // ❌ MONO: Single buffer
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile int ready;
    uint64_t write_timestamp_us;
} FftAudioDataBuffer;
```

**Problem:** Only one `data` pointer → mono output only

**Solution needed:** Two separate buffers for L/R channels

### 2. Thread Function Issue (synth_luxsynth.c:407)

```c
synth_luxsynthMode_process(
    polyphonic_audio_buffers[local_producer_idx].data,
    polyphonic_audio_buffers[local_producer_idx].data,  // ❌ SAME BUFFER!
    g_sp3ctra_config.audio_buffer_size);
```

**Problem:** Both L/R parameters point to the same buffer → stereo output overwritten

### 3. Audio Callback Issue (audio_rtaudio.cpp:162-235)

```cpp
// LuxSynth synthesis (mono)  ← Comment reveals the issue!
if (polyphonic_audio_buffers[polyphonic_read_buffer].ready == 1) {
  source_fft = &polyphonic_audio_buffers[polyphonic_read_buffer]
                    .data[global_read_offset];
}

// Later in mixing:
if (source_fft) {
  dry_sample_left += source_fft[i] * cached_level_luxsynth;
  dry_sample_right += source_fft[i] * cached_level_luxsynth;  // ❌ Duplication!
}
```

**Problem:** Single mono source duplicated to both channels

## Verification with Debug Traces

### Traces Added

1. **Stereo Gains Copy** (`read_preprocessed_fft_magnitudes`):
   - Logs L/R gains for first 8 harmonics every 100ms
   - Confirms gains are being copied from preprocessing

2. **Generated Output** (`synth_luxsynthMode_process`):
   - Logs average L/R levels every 100ms
   - Shows if stereo separation exists in generated audio

3. **Expected Output:**
   ```
   [POLY_STEREO] Gains copied - First 8 harmonics:
     H0: L=0.850 R=0.527 (diff=0.323)  ← Different gains = stereo!
     H1: L=0.650 R=0.760 (diff=-0.110)
     ...
   
   [POLY_OUTPUT] Generated L=0.012345 R=0.008765 (diff=0.003580, ratio=1.408)
   ```

If gains show differences but output shows ratio ≈ 1.0, confirms the bug.

## Solution Architecture

### Phase 1: Modify Buffer Structure

**File:** `src/audio/buffers/doublebuffer.h`

```c
typedef struct {
    float* data_left;         // ✅ NEW: Separate left channel
    float* data_right;        // ✅ NEW: Separate right channel
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile int ready;
    uint64_t write_timestamp_us;
} FftAudioDataBuffer;
```

### Phase 2: Update LuxSynth Thread

**File:** `src/synthesis/luxsynth/synth_luxsynth.c`

**Init function:**
```c
void synth_luxsynthMode_init(void) {
  // ...
  for (i = 0; i < 2; ++i) {
    // Allocate separate L/R buffers
    polyphonic_audio_buffers[i].data_left = 
        (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
    polyphonic_audio_buffers[i].data_right = 
        (float*)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
  }
}
```

**Thread function:**
```c
synth_luxsynthMode_process(
    polyphonic_audio_buffers[local_producer_idx].data_left,   // ✅ Separate L
    polyphonic_audio_buffers[local_producer_idx].data_right,  // ✅ Separate R
    g_sp3ctra_config.audio_buffer_size);
```

### Phase 3: Update Audio Callback

**File:** `src/audio/rtaudio/audio_rtaudio.cpp`

```cpp
// LuxSynth synthesis (stereo)  ← Update comment!
float *source_fft_left = nullptr;
float *source_fft_right = nullptr;

if (polyphonic_audio_buffers[polyphonic_read_buffer].ready == 1) {
  source_fft_left = &polyphonic_audio_buffers[polyphonic_read_buffer]
                         .data_left[global_read_offset];
  source_fft_right = &polyphonic_audio_buffers[polyphonic_read_buffer]
                          .data_right[global_read_offset];
}

// Later in mixing:
if (source_fft_left && source_fft_right) {
  dry_sample_left += source_fft_left[i] * cached_level_luxsynth;   // ✅ True L
  dry_sample_right += source_fft_right[i] * cached_level_luxsynth; // ✅ True R
}
```

## Implementation Checklist

- [x] Add diagnostic traces in synth_luxsynth.c
- [x] Add diagnostic traces in synth_luxsynth_process
- [x] Compile with traces active
- [ ] Run and confirm mono duplication via traces
- [ ] Modify FftAudioDataBuffer structure (add data_left/data_right)
- [ ] Update polyphonic init (allocate 2 buffers)
- [ ] Update polyphonic thread (pass 2 separate buffers)
- [ ] Update audio callback (read 2 separate buffers)
- [ ] Compile and test
- [ ] Validate stereo effect is audible
- [ ] Remove or reduce debug traces
- [ ] Document final solution

## Expected Results

After fix:
- **Spectral panning audible:** Each harmonic positioned independently
- **Dynamic stereo image:** Changes with image color content
- **Constant power panning:** Total energy preserved (L² + R² = 1)
- **Smooth transitions:** Temporal smoothing prevents clicks

## Technical Notes

### Memory Impact
- **Before:** 2 buffers × 1 channel = 2 × 512 floats = 4 KB
- **After:** 2 buffers × 2 channels = 4 × 512 floats = 8 KB
- **Impact:** Negligible (+4 KB)

### RT Safety
- All allocations done at init time
- No malloc/free in RT threads
- Atomic operations for buffer ready flags
- Lock-free reads in audio callback

### Performance
- No additional CPU cost in synthesis
- Minimal memory bandwidth increase (2× reads in callback)
- Same number of sine calculations
- Gains are pre-multiplied (no extra math)

## Related Files

- `src/audio/buffers/doublebuffer.h` - Buffer structure
- `src/synthesis/luxsynth/synth_luxsynth.c` - Synthesis thread
- `src/synthesis/luxsynth/synth_luxsynth.h` - Public API
- `src/audio/rtaudio/audio_rtaudio.cpp` - Audio callback
- `src/processing/image_preprocessor.c` - Color FFT & pan calculation

## References

- Color-based spectral panning algorithm: `image_preprocess_color_fft()`
- Constant power panning law: `sqrt(0.5 ± 0.5 * pan_position)`
- Temporal smoothing: 5-frame circular buffer @ 1kHz
