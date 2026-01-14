# Cleanup Status - Core Audio Only

**Branch**: `feature/vst-plugin`  
**Last Update**: 15/01/2026 00:50  
**Status**: 🟡 In Progress (69% complete)

---

## ✅ Completed Tasks

### 1. Files Deletion (100%)
- ✅ **22 files deleted, 4707 lines removed**
  - Display system (10 files)
  - DMX lighting (3 files)
  - Audio effects (8 files)
  - Image debug (2 files)

### 2. Build System (100%)
- ✅ **Makefile cleaned**
  - Removed AUDIO_EFFECTS_SOURCES
  - Removed DISPLAY_SOURCES
  - Removed DMX from COMMUNICATION_SOURCES
  - Removed image_debug from UTILS_SOURCES
  - Cleaned INCLUDES (no display/dmx/effects paths)
  - Cleaned LIBS (macOS: no SFML, Linux: no libftdi1)

### 3. Config Headers (100%)
- ✅ **src/core/config.h** - Removed config_display.h, config_dmx.h
- ✅ **src/core/context.h** - Simplified DMXContext stub, removed SFML blocks

### 4. Automated Include Cleanup (90%)
- ✅ **Script created**: `scripts/cleanup_includes.sh`
- ✅ **14 files processed** automatically
- ⚠️  **4 files need manual intervention**:
  - `src/core/main.c` (many code references, not just includes)
  - `src/config/config_parser_table.h`
  - `src/config/config_loader.h`
  - `src/communication/midi/midi_callbacks.h`

---

## 🟡 Remaining Tasks

### 1. src/core/main.c (Major Cleanup Required)

**Errors found** (14+ compilation errors):
```
Line 91:  keepRunning = 0; // From DMX - REMOVE
Line 118: DMX_PORT constant - REMOVE
Line 133: DMX_PORT in help - REMOVE
Line 243: image_debug_configure_raw_scanner() - REMOVE
Line 301: image_debug_configure_oscillator_capture() - REMOVE
Line 354: dmxCtx->colorUpdated - REMOVE
Line 357: dmxCtx->use_libftdi - REMOVE
Line 367: DMXSpot, DMX_NUM_SPOTS - REMOVE
Line 634: gAutoVolumeInstance = auto_volume_create() - REMOVE
Line 1006: auto_volume_destroy() - REMOVE
```

**Sections to remove:**
1. ❌ All SFML-related code blocks
2. ❌ All DMX initialization/cleanup
3. ❌ All auto-volume code
4. ❌ All image_debug calls
5. ❌ Command-line options: --display, --dmx, --debug-image, --debug-additive-osc-image
6. ❌ Main loop SFML event handling
7. ❌ Display rendering calls
8. ❌ DMX thread creation/join
9. ❌ Texture creation/cleanup

**Sections to keep:**
1. ✅ UDP receiver thread
2. ✅ Audio processing thread
3. ✅ MIDI initialization
4. ✅ Image preprocessor
5. ✅ Image sequencer (ESSENTIAL for audio!)
6. ✅ 3 synthesis engines init
7. ✅ IMU gesture system
8. ✅ Simple main loop (just sleep, no SFML events)

**Estimated LOC reduction**: ~1500 lines → ~400 lines (73% reduction)

### 2. src/audio/rtaudio/audio_rtaudio.cpp (Moderate Cleanup)

**Sections to remove:**
- ❌ ZitaRev1 reverb processing (already cleaned by script)
- ❌ EQ processing (three_band_eq)
- ❌ auto_volume processing
- ❌ Reverb member variables
- ❌ Reverb methods (enableReverb, setReverbMix, etc.)
- ❌ EQ member variables

**Sections to keep:**
- ✅ 3 synthesis buffer mixing
- ✅ Master volume
- ✅ Audio limiting
- ✅ Multi-channel raw outputs

**Est imated LOC reduction**: ~1200 lines → ~600 lines (50% reduction)

### 3. Other Files (Minor Cleanup)

**config_parser_table.h, config_loader.h, midi_callbacks.h**
- Check for display/DMX references
- Likely just config parsing code to stub

---

## 📋 Recommended Approach

### Option A: Incremental Fix (Conservative)
Fix errors one by one, preserving code structure.

**Time**: ~3-4 hours  
**Risk**: Low (careful refactoring)  
**Code quality**: Good (preserves architecture)

### Option B: Rewrite main.c (Aggressive)
Create new minimal main.c from scratch with just:
```c
int main() {
    // 1. Load config
    // 2. Init MIDI
    // 3. Init audio
    // 4. Init 3 synths
    // 5. Init image preprocessor + sequencer
    // 6. Start UDP thread
    // 7. Start audio thread
    // 8. Start synth threads
    // 9. Simple sleep loop
    // 10. Cleanup
}
```

**Time**: ~2 hours  
**Risk**: Medium (may break hidden dependencies)  
**Code quality**: Excellent (clean slate)

---

## 🎯 Next Immediate Steps

### Manual Cleanup Required

**1. main.c - Remove ALL DMX code:**
```bash
# Lines to delete or comment:
- Lines 91, 118, 133 (DMX references)
- Lines 327-379 (DMX initialization block)
- Lines 460-470 (DMX thread creation)
- Lines 797-802 (DMX thread join)
- Lines 873-885 (DMX color computation)
- All `use_dmx`, `dmxCtx`, `dmxFd` variables
```

**2. main.c - Remove ALL auto-volume code:**
```bash
# Lines to delete:
- Lines 626-640 (auto-volume init)
- Lines 1006-1009 (auto-volume cleanup)
- Variable `gAutoVolumeInstance`
```

**3. main.c - Remove ALL image_debug code:**
```bash
# Lines to delete:
- Lines 229-244 (--debug-image option)
- Lines 245-302 (--debug-additive-osc-image option)
- Lines 303-319 (--debug-additive-osc option)
```

**4. main.c - Remove ALL SFML/display code:**
```bash
# Blocks to delete:
- Lines 380-402 (SFML window creation)
- Lines 408-421 (Textures/sprites creation)
- Lines 648-663 (SFML clock)
- Lines 700-715 (SFML event loop)
- Lines 743-751 (Display rendering)
- Lines 760-776 (FPS counting)
- Lines 1023-1035 (SFML cleanup)
```

**5. Simplify main loop:**
```c
// From: ~100 lines of SFML events + rendering + DMX
// To:   ~5 lines
while (running && context.running && app_running) {
    usleep(10000);  // 10ms sleep
}
```

---

## 📊 Progress Summary

| Component | Status | LOC Changed |
|-----------|--------|-------------|
| File deletion | ✅ 100% | -4707 |
| Makefile | ✅ 100% | -20 |
| config.h | ✅ 100% | -2 |
| context.h | ✅ 100% | -50 |
| Include cleanup (auto) | ✅ 90% | -40 |
| main.c code cleanup | 🟡 0% | TBD -1100 |
| audio_rtaudio.cpp | 🟡 0% | TBD -600 |
| Final test & commit | ⏳ 0% | N/A |
| **Total** | **69%** | **-6519** (est.) |

---

## ⏭️ Continue From Here

When resuming:
1. Review this document
2. Choose approach (Option A or B for main.c)
3. Execute cleanup
4. Test compilation
5. Commit final "core audio only" version

All prep work is done - ready for final code cleanup phase!
