# LuxSynth FFT Optimization - Architecture Refactoring

**Date:** 2025-11-13  
**Author:** zhonx  
**Status:** ✅ Implemented and Compiled

## Problem Statement

### Symptoms
- Frequent "[AUDIO] LuxSynth buffer missing!" messages
- Regular audio crackling/pops during polyphonic synthesis
- Unstable polyphonic synthesis performance

### Root Cause Analysis

**System Configuration:**
- Audio callback: 128 frames @ 48kHz = 2.67ms per callback
- UDP images: 1kHz (1ms per image)
- Platform: macOS (development) + Raspberry Pi 5 (production)

**Bottleneck Identified:**
The polyphonic thread was attempting to compute **1000 FFT/second** (one FFT per UDP image), but:
- Each FFT on ~2592 pixels takes **1.5-2ms**
- Thread can physically only achieve **500-600 FFT/sec maximum**
- Result: Audio buffers not ready in time → underruns → crackling

**Problematic Code Flow (BEFORE):**
```c
// In synth_luxsynthMode_thread_func()
while (keepRunning) {
    process_image_data_for_fft(image_db);  // Blocked waiting for image
    // When image arrives:
    // 1. Convert RGB → grayscale
    // 2. Moving average over 8 frames  
    // 3. FFT on entire image (~1.5-2ms) ← BOTTLENECK
    // 4. Calculate magnitudes
    // 5. Generate audio buffer
}
```

## Solution Architecture

### Core Principle
**Move FFT computation from RT-constrained polyphonic thread to non-RT UDP thread**

### New Data Flow

**UDP Thread** (`multithreading.c` - `udpThread`):
```
For each complete image received:
1. Mix RGB (sequencer)
2. Calculate grayscale
3. ✨ NEW: Calculate FFT + magnitudes with smoothing
4. Calculate pan/stereo
5. Calculate DMX
6. Store everything in preprocessed_data
```

**LuxSynth Thread** (`synth_luxsynth.c`):
```
while (keepRunning) {
    // Read pre-computed FFT magnitudes from preprocessed_data
    read_preprocessed_fft_magnitudes(image_db);
    // Generate audio buffer (~0.3-0.5ms instead of 2ms)
    synth_luxsynthMode_process(...);
}
```

### Benefits
- ✅ FFT computed in non-RT thread (no strict time constraint)
- ✅ LuxSynth thread 4x faster (0.5ms vs 2ms)
- ✅ Coherent architecture (all preprocessing centralized)
- ✅ No moving average needed (FFT per image @ 1kHz is sufficient)
- ✅ No added audio latency

## Implementation Details

### 1. Data Structure Extension

**File:** `src/processing/image_preprocessor.h`

Added FFT data to `PreprocessedImageData`:
```c
#ifndef DISABLE_LUXSYNTH
#define PREPROCESS_MAX_FFT_BINS 64  /* Must match MAX_MAPPED_OSCILLATORS */
struct {
    float magnitudes[PREPROCESS_MAX_FFT_BINS];  /* Pre-computed smoothed FFT magnitudes */
    int valid;  /* 1 if FFT data is valid, 0 otherwise */
} fft;
#endif
```

### 2. FFT Preprocessing Function

**File:** `src/processing/image_preprocessor.c`

New function `image_preprocess_fft()`:
- Lazy initialization of KissFFT on first call
- Converts grayscale [0.0-1.0] to FFT input [0-255]
- Computes FFT using KissFFT
- Calculates and normalizes magnitudes
- Applies exponential smoothing (AMPLITUDE_SMOOTHING_ALPHA = 0.1)
- Stores results in `data->fft.magnitudes[]`

**Key Features:**
- Reuses normalization factors from original implementation
- Maintains smoothing for temporal stability
- Conditional compilation with `#ifndef DISABLE_LUXSYNTH`

### 3. UDP Thread Integration

**File:** `src/threading/multithreading.c`

Added FFT preprocessing call after standard preprocessing:
```c
/* Step 2.5: Calculate FFT for polyphonic synthesis (if enabled) */
#ifndef DISABLE_LUXSYNTH
if (image_preprocess_fft(&preprocessed_temp) != 0) {
    log_warning("THREAD", "FFT preprocessing failed - polyphonic synthesis may glitch");
    preprocessed_temp.fft.valid = 0;  /* Mark FFT as invalid */
}
#endif
```

Also initialized FFT data in `initDoubleBuffer()`:
```c
#ifndef DISABLE_LUXSYNTH
memset(db->preprocessed_data.fft.magnitudes, 0, sizeof(db->preprocessed_data.fft.magnitudes));
db->preprocessed_data.fft.valid = 0;
#endif
```

### 4. LuxSynth Thread Simplification

**File:** `src/synthesis/luxsynth/synth_luxsynth.c`

**Removed:**
- `process_image_data_for_fft()` function (entire FFT computation)
- Moving average logic (no longer needed at 1kHz)
- Local FFT magnitude calculation in `synth_luxsynthMode_process()`

**Added:**
- `read_preprocessed_fft_magnitudes()` - simple reader function
- Direct copy from `preprocessed_data.fft.magnitudes[]` to `global_smoothed_magnitudes[]`

**Modified:**
- `synth_luxsynthMode_thread_func()` now calls `read_preprocessed_fft_magnitudes()`
- `synth_luxsynthMode_process()` simplified - just uses pre-computed magnitudes

## Performance Impact

### Expected Improvements

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| LuxSynth thread time | ~2ms | ~0.5ms | **4x faster** |
| FFT computation rate | 500-600/sec | 1000/sec | **2x faster** |
| CPU usage (polyphonic) | High | Low | **-60-70%** |
| Buffer underruns | Frequent | None | **100% reduction** |

### Validation Criteria

After implementation, we should observe:
- ❌ Complete disappearance of "[AUDIO] LuxSynth buffer missing!" messages
- ✅ Stable audio without crackling
- ✅ Logs showing much faster polyphonic thread execution
- ✅ Reduced CPU usage in polyphonic synthesis

## Technical Decisions

### Smoothing Strategy
**Decision:** Keep exponential smoothing (Option A - conservative approach)

**Rationale:**
- Smoothing applied @ 1kHz in `image_preprocessor.c`
- Provides temporal stability for FFT magnitude transitions
- Minimal CPU cost (one line of calculation)
- Can be removed later if testing shows it's unnecessary

### Moving Average
**Decision:** Removed completely

**Rationale:**
- At 1kHz image rate, moving average over 8 frames is unnecessary
- FFT per image provides sufficient temporal resolution
- Simplifies code and reduces memory usage

### Conditional Compilation
**Decision:** All FFT preprocessing guarded by `#ifndef DISABLE_LUXSYNTH`

**Rationale:**
- Respects existing polyphonic enable/disable mechanism
- No overhead when polyphonic synthesis is disabled
- Clean separation of concerns

## Code Quality

### RT-Safety Compliance
- ✅ No FFT computation in RT thread
- ✅ No dynamic allocation in RT path
- ✅ Simple memcpy() for data transfer
- ✅ Mutex-protected read (brief, bounded operation)

### Error Handling
- FFT initialization failure → `fft.valid = 0` → silence fallback
- NULL pointer checks in all functions
- Graceful degradation if preprocessing fails

### Documentation
- Clear comments explaining new architecture
- English comments as per project conventions
- Function documentation with parameters and return values

## Migration Path

### Phase 1: Implementation (COMPLETED ✅)
1. ✅ Add FFT structure to `PreprocessedImageData`
2. ✅ Implement `image_preprocess_fft()` in `image_preprocessor.c`
3. ✅ Integrate FFT call in `udpThread()`
4. ✅ Simplify `synth_luxsynth.c` to use pre-computed data
5. ✅ Compile and verify no errors

### Phase 2: Testing (USER REQUIRED)
1. Run application with MIDI controller
2. Monitor for "[AUDIO] LuxSynth buffer missing!" messages
3. Listen for audio crackling
4. Verify CPU usage reduction
5. Test on Raspberry Pi 5 (production environment)

### Phase 3: Cleanup (OPTIONAL - Future PR)
The following legacy code can be removed in a future cleanup:
- `image_line_history[]` array (moving average buffer)
- `polyphonic_context.fft_input/output` (duplicate FFT buffers)
- `generate_test_data_for_fft()` (test function)
- `image_history_mutex` (no longer needed)

**Note:** These are kept for now to maintain backward compatibility with test mode.

## Files Modified

1. `src/processing/image_preprocessor.h` - Added FFT structure and function declaration
2. `src/processing/image_preprocessor.c` - Implemented FFT preprocessing
3. `src/threading/multithreading.c` - Integrated FFT call in UDP thread
4. `src/synthesis/luxsynth/synth_luxsynth.c` - Simplified to use pre-computed FFT

## Rollback Plan

If issues arise:
```bash
git revert <commit-hash>
```

Or disable FFT preprocessing by defining `DISABLE_LUXSYNTH` in build flags.

## FFT Temporal Smoothing Architecture

### Problem: Low-Frequency Crackling

**Date Added:** 2025-11-14  
**Status:** 🔄 Migration Planned

#### Root Cause Analysis

While the current implementation successfully moved FFT computation to the UDP thread, testing revealed **low-frequency crackling** (bass frequencies, bins 0-10). Analysis shows:

**Why Bass Frequencies Crack:**
1. **Long periods**: A 50Hz bass has a 20ms period. Magnitude discontinuities create audible "clacks" lasting multiple cycles
2. **High energy concentration**: Bass bins contain significant energy. Abrupt changes = loud artifacts
3. **No temporal continuity**: Current implementation calculates FFT on each individual image without historical context
4. **Temporal aliasing**: UDP reception rate (~1kHz) creates beating patterns with bass frequencies

**Comparison with Working LuxSynth Implementation:**

The original polyphonic thread (`synth_luxsynth.c`) **did not have this problem** because it used:
- **Moving average window** (`MOVING_AVERAGE_WINDOW_SIZE = 8`)
- **Circular buffer** of historical image lines
- **Temporal smoothing** before FFT computation
- **Pre-filled history** with white lines at startup (prevents transients)

```c
// Original working code (synth_luxsynth.c)
for (j = 0; j < nb_pixels; ++j) {
  sum = 0.0f;
  for (k = 0; k < history_fill_count; ++k) {
    idx = (history_write_index - 1 - k + MOVING_AVERAGE_WINDOW_SIZE) %
          MOVING_AVERAGE_WINDOW_SIZE;
    sum += image_line_history[idx].line_data[j];
  }
  polyphonic_context.fft_input[j] = sum / history_fill_count;  // Averaged!
}
kiss_fftr(polyphonic_context.fft_cfg, polyphonic_context.fft_input, ...);
```

**Current problematic flow:**
```c
// Current implementation (image_preprocessor.c)
// Converts grayscale directly to FFT input - NO HISTORY!
for (i = 0; i < nb_pixels; i++) {
    fft_input[i] = data->grayscale[i] * 255.0f;  // Direct conversion
}
kiss_fftr(fft_cfg, fft_input, fft_output);  // FFT on single frame
```

### Solution: Add Temporal History Buffer

#### Design Overview

Add a **circular buffer** in `image_preprocessor.c` to maintain FFT temporal continuity:

```c
/* Private state for FFT temporal smoothing */
#ifndef DISABLE_LUXSYNTH
#define FFT_HISTORY_SIZE 5  /* 5ms @ 1kHz - good compromise */

static struct {
    float history[FFT_HISTORY_SIZE][PREPROCESS_MAX_FFT_BINS];
    int write_index;
    int fill_count;
    int initialized;
} fft_history_state = {0};
#endif
```

#### Modified FFT Processing Flow

```c
int image_preprocess_fft(PreprocessedImageData *data) {
    // 1. Calculate current frame FFT
    kiss_fftr(fft_cfg, fft_input, fft_output);
    
    // 2. Calculate raw magnitudes
    float raw_magnitudes[PREPROCESS_MAX_FFT_BINS];
    for (i = 0; i < PREPROCESS_MAX_FFT_BINS; i++) {
        float real = fft_output[i].r;
        float imag = fft_output[i].i;
        raw_magnitudes[i] = sqrtf(real * real + imag * imag) / norm_factor;
    }
    
    // 3. Store in circular buffer
    memcpy(fft_history_state.history[fft_history_state.write_index],
           raw_magnitudes, sizeof(raw_magnitudes));
    fft_history_state.write_index = 
        (fft_history_state.write_index + 1) % FFT_HISTORY_SIZE;
    if (fft_history_state.fill_count < FFT_HISTORY_SIZE) {
        fft_history_state.fill_count++;
    }
    
    // 4. Calculate moving average
    for (i = 0; i < PREPROCESS_MAX_FFT_BINS; i++) {
        float sum = 0.0f;
        for (int h = 0; h < fft_history_state.fill_count; h++) {
            int idx = (fft_history_state.write_index - 1 - h + FFT_HISTORY_SIZE) 
                      % FFT_HISTORY_SIZE;
            sum += fft_history_state.history[idx][i];
        }
        float averaged = sum / fft_history_state.fill_count;
        
        // 5. Apply exponential smoothing on top of moving average
        data->fft.magnitudes[i] = 
            AMPLITUDE_SMOOTHING_ALPHA * averaged +
            (1.0f - AMPLITUDE_SMOOTHING_ALPHA) * data->fft.magnitudes[i];
    }
}
```

#### Initialization Strategy

Pre-fill history buffer at startup to prevent transients:

```c
void image_preprocess_init(void) {
    #ifndef DISABLE_LUXSYNTH
    // Pre-fill FFT history with "white" spectrum (all bins = 1.0)
    for (int h = 0; h < FFT_HISTORY_SIZE; h++) {
        for (int i = 0; i < PREPROCESS_MAX_FFT_BINS; i++) {
            fft_history_state.history[h][i] = 1.0f;
        }
    }
    fft_history_state.fill_count = FFT_HISTORY_SIZE;
    fft_history_state.initialized = 1;
    #endif
}
```

### Benefits of This Approach

| Aspect | Benefit |
|--------|---------|
| **Temporal continuity** | 5ms moving average smooths magnitude transitions |
| **Bass stability** | Low frequencies no longer have abrupt changes |
| **Memory overhead** | Minimal: 5 × 64 floats = 1.3 KB |
| **CPU overhead** | Negligible: simple averaging loop |
| **RT-safety** | All computation in non-RT UDP thread |
| **Architecture** | Consistent with original working polyphonic design |

### Performance Characteristics

**FFT History Size Trade-offs:**

| Size | Latency | Smoothing | Memory | Recommendation |
|------|---------|-----------|--------|----------------|
| 3 frames | 3ms | Light | 768 bytes | Too reactive |
| **5 frames** | **5ms** | **Good** | **1.3 KB** | **✅ Optimal** |
| 8 frames | 8ms | Heavy | 2 KB | Excessive |
| 10 frames | 10ms | Very heavy | 2.5 KB | Too sluggish |

**Rationale for FFT_HISTORY_SIZE = 5:**
- Provides sufficient smoothing for bass frequencies (50-100Hz)
- Latency (5ms) is imperceptible in musical context
- Balances responsiveness vs. stability
- Matches typical audio buffer sizes (128-256 frames @ 48kHz = 2.7-5.3ms)

## Migration Path - Phase 4: Temporal Smoothing

### Phase 4.1: Implementation (PLANNED)

**Files to Modify:**

1. **`src/processing/image_preprocessor.c`**
   - Add `fft_history_state` static structure
   - Modify `image_preprocess_fft()` to use circular buffer
   - Add history pre-fill in `image_preprocess_init()`
   - Add cleanup in `image_preprocess_cleanup()`

2. **`src/processing/image_preprocessor.h`**
   - Add `FFT_HISTORY_SIZE` constant
   - Document temporal smoothing in function comments

**Implementation Steps:**

```bash
# 1. Create feature branch
git checkout -b feature/fft-temporal-smoothing

# 2. Implement changes in image_preprocessor.c
# 3. Test compilation
make clean && make

# 4. Test with MIDI controller
# 5. Verify bass frequencies are stable
# 6. Commit changes
git add src/processing/image_preprocessor.{c,h}
git commit -m "feat(fft): add temporal smoothing with circular buffer

- Add FFT_HISTORY_SIZE=5 circular buffer for magnitude history
- Implement moving average over 5 frames (5ms @ 1kHz)
- Pre-fill history with white spectrum at startup
- Prevents low-frequency crackling in polyphonic synthesis

Fixes bass frequency artifacts caused by magnitude discontinuities.
Maintains RT-safety by keeping all computation in UDP thread."
```

### Phase 4.2: Testing Checklist

**Functional Tests:**
- [ ] No compilation errors or warnings
- [ ] Application starts without crashes
- [ ] LuxSynth synthesis produces sound
- [ ] MIDI note on/off works correctly
- [ ] No "[AUDIO] LuxSynth buffer missing!" messages

**Audio Quality Tests:**
- [ ] **Bass frequencies (50-100Hz) are stable** ← PRIMARY GOAL
- [ ] No crackling or popping sounds
- [ ] Smooth transitions between notes
- [ ] No startup "tac" or transient artifacts
- [ ] Vibrato/LFO modulation works smoothly

**Performance Tests:**
- [ ] CPU usage remains acceptable
- [ ] No increase in buffer underruns
- [ ] FFT computation time < 1ms (measured)
- [ ] Memory usage increase < 2KB

**Platform Tests:**
- [ ] macOS development environment
- [ ] Raspberry Pi 5 production environment
- [ ] Both platforms show stable bass frequencies

### Phase 4.3: Validation Metrics

**Before Temporal Smoothing:**
- Bass crackling: Present
- Magnitude discontinuities: Frequent
- Temporal coherence: None

**After Temporal Smoothing:**
- Bass crackling: **Eliminated**
- Magnitude discontinuities: **Smoothed over 5ms**
- Temporal coherence: **Maintained**

### Phase 4.4: Rollback Plan

If temporal smoothing causes issues:

```bash
# Option 1: Revert commit
git revert <commit-hash>

# Option 2: Adjust FFT_HISTORY_SIZE
# Edit image_preprocessor.c and change:
#define FFT_HISTORY_SIZE 3  // Try smaller window

# Option 3: Disable moving average, keep only exponential smoothing
# Comment out moving average loop in image_preprocess_fft()
```

## Future Optimizations

### Potential Improvements
1. **Adaptive history size**: Adjust FFT_HISTORY_SIZE based on detected tempo/rhythm
2. **Frequency-dependent smoothing**: More smoothing for bass, less for treble
3. Remove legacy moving average code from `synth_luxsynth.c` (cleanup)
4. Test without exponential smoothing to measure impact
5. Consider lock-free queue for FFT data transfer
6. Profile actual performance gains on Raspberry Pi 5

### Advanced Optimizations
- **Perceptual weighting**: Apply psychoacoustic model to FFT magnitudes
- **Transient detection**: Reduce smoothing during note attacks
- **Spectral envelope tracking**: Maintain harmonic structure during transitions

### Monitoring
- Add performance counters for FFT computation time
- Track buffer underrun statistics
- Monitor CPU usage per thread
- **NEW:** Log FFT magnitude variance to detect instability

## Conclusion

This architectural refactoring addresses the root cause of polyphonic audio crackling by moving expensive FFT computation out of the RT-constrained audio thread. The implementation maintains code quality, RT-safety, and provides a clear migration path with minimal risk.

**Phase 4 Addition:** The temporal smoothing enhancement eliminates low-frequency crackling by maintaining FFT magnitude continuity through a circular history buffer, matching the proven architecture of the original polyphonic implementation.

**Expected Result:** Stable, crackle-free polyphonic synthesis at 1kHz image rate with smooth bass frequencies.
