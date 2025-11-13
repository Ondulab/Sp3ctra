# Polyphonic FFT Optimization - Architecture Refactoring

**Date:** 2025-11-13  
**Author:** zhonx  
**Status:** ✅ Implemented and Compiled

## Problem Statement

### Symptoms
- Frequent "[AUDIO] Polyphonic buffer missing!" messages
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
// In synth_polyphonicMode_thread_func()
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

**Polyphonic Thread** (`synth_polyphonic.c`):
```
while (keepRunning) {
    // Read pre-computed FFT magnitudes from preprocessed_data
    read_preprocessed_fft_magnitudes(image_db);
    // Generate audio buffer (~0.3-0.5ms instead of 2ms)
    synth_polyphonicMode_process(...);
}
```

### Benefits
- ✅ FFT computed in non-RT thread (no strict time constraint)
- ✅ Polyphonic thread 4x faster (0.5ms vs 2ms)
- ✅ Coherent architecture (all preprocessing centralized)
- ✅ No moving average needed (FFT per image @ 1kHz is sufficient)
- ✅ No added audio latency

## Implementation Details

### 1. Data Structure Extension

**File:** `src/processing/image_preprocessor.h`

Added FFT data to `PreprocessedImageData`:
```c
#ifndef DISABLE_POLYPHONIC
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
- Conditional compilation with `#ifndef DISABLE_POLYPHONIC`

### 3. UDP Thread Integration

**File:** `src/threading/multithreading.c`

Added FFT preprocessing call after standard preprocessing:
```c
/* Step 2.5: Calculate FFT for polyphonic synthesis (if enabled) */
#ifndef DISABLE_POLYPHONIC
if (image_preprocess_fft(&preprocessed_temp) != 0) {
    log_warning("THREAD", "FFT preprocessing failed - polyphonic synthesis may glitch");
    preprocessed_temp.fft.valid = 0;  /* Mark FFT as invalid */
}
#endif
```

Also initialized FFT data in `initDoubleBuffer()`:
```c
#ifndef DISABLE_POLYPHONIC
memset(db->preprocessed_data.fft.magnitudes, 0, sizeof(db->preprocessed_data.fft.magnitudes));
db->preprocessed_data.fft.valid = 0;
#endif
```

### 4. Polyphonic Thread Simplification

**File:** `src/synthesis/polyphonic/synth_polyphonic.c`

**Removed:**
- `process_image_data_for_fft()` function (entire FFT computation)
- Moving average logic (no longer needed at 1kHz)
- Local FFT magnitude calculation in `synth_polyphonicMode_process()`

**Added:**
- `read_preprocessed_fft_magnitudes()` - simple reader function
- Direct copy from `preprocessed_data.fft.magnitudes[]` to `global_smoothed_magnitudes[]`

**Modified:**
- `synth_polyphonicMode_thread_func()` now calls `read_preprocessed_fft_magnitudes()`
- `synth_polyphonicMode_process()` simplified - just uses pre-computed magnitudes

## Performance Impact

### Expected Improvements

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Polyphonic thread time | ~2ms | ~0.5ms | **4x faster** |
| FFT computation rate | 500-600/sec | 1000/sec | **2x faster** |
| CPU usage (polyphonic) | High | Low | **-60-70%** |
| Buffer underruns | Frequent | None | **100% reduction** |

### Validation Criteria

After implementation, we should observe:
- ❌ Complete disappearance of "[AUDIO] Polyphonic buffer missing!" messages
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
**Decision:** All FFT preprocessing guarded by `#ifndef DISABLE_POLYPHONIC`

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
4. ✅ Simplify `synth_polyphonic.c` to use pre-computed data
5. ✅ Compile and verify no errors

### Phase 2: Testing (USER REQUIRED)
1. Run application with MIDI controller
2. Monitor for "[AUDIO] Polyphonic buffer missing!" messages
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
4. `src/synthesis/polyphonic/synth_polyphonic.c` - Simplified to use pre-computed FFT

## Rollback Plan

If issues arise:
```bash
git revert <commit-hash>
```

Or disable FFT preprocessing by defining `DISABLE_POLYPHONIC` in build flags.

## Future Optimizations

### Potential Improvements
1. Remove legacy moving average code (cleanup)
2. Test without smoothing to measure impact
3. Consider lock-free queue for FFT data transfer
4. Profile actual performance gains on Raspberry Pi 5

### Monitoring
- Add performance counters for FFT computation time
- Track buffer underrun statistics
- Monitor CPU usage per thread

## FFT Temporal Smoothing Architecture (Phase 2 - PLANNED)

**Date:** 2025-11-14  
**Status:** 📋 Planned - Not Yet Implemented

### Problem Analysis: Low-Frequency Crackling

#### Root Cause
While Phase 1 successfully moved FFT computation out of the RT thread, testing revealed **low-frequency crackling** (bass frequencies, bins 0-10) due to:

1. **Lack of temporal continuity**: Each UDP frame (1ms) computes FFT independently
2. **Abrupt magnitude transitions**: No smoothing between consecutive FFT frames
3. **Bass frequency sensitivity**: Low frequencies have long periods (50Hz = 20ms)
   - A sudden magnitude change lasts multiple complete cycles
   - Creates audible "clac" or "pop" sounds
4. **Temporal aliasing**: UDP frame rate (~1kHz) creates beating with bass frequencies

#### Why Bass Frequencies Crack Specifically

| Frequency | Period | Impact of Discontinuity |
|-----------|--------|------------------------|
| 50 Hz | 20ms | Discontinuity lasts 20 complete cycles |
| 100 Hz | 10ms | Discontinuity lasts 10 complete cycles |
| 500 Hz | 2ms | Discontinuity lasts 2 cycles (less audible) |
| 2000 Hz | 0.5ms | Discontinuity barely noticeable |

**Conclusion:** Bass frequencies need temporal smoothing to maintain continuity.

### Solution: Dedicated FFT Thread with Temporal Smoothing

#### Architecture Overview

**New Thread:** `fftProcessingThread` (non-RT, dedicated to FFT computation)

```
UDP Thread                FFT Thread              Polyphonic Thread
    |                         |                          |
    |--[Image received]------>|                          |
    |                         |--[FFT + smoothing]       |
    |                         |--[Store magnitudes]----->|
    |                         |                          |--[Generate audio]
    |                         |                          |
```

#### Key Components

**1. Circular History Buffer**
```c
#define FFT_HISTORY_SIZE 5  /* 5ms @ 1kHz - optimal for bass smoothing */

static struct {
    float history[FFT_HISTORY_SIZE][PREPROCESS_MAX_FFT_BINS];
    int write_index;
    int fill_count;
    pthread_mutex_t mutex;
} fft_history_state;
```

**2. FFT Processing Pipeline**
```c
void *fftProcessingThread(void *arg) {
    while (keepRunning) {
        // 1. Wait for new image data (condition variable)
        // 2. Compute FFT on grayscale
        // 3. Store in circular buffer
        // 4. Compute moving average over FFT_HISTORY_SIZE frames
        // 5. Apply exponential smoothing (AMPLITUDE_SMOOTHING_ALPHA = 0.1)
        // 6. Store smoothed magnitudes in preprocessed_data
        // 7. Signal polyphonic thread (data ready)
    }
}
```

**3. Temporal Smoothing Algorithm**
```c
// For each FFT bin:
for (int bin = 0; bin < MAX_FFT_BINS; bin++) {
    // Step 1: Moving average over history
    float sum = 0.0f;
    for (int h = 0; h < fft_history_state.fill_count; h++) {
        int idx = (write_index - 1 - h + FFT_HISTORY_SIZE) % FFT_HISTORY_SIZE;
        sum += fft_history_state.history[idx][bin];
    }
    float averaged = sum / fft_history_state.fill_count;
    
    // Step 2: Exponential smoothing
    float smoothed = ALPHA * averaged + (1.0f - ALPHA) * previous_magnitude[bin];
    
    // Step 3: Store result
    preprocessed_data.fft.magnitudes[bin] = smoothed;
}
```

### Implementation Plan

#### Phase 2.1: Data Structures (image_preprocessor.h)
```c
#ifndef DISABLE_POLYPHONIC

/* FFT history for temporal smoothing */
#define FFT_HISTORY_SIZE 5
#define AMPLITUDE_SMOOTHING_ALPHA 0.1f

typedef struct {
    float history[FFT_HISTORY_SIZE][PREPROCESS_MAX_FFT_BINS];
    int write_index;
    int fill_count;
    pthread_mutex_t mutex;
    pthread_cond_t data_ready;
    int initialized;
} FftHistoryState;

/* Global FFT history state */
extern FftHistoryState g_fft_history;

#endif
```

#### Phase 2.2: FFT Thread Implementation (multithreading.c)
```c
void *fftProcessingThread(void *arg) {
    Context *ctx = (Context *)arg;
    DoubleBuffer *db = ctx->doubleBuffer;
    
    // Pre-fill history with white line (prevents startup transients)
    fft_history_prefill_white();
    
    log_info("THREAD", "FFT processing thread started with temporal smoothing");
    
    while (ctx->running) {
        // Wait for new image data
        pthread_mutex_lock(&db->mutex);
        while (!db->dataReady && ctx->running) {
            pthread_cond_wait(&db->cond, &db->mutex);
        }
        
        if (!ctx->running) {
            pthread_mutex_unlock(&db->mutex);
            break;
        }
        
        // Get grayscale data
        float grayscale[MAX_PIXELS];
        memcpy(grayscale, db->preprocessed_data.grayscale, sizeof(grayscale));
        pthread_mutex_unlock(&db->mutex);
        
        // Compute FFT with temporal smoothing
        fft_compute_with_smoothing(grayscale, &db->preprocessed_data);
    }
    
    log_info("THREAD", "FFT processing thread terminated");
    return NULL;
}
```

#### Phase 2.3: Smoothing Function (image_preprocessor.c)
```c
int fft_compute_with_smoothing(const float *grayscale, PreprocessedImageData *out) {
    // 1. Compute raw FFT
    kiss_fftr(fft_cfg, fft_input, fft_output);
    
    // 2. Calculate raw magnitudes
    float raw_magnitudes[MAX_FFT_BINS];
    for (int i = 0; i < MAX_FFT_BINS; i++) {
        float real = fft_output[i].r;
        float imag = fft_output[i].i;
        raw_magnitudes[i] = sqrtf(real * real + imag * imag) / NORM_FACTOR;
    }
    
    // 3. Store in circular buffer
    pthread_mutex_lock(&g_fft_history.mutex);
    memcpy(g_fft_history.history[g_fft_history.write_index], 
           raw_magnitudes, sizeof(raw_magnitudes));
    g_fft_history.write_index = (g_fft_history.write_index + 1) % FFT_HISTORY_SIZE;
    if (g_fft_history.fill_count < FFT_HISTORY_SIZE) {
        g_fft_history.fill_count++;
    }
    
    // 4. Compute moving average + exponential smoothing
    for (int bin = 0; bin < MAX_FFT_BINS; bin++) {
        float sum = 0.0f;
        for (int h = 0; h < g_fft_history.fill_count; h++) {
            int idx = (g_fft_history.write_index - 1 - h + FFT_HISTORY_SIZE) % FFT_HISTORY_SIZE;
            sum += g_fft_history.history[idx][bin];
        }
        float averaged = sum / g_fft_history.fill_count;
        
        // Exponential smoothing
        static float prev_magnitudes[MAX_FFT_BINS] = {0};
        float smoothed = AMPLITUDE_SMOOTHING_ALPHA * averaged + 
                        (1.0f - AMPLITUDE_SMOOTHING_ALPHA) * prev_magnitudes[bin];
        prev_magnitudes[bin] = smoothed;
        
        out->fft.magnitudes[bin] = smoothed;
    }
    
    pthread_mutex_unlock(&g_fft_history.mutex);
    out->fft.valid = 1;
    return 0;
}
```

### Benefits of Dedicated FFT Thread

| Aspect | Benefit |
|--------|---------|
| **Temporal Smoothing** | 5-frame moving average eliminates bass crackling |
| **Thread Isolation** | FFT computation doesn't block UDP or audio threads |
| **Scalability** | Can adjust FFT_HISTORY_SIZE without affecting other threads |
| **RT-Safety** | No FFT computation in RT path (polyphonic thread) |
| **Debugging** | Isolated thread makes profiling and optimization easier |

### Performance Considerations

**Memory Overhead:**
- History buffer: 5 frames × 64 bins × 4 bytes = **1.3 KB** (negligible)
- Additional mutex/cond: **~100 bytes**
- **Total: ~1.4 KB** (acceptable)

**CPU Overhead:**
- Moving average: 5 additions + 1 division per bin = **~320 operations**
- Exponential smoothing: 1 multiply + 1 add per bin = **~128 operations**
- **Total: ~450 operations** (< 1µs on modern CPU)

**Latency:**
- FFT computation: ~1.5ms (unchanged)
- Smoothing: < 0.01ms (negligible)
- **Total: ~1.5ms** (acceptable for non-RT thread)

### Migration Steps

#### Step 1: Add FFT History State
- Extend `image_preprocessor.h` with `FftHistoryState`
- Initialize in `image_preprocess_init()`
- Add mutex and condition variable

#### Step 2: Implement FFT Thread
- Create `fftProcessingThread()` in `multithreading.c`
- Add thread handle to `Context` structure
- Start thread in `main.c` after UDP thread

#### Step 3: Implement Smoothing
- Create `fft_compute_with_smoothing()` in `image_preprocessor.c`
- Add `fft_history_prefill_white()` for startup
- Implement circular buffer logic

#### Step 4: Update UDP Thread
- Remove direct FFT call from `udpThread()`
- Signal FFT thread when new image arrives
- FFT thread computes and stores results

#### Step 5: Testing
- Verify bass frequencies no longer crack
- Monitor CPU usage (should be similar to Phase 1)
- Test on Raspberry Pi 5

### Rollback Plan

If issues arise:
1. Keep Phase 1 implementation (FFT in UDP thread)
2. Disable temporal smoothing with compile flag
3. Revert to original polyphonic thread architecture

### Expected Results

**Before (Phase 1):**
- ❌ Bass frequencies crack on magnitude transitions
- ❌ Audible "pops" when image changes rapidly
- ❌ Unstable low-frequency synthesis

**After (Phase 2):**
- ✅ Smooth bass frequency transitions
- ✅ No audible artifacts on image changes
- ✅ Stable, continuous low-frequency synthesis
- ✅ Professional audio quality

### Comparison with Original Polyphonic Architecture

| Feature | Original Polyphonic | Phase 1 (UDP FFT) | Phase 2 (Dedicated Thread) |
|---------|-------------------|------------------|---------------------------|
| FFT Location | Polyphonic thread | UDP thread | Dedicated FFT thread |
| Temporal Smoothing | ✅ Moving average (8 frames) | ❌ None | ✅ Moving average (5 frames) |
| RT-Safety | ❌ FFT in RT path | ✅ FFT in non-RT | ✅ FFT in non-RT |
| Bass Crackling | ✅ None | ❌ Present | ✅ None (expected) |
| CPU Efficiency | ❌ Low (RT blocking) | ✅ High | ✅ High |
| Architecture | ❌ Monolithic | ⚠️ Hybrid | ✅ Clean separation |

### Conclusion (Phase 2)

Phase 2 completes the FFT architecture refactoring by adding the missing temporal smoothing component. This addresses the bass crackling issue identified during Phase 1 testing while maintaining the performance benefits of moving FFT out of the RT thread.

**Key Innovation:** Dedicated FFT thread with circular history buffer provides optimal balance between:
- Temporal continuity (smooth bass frequencies)
- RT-safety (no FFT in audio callback)
- Performance (efficient thread isolation)
- Maintainability (clean architecture)

**Next Steps:**
1. Implement Phase 2 changes
2. Test on macOS development environment
3. Validate on Raspberry Pi 5 production environment
4. Document performance metrics and audio quality improvements

---

## Conclusion (Overall)

This architectural refactoring addresses the root cause of polyphonic audio crackling by moving expensive FFT computation out of the RT-constrained audio thread. The implementation maintains code quality, RT-safety, and provides a clear migration path with minimal risk.

**Phase 1 Result:** Stable polyphonic synthesis at 1kHz image rate (with minor bass crackling)  
**Phase 2 Result (Expected):** Professional-quality, crackle-free polyphonic synthesis with smooth bass frequencies
