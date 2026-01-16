# Debugging Sp3ctra VST/Standalone

This guide explains how to test and debug Sp3ctra without using a DAW, using the standalone application.

## Quick Start

### 1. Build and Run Standalone

```bash
# Build Release and launch
./scripts/build_vst.sh run

# Build Debug and launch
./scripts/build_vst.sh debug run

# Or use the dedicated launcher
./scripts/run_standalone.sh          # Launch Release
./scripts/run_standalone.sh debug    # Launch Debug
```

### 2. Run Existing Build

```bash
# Launch standalone directly
./scripts/run_standalone.sh

# Available options
./scripts/run_standalone.sh release  # Launch Release (default)
./scripts/run_standalone.sh debug    # Launch Debug
./scripts/run_standalone.sh lldb     # Launch with debugger
```

## Debugging with LLDB

### Interactive Debugging Session

```bash
# Launch with debugger
./scripts/run_standalone.sh lldb
```

This will start lldb and load the Sp3ctra executable. Use these commands:

```lldb
# Start the application
(lldb) run

# Set breakpoints (BEFORE running)
(lldb) b PluginProcessor::processBlock
(lldb) b PluginProcessor::prepareToPlay
(lldb) b Sp3ctraAudioProcessor::Sp3ctraAudioProcessor

# After hitting a breakpoint
(lldb) continue      # Resume execution (alias: c)
(lldb) step          # Step into (alias: s)
(lldb) next          # Step over (alias: n)
(lldb) finish        # Step out (alias: fin)

# Inspect variables
(lldb) print sampleRate
(lldb) print testTonePhase
(lldb) frame variable  # Show all local variables

# View call stack
(lldb) bt            # Backtrace

# Quit
(lldb) quit
```

### Manual LLDB Launch

```bash
# Direct path to standalone executable
lldb vst/build/Sp3ctraVST_artefacts/Standalone/Sp3ctra.app/Contents/MacOS/Sp3ctra
```

## Debugging with Xcode

### Option 1: Attach to Process

1. Build the standalone:
   ```bash
   ./scripts/build_vst.sh debug
   ```

2. Launch the standalone:
   ```bash
   ./scripts/run_standalone.sh debug
   ```

3. In Xcode:
   - Debug → Attach to Process → Sp3ctra
   - Set breakpoints in your source files
   - Interact with the app to trigger breakpoints

### Option 2: Generate Xcode Project (Advanced)

```bash
cd vst/build
cmake -G Xcode ..
open Sp3ctraVST.xcodeproj
```

Then in Xcode:
- Select the Sp3ctraVST_Standalone scheme
- Set breakpoints
- Run (⌘R)

## Viewing Console Output

### Standard Output/Logs

```bash
# Run standalone and see logs in terminal
./scripts/run_standalone.sh debug

# Or pipe to file
./scripts/run_standalone.sh debug 2>&1 | tee debug.log
```

### Console.app (macOS System Logs)

1. Open Console.app
2. Launch standalone
3. Filter by "Sp3ctra" to see system-level logs

## Testing MIDI Input

The standalone includes JUCE's built-in MIDI device selector:

1. Launch standalone
2. Go to **Options → Audio/MIDI Settings**
3. Select your MIDI input device
4. Play notes on your MIDI controller
5. Set breakpoints in `processBlock` to inspect MIDI messages

## Testing Audio Output

### Built-in Test Tone

The current implementation plays a 440Hz test tone:

```cpp
// In PluginProcessor.cpp - processBlock()
// This should generate a constant sine wave
```

To hear it:
1. Launch standalone
2. Select audio output device in Options
3. Adjust volume
4. You should hear a 440Hz tone

### Monitoring Audio Callback Performance

Add debug output (only in Debug builds):

```cpp
void Sp3ctraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, 
                                          juce::MidiBuffer& midiMessages)
{
    #if JUCE_DEBUG
        auto start = std::chrono::high_resolution_clock::now();
    #endif
    
    // Your audio processing...
    
    #if JUCE_DEBUG
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        DBG("processBlock took: " << duration.count() << " µs");
    #endif
}
```

## Common Debugging Scenarios

### 1. No Audio Output

**Check:**
- Audio device selected in Options?
- Buffer is being filled (set breakpoint in processBlock)?
- Volume is up?
- testTonePhase is incrementing?

### 2. Crackling/Distortion

**Check:**
- processBlock execution time (should be < 50% of buffer duration)
- No allocations in audio callback
- No locks in audio callback
- Buffer sizes are correct

### 3. Crashes

**Use lldb:**
```bash
./scripts/run_standalone.sh lldb
(lldb) run
# Wait for crash
(lldb) bt  # Show backtrace
```

### 4. MIDI Not Working

**Check:**
- MIDI device connected and selected
- Set breakpoint in processBlock to inspect midiMessages
- Check midiMessages.getNumEvents()

## Real-Time Audio Constraints

⚠️ **Never in processBlock():**
- Dynamic allocation (malloc/new)
- Locks (mutex, critical sections)
- Blocking I/O
- Logging to console (use lock-free ring buffer)
- std::cout/printf

✅ **Allowed in processBlock():**
- Bounded atomic operations
- Lock-free queues
- Preallocated buffers
- O(1) per-sample operations

## Performance Profiling

### Xcode Instruments

1. Build Release with debug symbols:
   ```bash
   ./scripts/build_vst.sh
   ```

2. Profile with Instruments:
   ```bash
   instruments -t "Time Profiler" vst/build/Sp3ctraVST_artefacts/Release/Standalone/Sp3ctra.app
   ```

### Manual Timing

Add to your code:

```cpp
#include <chrono>

void Sp3ctraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Expected callback duration (50% safety margin)
    auto expectedDuration = (samplesPerBlock / sampleRate) * 0.5;
    DBG("Max callback time: " << expectedDuration << " seconds");
}
```

## Workflow Recommendations

### Development Cycle

1. **Make changes** to source code
2. **Quick rebuild + test:**
   ```bash
   ./scripts/build_vst.sh debug run
   ```
3. **If crashes**, use lldb:
   ```bash
   ./scripts/run_standalone.sh lldb
   ```
4. **For performance**, build Release:
   ```bash
   ./scripts/build_vst.sh run
   ```

### Before Committing

```bash
# Clean build both configs
./scripts/build_vst.sh clean
./scripts/build_vst.sh debug
./scripts/build_vst.sh

# Test standalone
./scripts/run_standalone.sh debug
./scripts/run_standalone.sh

# Test in DAW (manual)
./scripts/build_vst.sh install
# Open your DAW and test
```

## Useful JUCE Debug Macros

```cpp
DBG("Debug message");                          // Debug builds only
jassert(condition);                             // Assert in debug
jassertfalse;                                  // Always fails in debug

#if JUCE_DEBUG
    // Debug-only code
#endif
```

## Tips and Tricks

1. **Fast iteration**: Use `./scripts/build_vst.sh debug run` for rapid testing
2. **Memory issues**: Build with Address Sanitizer (ASan) in CMake
3. **Thread issues**: Use Thread Sanitizer (TSan) for race conditions
4. **MIDI visualization**: Use MIDI Monitor app alongside standalone
5. **Audio comparison**: Use Audio Hijack to record standalone output

## Getting Help

If you encounter issues:

1. Check Console.app for crash logs
2. Use lldb backtrace to find crash location
3. Review JUCE documentation: https://docs.juce.com/
4. Check vst/NOTES_ARCHITECTURE.md for implementation details

---

Last updated: 2026-01-16
