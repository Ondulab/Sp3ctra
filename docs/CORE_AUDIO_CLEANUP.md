# Sp3ctra Core Audio - Cleanup Summary

**Branch**: `feature/vst-plugin`  
**Date**: 15/01/2026  
**Objective**: Remove all non-essential components to create a minimal "Core Audio" version focusing on spectral synthesis only

---

## 🗑️ Files Removed (22 files)

### Display System (10 files)
- `src/display/display.c`
- `src/display/display.h`
- `src/display/display_buffer.c`
- `src/display/display_buffer.h`
- `src/core/display_globals.c`
- `src/core/display_globals.h`
- `src/config/config_display.h`
- `src/config/config_display_loader.c`
- `src/config/config_display_loader.h`
- SFML dependencies completely removed

### DMX Lighting (3 files)
- `src/communication/dmx/dmx.c`
- `src/communication/dmx/dmx.h`
- `src/config/config_dmx.h`

### Audio Effects (8 files)
- `src/audio/effects/ZitaRev1.cpp` (reverb)
- `src/audio/effects/ZitaRev1.h`
- `src/audio/effects/three_band_eq.cpp` (equalizer)
- `src/audio/effects/three_band_eq.h`
- `src/audio/effects/pareq.cpp`
- `src/audio/effects/pareq.h`
- `src/audio/effects/auto_volume.c`
- `src/audio/effects/auto_volume.h`

### Debug Utilities (2 files)
- `src/utils/image_debug.c`
- `src/utils/image_debug.h`

---

## ✅ Files Kept (Core Audio Pipeline)

### Synthesis Engines (CRITICAL)
```
src/synthesis/
├── luxstral/           ✅ Additive synthesis (spectral)
├── luxsynth/           ✅ Polyphonic FFT synthesis
├── luxwave/            ✅ Photowave synthesis
└── common/             ✅ Voice manager
```

### Audio Processing (ESSENTIAL)
```
src/processing/         ✅ KEPT - Essential for spectral audio!
├── image_preprocessor.* → Preprocessing CIS data
├── image_sequencer.*    → Spectral buffer sequencing with ADSR
└── imu_gesture.*        → Real-time gestural control
```

### Audio Backend
```
src/audio/
├── rtaudio/            ✅ RtAudio for testing (will be replaced by DAW in VST)
├── buffers/            ✅ Audio buffer management
└── pan/                ✅ Lock-free panning
```

### Communication
```
src/communication/
├── network/udp.*       ✅ UDP receiver for CIS data (3456 bytes @ 1kHz)
└── midi/               ✅ MIDI control system
```

### Core & Utils
```
src/core/               ✅ Main application, context, config
src/threading/          ✅ Thread management
src/utils/              ✅ Logger, error handling, RT profiler
```

---

## 📝 Code Modifications Required

### 1. src/core/main.c
**Remove:**
- All SFML includes (`#include <SFML/...>`)
- SFML window creation/management
- DMX context and thread
- Display rendering logic
- Options: `--display`, `--dmx`, `--sfml-window`

**Keep:**
- UDP receiver thread
- Audio processing thread
- Image sequencer (ESSENTIAL for audio!)
- Image preprocessor (ESSENTIAL for audio!)
- MIDI system
- 3 synthesis engines
- IMU gesture system

**Simplify main loop:**
```c
// From: Complex SFML event loop + rendering + DMX updates
// To:   Simple sleep loop waiting for Ctrl+C
while (running && context.running && app_running) {
    usleep(10000);  // 10ms
}
```

### 2. src/audio/rtaudio/audio_rtaudio.cpp
**Remove:**
- ZitaRev1 includes and reverb processing
- Three-band EQ includes and EQ processing
- Auto-volume includes and auto-volume processing
- `processReverbOptimized()` function
- `eq_Process()` calls
- Reverb-related member variables and methods

**Keep:**
- 3 synthesis engine mix levels
- Master volume control
- Audio limiting (-1.0 to +1.0)
- Multi-channel raw outputs (if enabled)
- Lock-free buffer synchronization

**Simplified callback:**
```cpp
handleCallback() {
    // 1. Read from 3 synthesis buffers (lock-free)
    // 2. Mix with respective levels
    // 3. Apply master volume
    // 4. Limit output
    // 5. Write to output buffer
}
```

### 3. Makefile
**Remove from sources:**
- `DISPLAY_SOURCES` (entire section)
- `AUDIO_EFFECTS_SOURCES` (entire section)
- DMX sources from `COMMUNICATION_SOURCES`

**Remove from LIBS (macOS):**
- `-lsfml-graphics -lsfml-window -lsfml-system`
- `-lcsfml-graphics -lcsfml-window -lcsfml-system`
- SFML_PATH and SFML_INCLUDE variables

**Keep:**
- RtAudio, RtMidi
- FFTW3, libsndfile
- CoreAudio frameworks (macOS)
- Processing sources (image_preprocessor, image_sequencer, imu_gesture)

---

## 🎯 Result: "Sp3ctra Core Audio"

### Architecture
```
┌──────────────────────────────────────────────────────┐
│  Sp3ctra Core Audio (Headless)                       │
│                                                      │
│  ┌─────────────┐    ┌──────────────────────────┐    │
│  │ UDP         │───→│ Image Preprocessor       │    │
│  │ CIS Data    │    │ (spectral preparation)   │    │
│  │ 3456×1kHz   │    └────────────┬─────────────┘    │
│  └─────────────┘                 │                  │
│                                  ↓                  │
│  ┌─────────────┐    ┌──────────────────────────┐    │
│  │ MIDI        │───→│ Image Sequencer          │    │
│  │ Control     │    │ (ADSR, timing, buffers)  │    │
│  └─────────────┘    └────────────┬─────────────┘    │
│                                  │                  │
│  ┌─────────────┐    ┌────────────▼─────────────┐    │
│  │ IMU         │───→│ 3 Synthesis Engines      │    │
│  │ Gestures    │    │ • LuxStral (additive)    │──→ Audio
│  └─────────────┘    │ • LuxSynth (polyphonic)  │    │ Out
│                     │ • LuxWave (photowave)    │    │
│                     └──────────────────────────┘    │
│                                                      │
│  RtAudio Backend (testing before VST migration)     │
└──────────────────────────────────────────────────────┘
```

### Key Features Preserved
- ✅ **UDP spectral injection** (3456 values @ 1kHz)
- ✅ **Image preprocessing** (CIS data preparation)
- ✅ **Image sequencer** (ADSR, buffer management)
- ✅ **3 synthesis engines** (complete and unmodified)
- ✅ **MIDI control** (note on/off, CC mapping)
- ✅ **IMU gestures** (real-time control)
- ✅ **Lock-free buffers** (RT-safe)
- ✅ **Voice management** (polyphonic)

### Features Removed
- ❌ SFML display/visualization
- ❌ DMX lighting control
- ❌ Reverb processing
- ❌ Equalizer
- ❌ Auto-volume
- ❌ Image debug output

---

## 📊 Code Size Reduction

**Estimated reduction:**
- Source files: -22 files (~30% reduction)
- main.c: ~1500 lines → ~400 lines (73% reduction)
- audio_rtaudio.cpp: ~1200 lines → ~600 lines (50% reduction)
- Binary size: ~8MB → ~4MB (estimated)

**Dependency reduction:**
- No SFML (~40MB saved)
- No reverb DSP
- No EQ processing

---

## 🚀 Next Steps

1. ✅ Files removed (22 files deleted)
2. ⏳ Simplify `src/core/main.c`
3. ⏳ Clean `src/audio/rtaudio/audio_rtaudio.cpp`
4. ⏳ Update `Makefile`
5. ⏳ Test compilation
6. ⏳ Test functionality (UDP → spectral synthesis → audio)
7. ⏳ Commit "core audio only" version
8. ⏳ Document for VST migration

---

## 🎯 Benefits for VST Migration

This cleanup provides:
- **Clean codebase** focusing only on audio DSP
- **No graphics dependencies** to port
- **Reusable synthesis engines** (90% code reuse in VST)
- **Clear separation** between RtAudio (test) and synthesis (core)
- **Minimal dependencies** (easier to integrate in iPlug2)
- **Validated audio pipeline** before VST work

The spectral synthesis pipeline (UDP → preprocessing → sequencer → synthesis) remains **100% intact** and will be directly reusable in the VST plugin.
