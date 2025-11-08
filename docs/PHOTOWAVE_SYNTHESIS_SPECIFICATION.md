# PHOTOWAVE SYNTHESIS - TECHNICAL SPECIFICATION

**Version**: 1.0  
**Date**: 08/11/2025  
**Author**: Sp3ctra Architecture Team  
**Status**: Design Phase

---

## 1. OVERVIEW

### 1.1 Concept

The **Photowave** mode transforms an image line directly into an audio waveform through spatial→temporal transduction. Each pixel luminance value becomes an audio sample, creating a "dynamic optical wavetable".

**Fundamental Principle**:
```
Image Line (spatial)     →  Waveform (temporal)
Luminance [0-255]        →  Amplitude [-1.0, +1.0]
Line Width (pixels)      →  Wave Period (samples)
Reading Frequency        →  Note Pitch
```

### 1.2 System Integration

- **Position**: New synthesis mode (third engine after Additive and Polyphonic)
- **Enum**: Add `PHOTOWAVE_MODE` to `synthModeTypeDef`
- **Architecture**: Autonomous module reusing existing infrastructure
- **Compatibility**: Raspberry Pi 5 + macOS, real-time constraints respected

---

## 2. FUNCTIONAL SPECIFICATIONS

### 2.1 Main Parameters

| Parameter | Type | Values | Description |
|-----------|------|--------|-------------|
| **MIDI Note** | int | 0-127 | Pitch control (frequency) |
| **Scan Mode** | enum | L→R, R→L, DUAL | Line scanning mode |
| **Blur Amount** | float | 0.0-1.0 | Line smoothing (spatial low-pass filter) |
| **Interpolation** | enum | LINEAR, CUBIC | Resampling quality |
| **Volume** | float | 0.0-1.0 | Master volume |

### 2.2 Physical Frequency Range

**Minimum Frequency** = `sampling_rate / DPI_pixels`
- 400 DPI (3456 px) @ 48 kHz: **f_min = 13.89 Hz**
- 200 DPI (1728 px) @ 48 kHz: **f_min = 27.78 Hz**

**Maximum Frequency** = **12 kHz** (comfortable audible limit)

**MIDI Mapping**: Exponential over range [f_min, 12000 Hz]
- MIDI 0 → f_min
- MIDI 60 → ~580 Hz
- MIDI 127 → 12 kHz

**Formula**:
```c
float midi_to_freq(int midi_note) {
    float f_min = sample_rate / get_cis_pixels_nb();
    float f_max = 12000.0f;
    float norm = (float)midi_note / 127.0f;
    float log_min = logf(f_min);
    float log_max = logf(f_max);
    return expf(log_min + norm * (log_max - log_min));
}
```

### 2.3 Scanning Modes

#### Mode 1: Left-to-Right (L→R)
- Sequential reading: `pixels[0] → pixels[width-1]`
- Phase: `0.0 → 1.0` (linear)
- Repetition: continuous, wraps to beginning

#### Mode 2: Right-to-Left (R→L)
- Reverse reading: `pixels[width-1] → pixels[0]`
- Phase: `1.0 → 0.0`
- Equivalent to horizontal flip of waveform

#### Mode 3: Dual/Ping-Pong (DUAL)
- Rising phase: `pixels[0] → pixels[width-1]`
- Falling phase: `pixels[width-1] → pixels[0]`
- Effective period: `2 × width`
- Symmetric waveform (eliminates even harmonics)

### 2.4 Spatial Blur Filter

- **Purpose**: Reduce high-frequency content of waveform
- **Method**: Moving average (sliding window)
- **Kernel Size**: Proportional to `blur_amount` (1 to 11 pixels)
- **Implementation**: Circular wrap for phase continuity
- **RT Constraint**: Pre-computation outside audio callback (dedicated thread)

### 2.5 Real-Time Constraints

✅ **Mandatory**:
- No dynamic allocation in audio callback
- No mutex/locks in hot path
- Preallocated buffers (init-time)
- CPU budget ≤ 50% of buffer duration
- Zero underruns in nominal conditions

✅ **Recommendations**:
- O(1) interpolation per sample
- Double buffering for line changes
- Atomics for synchronization flags

---

## 3. SOFTWARE ARCHITECTURE

### 3.1 Modular Structure

```
src/synthesis/photowave/
├── synth_photowave.h                    # Public API
├── synth_photowave.c                    # Main implementation
├── synth_photowave_state.h              # State structures
├── synth_photowave_state.c              # State management (init/cleanup)
├── synth_photowave_interpolation.h      # Linear/cubic interpolation
├── synth_photowave_interpolation.c
├── synth_photowave_blur.h               # Spatial blur filter
├── synth_photowave_blur.c
└── synth_photowave_runtime.h            # RT-safe parameters (atomics)
```

### 3.2 Key Data Structures

**PhotowaveScanMode** (enum)
```c
typedef enum {
    PHOTOWAVE_SCAN_LR = 0,      // Left to Right
    PHOTOWAVE_SCAN_RL,          // Right to Left
    PHOTOWAVE_SCAN_DUAL         // Ping-pong (both directions)
} PhotowaveScanMode;
```

**PhotowaveInterpMode** (enum)
```c
typedef enum {
    PHOTOWAVE_INTERP_LINEAR = 0,
    PHOTOWAVE_INTERP_CUBIC
} PhotowaveInterpMode;
```

**PhotowaveState** (main RT-safe structure)
```c
typedef struct {
    // Reading phase [0.0, 1.0 or 2.0]
    float phase;
    float phase_increment;       // Computed from MIDI note
    
    // Configuration
    PhotowaveScanMode scan_mode;
    PhotowaveInterpMode interp_mode;
    int midi_note;               // MIDI note [0-127]
    float blur_amount;           // Blur factor [0.0-1.0]
    
    // Image data
    float *line_data_raw;        // Raw line (from DoubleBuffer)
    float *line_data_blurred;    // Line after blur
    int line_width;              // Width (DPI-dependent)
    
    // Double buffering for line changes
    float *line_buffer_a;
    float *line_buffer_b;
    float *blur_buffer_a;
    float *blur_buffer_b;
    volatile int active_buffer;  // Active buffer index (atomic)
    
    float volume;                // Master volume [0.0, 1.0]
} PhotowaveState;
```

**PhotowaveConfig** (global configuration)
```c
typedef struct {
    PhotowaveScanMode scan_mode;     // Default: L→R
    PhotowaveInterpMode interp_mode; // Default: LINEAR
    float blur_amount;               // Default: 0.0
    float volume;                    // Default: 0.7
    int midi_note_default;           // Default: 60 (C4)
} PhotowaveConfig;
```

### 3.3 Public API (Headers)

**Initialization/Cleanup**:
```c
int32_t synth_photowave_init(void);
void synth_photowave_cleanup(void);
```

**Audio Processing (RT-critical)**:
```c
void synth_photowave_process(float *audio_left, 
                              float *audio_right,
                              unsigned int buffer_size,
                              struct DoubleBuffer *db);
```

**Configuration (thread-safe)**:
```c
void synth_photowave_set_midi_note(int midi_note);
void synth_photowave_set_scan_mode(PhotowaveScanMode mode);
void synth_photowave_set_interpolation(PhotowaveInterpMode mode);
void synth_photowave_set_blur_amount(float amount);
void synth_photowave_set_volume(float volume);
```

**Getters**:
```c
float synth_photowave_get_current_frequency(void);
PhotowaveScanMode synth_photowave_get_scan_mode(void);
int synth_photowave_get_midi_note(void);
```

### 3.4 Data Flow

```
[Image Preprocessor]
        ↓
   [DoubleBuffer] ← Complete grayscale line
        ↓
[Photowave Thread] ← Non-RT thread
   • Copy line to inactive buffer
   • Apply blur if necessary
   • Atomic buffer swap
        ↓
[Audio Callback] ← RT thread
   • Read active buffer
   • Linear/cubic interpolation
   • Phase increment & wrap
   • Generate stereo samples
        ↓
   [RtAudio Output]
```

### 3.5 Integration with Existing System

**Required Modifications**:

1. **`src/core/context.h`**:
   - Add `PHOTOWAVE_MODE` to `synthModeTypeDef` enum

2. **`src/core/main.c`**:
   - Add `PHOTOWAVE_MODE` case in mode selection switch
   - Call `synth_photowave_process()` in callback

3. **`src/communication/midi/midi_callbacks.cpp`**:
   - Route Note On/Off to `synth_photowave_set_midi_note()`
   - Route CC to Photowave parameters

4. **`src/config/config_loader.c`**:
   - Parse `[photowave]` section in `sp3ctra.ini`

5. **`Makefile`**:
   - Add new files to SOURCES

---

## 4. MIDI INTEGRATION

### 4.1 Supported MIDI Messages

**Note On** (0x9n):
- Changes frequency via MIDI→frequency mapping
- Velocity ignored (constant volume or controlled by CC7)

**Note Off** (0x8n):
- Optional: can trigger fade-out or be ignored

**Control Change** (0xBn):
| CC# | Parameter | Range | Resolution |
|-----|-----------|-------|------------|
| 1 | Scan Mode | 0-2 | 3 discrete values |
| 7 | Volume | 0-127 | Linear 0.0-1.0 |
| 71 | Blur Amount | 0-127 | Linear 0.0-1.0 |
| 74 | Interpolation | 0-1 | 2 discrete values |

**Pitch Bend** (0xEn):
- Fine pitch modulation (±2 semitones recommended)

### 4.2 MIDI Configuration

Add to `midi_mapping.ini`:
```ini
[photowave]
cc_scan_mode = 1
cc_volume = 7
cc_blur = 71
cc_interpolation = 74
pitch_bend_range = 2
```

---

## 5. CONFIGURATION (.ini)

Add section to `sp3ctra.ini`:

```ini
[photowave]
enabled = 1
scan_mode = 0              # 0=L→R, 1=R→L, 2=DUAL
interpolation = 0          # 0=LINEAR, 1=CUBIC
blur_amount = 0.0          # 0.0 to 1.0
volume = 0.7               # 0.0 to 1.0
midi_note_default = 60     # C4 (default pitch)
```

---

## 6. DEVELOPMENT PLAN

### Phase 1: Base Structure [PRIORITY: CRITICAL]
**Estimated Duration**: 2-3 hours

**Tasks**:
- [ ] Create directory `src/synthesis/photowave/`
- [ ] Create main headers (`.h`)
  - `synth_photowave.h` (public API)
  - `synth_photowave_state.h` (structures)
  - `synth_photowave_interpolation.h`
  - `synth_photowave_blur.h`
- [ ] Define enums (`PhotowaveScanMode`, `PhotowaveInterpMode`)
- [ ] Define structures (`PhotowaveState`, `PhotowaveConfig`)
- [ ] Add `PHOTOWAVE_MODE` to `src/core/context.h`

**Deliverables**:
- Complete headers with documentation
- Compiles without errors (empty stubs OK)

---

### Phase 2: Init/Cleanup Implementation [PRIORITY: HIGH]
**Estimated Duration**: 2 hours

**Tasks**:
- [ ] Implement `synth_photowave_init()`
  - Allocate buffers (line_buffer_a/b, blur_buffer_a/b)
  - Size = `get_cis_pixels_nb()` (DPI-dependent)
  - Initialize default state
- [ ] Implement `synth_photowave_cleanup()`
  - Free memory
  - Reset global state
- [ ] Add to `Makefile`
- [ ] Test compilation macOS + Raspberry Pi

**Constraints**:
- Check allocation success (return -1 on failure)
- Log init/cleanup events

---

### Phase 3: Core Generation Algorithm [PRIORITY: CRITICAL]
**Estimated Duration**: 4-5 hours

**Tasks**:
- [ ] Implement `synth_photowave_process()` (simple version)
  - Read line from `DoubleBuffer`
  - Generate samples with phase increment
  - Basic linear interpolation
  - L→R mode only (to start)
- [ ] Implement `phase_increment` calculation
  - Function `photowave_midi_to_freq()`
  - Exponential MIDI mapping→[f_min, 12kHz]
  - Convert freq→phase_increment
- [ ] Implement phase wrapping
- [ ] Test with simple line (ramp, sine wave)

**Tests**:
- Verify correct pitch (frequency analyzer)
- Verify no underruns
- Measure CPU load (must be <30%)

---

### Phase 4: Scanning Modes and Blur [PRIORITY: MEDIUM]
**Estimated Duration**: 3-4 hours

**Tasks**:
- [ ] Implement R→L and DUAL modes in `process()`
  - Position calculation per scan_mode
  - Test correct wrapping
- [ ] Implement blur filter (`synth_photowave_blur.c`)
  - Moving average with variable kernel
  - Circular wrap
  - Optimization (avoid divisions)
- [ ] Integrate blur in pipeline (non-RT thread)
- [ ] Implement thread-safe setters

**Tests**:
- Verify DUAL mode symmetry
- Verify progressive smoothing with blur_amount

---

### Phase 5: Cubic Interpolation [PRIORITY: LOW]
**Estimated Duration**: 2 hours

**Tasks**:
- [ ] Implement Catmull-Rom cubic interpolation
- [ ] Compare quality vs linear (spectral analysis)
- [ ] Measure CPU overhead
- [ ] Make selectable via config

**Acceptance Criteria**:
- CPU overhead < 10% vs linear
- Measurable aliasing reduction

---

### Phase 6: MIDI Integration [PRIORITY: HIGH]
**Estimated Duration**: 3 hours

**Tasks**:
- [ ] Add callbacks in `src/communication/midi/midi_callbacks.cpp`
  - Note On → `synth_photowave_set_midi_note()`
  - CC1 → `synth_photowave_set_scan_mode()`
  - CC7 → `synth_photowave_set_volume()`
  - CC71 → `synth_photowave_set_blur_amount()`
- [ ] Implement pitch bend (optional)
- [ ] Add section to `midi_mapping.ini`
- [ ] Test with physical MIDI controller

**Tests**:
- Verify note change responsiveness (<10ms)
- Verify no glitches during parameter changes

---

### Phase 7: Configuration and Persistence [PRIORITY: MEDIUM]
**Estimated Duration**: 2 hours

**Tasks**:
- [ ] Add `[photowave]` section parsing in `config_loader.c`
- [ ] Create `config_photowave.h` structure
- [ ] Load parameters at startup
- [ ] Test default values + overrides

---

### Phase 8: Main Loop Integration [PRIORITY: CRITICAL]
**Estimated Duration**: 2 hours

**Tasks**:
- [ ] Add `PHOTOWAVE_MODE` case in `src/core/main.c`
- [ ] Route to `synth_photowave_process()` in callback
- [ ] Implement mode switching (IFFT/DWAVE/PHOTOWAVE)
- [ ] Test transitions without crashes

---

### Phase 9: Testing and Validation [PRIORITY: HIGH]
**Estimated Duration**: 4-6 hours

**Unit Tests**:
- [ ] Test MIDI→frequency conversion (precision)
- [ ] Test linear interpolation (error < 1%)
- [ ] Test phase wrapping (continuity)
- [ ] Test scanning modes (correct outputs)

**Integration Tests**:
- [ ] Test on Raspberry Pi 5
  - No xruns with buffer 256@48kHz
  - CPU < 40% of one core
  - Stable temperatures
- [ ] Test on macOS
  - Acceptable latency
  - No crackling
- [ ] Test live parameter changes
  - No glitches
  - Smooth transitions

**Robustness Tests**:
- [ ] Rapid MIDI note changes
- [ ] Scan mode change during playback
- [ ] Extreme images (all black, all white, noise)

---

### Phase 10: Documentation [PRIORITY: MEDIUM]
**Estimated Duration**: 3 hours

**Tasks**:
- [ ] Document API in headers (Doxygen style)
- [ ] Create `docs/PHOTOWAVE_USER_GUIDE.md`
  - Concept explanation
  - Parameters and their effects
  - Usage examples
  - Creative tips
- [ ] Update main `README.md`
- [ ] Create test image examples

---

## 7. GLOBAL ESTIMATION

**Total Time**: ~30-35 hours of development

**Breakdown**:
- Core implementation: 15h (Phases 1-4)
- Advanced features: 8h (Phases 5-7)
- Integration: 4h (Phases 6, 8)
- Testing: 6h (Phase 9)
- Documentation: 3h (Phase 10)

**Recommended Order**: 1 → 2 → 3 → 6 → 8 → 4 → 7 → 9 → 5 → 10

---

## 8. SUCCESS CRITERIA

✅ **Functional**:
- Correct audio generation from image line
- Pitch controllable via MIDI (0-127)
- 3 functional scanning modes
- Operational spatial blur

✅ **Performance** (Raspberry Pi 5):
- Zero xruns in nominal conditions
- CPU < 50% of one core
- Latency < 10ms (buffer 256@48kHz)

✅ **Quality**:
- No clicks/pops during changes
- Correct phase continuity
- Minimal aliasing

✅ **Code**:
- RT constraints respected
- Code commented in English
- clang-format compliance
- Zero compilation warnings

---

## 9. DEPENDENCIES AND RISKS

**Dependencies**:
- Existing `DoubleBuffer` (preprocessed line)
- RtAudio callback infrastructure
- MIDI routing system
- Configuration parser

**Identified Risks**:

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Audio underruns | Medium | Critical | Early profiling, NEON optimizations if needed |
| Audible aliasing | High | Medium | Cubic interpolation + blur filter |
| Note change latency | Low | Medium | Double buffering, atomics |
| DPI compatibility | Low | Medium | Use `get_cis_pixels_nb()` everywhere |

---

## 10. ALGORITHM PSEUDOCODE

### 10.1 Main Processing Loop

```c
void synth_photowave_process(float *left, float *right, 
                              unsigned int samples,
                              DoubleBuffer *db) {
    PhotowaveState *state = &g_photowave_state;
    
    for (int i = 0; i < samples; i++) {
        // 1. Calculate position in line
        float position;
        if (state->scan_mode == PHOTOWAVE_SCAN_LR) {
            position = state->phase * state->line_width;
        }
        else if (state->scan_mode == PHOTOWAVE_SCAN_RL) {
            position = (1.0f - state->phase) * state->line_width;
        }
        else { // DUAL
            float p = state->phase;
            if (p < 0.5f) {
                position = (p * 2.0f) * state->line_width;
            } else {
                position = ((1.0f - p) * 2.0f) * state->line_width;
            }
        }
        
        // 2. Interpolate luminance value
        float sample = interpolate_line(state->line_data, 
                                       position, 
                                       state->line_width,
                                       state->interp_mode);
        
        // 3. Convert to audio amplitude [-1.0, +1.0]
        float amplitude = (sample * 2.0f) - 1.0f;
        
        // 4. Write to stereo buffers
        left[i] = amplitude * state->volume;
        right[i] = amplitude * state->volume;
        
        // 5. Increment phase
        state->phase += state->phase_increment;
        
        // 6. Wrap phase
        float period = (state->scan_mode == PHOTOWAVE_SCAN_DUAL) ? 2.0f : 1.0f;
        if (state->phase >= period) {
            state->phase -= period;
        }
    }
}
```

### 10.2 Linear Interpolation (RT-safe)

```c
static inline float interpolate_linear(const float *data, 
                                      float position, 
                                      int width) {
    int index0 = (int)position;
    int index1 = (index0 + 1) % width;  // Circular wrap
    float frac = position - (float)index0;
    
    return data[index0] * (1.0f - frac) + data[index1] * frac;
}
```

### 10.3 Blur Filter (Non-RT thread)

```c
void apply_blur(float *line_in, float *line_out, int width, float amount) {
    if (amount < 0.01f) {
        memcpy(line_out, line_in, width * sizeof(float));
        return;
    }
    
    int kernel = (int)(amount * 10.0f) + 1;  // 1 to 11
    float kernel_inv = 1.0f / (float)kernel;
    
    for (int i = 0; i < width; i++) {
        float sum = 0.0f;
        for (int k = -kernel/2; k <= kernel/2; k++) {
            int idx = (i + k + width) % width;  // Circular wrap
            sum += line_in[idx];
        }
        line_out[i] = sum * kernel_inv;
    }
}
```

---

## 11. REFERENCES

- **Existing Synthesis Modes**: `src/synthesis/additive/`, `src/synthesis/polyphonic/`
- **Audio Infrastructure**: `src/audio/rtaudio/`
- **MIDI System**: `src/communication/midi/`
- **Configuration**: `src/config/config_loader.c`
- **Real-Time Guidelines**: `.clinerules/custom_instructions.md`

---

**END OF SPECIFICATION**
