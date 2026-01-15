# LuxSynth Shutdown Fix

**Date:** 2026-01-15  
**Status:** ✅ Fixed  
**Priority:** High

## Problem Description

During application shutdown (Ctrl+C), the LuxSynth synthesis thread was not terminating properly, causing:
- Continuous buffer timeout warnings: `"LuxSynth: Buffer wait timeout (callback too slow)"`
- Application hanging until a second Ctrl+C was sent
- Forced immediate exit required

### Root Cause

The LuxSynth synthesis thread (`synth_luxsynthMode_thread_func`) uses the global variable `keepRunning` in its main loop:

```c
while (keepRunning) {
    // synthesis loop
}
```

However, during shutdown sequence, only `app_running` and `context.running` were being set to 0, but **`keepRunning` was never updated**. This caused the LuxSynth thread to remain in its loop, continuously attempting to process buffers and generating timeout warnings.

## Solution

### Changes Made

1. **Added `keepRunning` declaration in main.c:**
```c
// External keepRunning flag used by synthesis threads
extern volatile int keepRunning;
```

2. **Updated signal handler (`signalHandler`):**
```c
// Update stop flags
app_running = 0;
keepRunning = 0;  // Signal synthesis threads to stop
if (global_context) {
    global_context->running = 0;
}
```

3. **Updated main shutdown sequence:**
```c
/* Step 2: Signal threads to stop */
log_info("MAIN", "Step 2/4: Signaling threads to stop...");
context.running = 0;
app_running = 0;
keepRunning = 0;  // Signal synthesis threads to stop
synth_luxwave_thread_stop();
```

4. **Restored default signal handler after cleanup:**
```c
// Restore default signal handler to allow clean process termination
signal(SIGINT, SIG_DFL);

return 0;
```

### Files Modified

- `src/core/main.c`: Added `keepRunning` control in signal handler and shutdown sequence, and restored default SIGINT handler

## Verification

### Expected Behavior (After Fix)

1. Press Ctrl+C once
2. Application logs clean shutdown sequence:
   - Step 1/4: Stopping audio stream
   - Step 2/4: Signaling threads to stop
   - Step 3/4: Joining threads
   - Step 4/4: Cleaning up resources
3. All threads terminate cleanly without warnings
4. Application exits successfully with single Ctrl+C

### Testing

```bash
# Build the fixed version
make clean && make

# Run and test shutdown
./build/Sp3ctra

# Press Ctrl+C once - should exit cleanly
```

## Technical Details

### Thread Synchronization

The application uses three main stop flags:
- `app_running`: Global application state (main loop)
- `context.running`: Thread context state (UDP, audio processing threads)
- `keepRunning`: Synthesis threads state (LuxSynth, LuxWave threads)

All three flags must be set to 0 during shutdown to ensure all threads terminate properly.

### RT Constraints Compliance

This fix maintains real-time (RT) audio constraints:
- No dynamic allocation in shutdown path
- No blocking operations beyond thread joins
- Atomic flag updates (volatile variables)
- Clean thread termination without forced kills

## Related Code

### Variable Definition
`src/utils/stubs.c`:
```c
volatile int keepRunning = 1;
```

### Thread Usage
`src/synthesis/luxsynth/synth_luxsynth.c`:
```c
extern volatile int keepRunning;

void *synth_luxsynthMode_thread_func(void *arg) {
    // ...
    while (keepRunning) {
        // synthesis loop
    }
    // ...
}
```

## Impact

- **User Experience:** Single Ctrl+C now cleanly exits the application
- **Logging:** No more spurious timeout warnings during shutdown
- **System Resources:** Proper thread cleanup ensures no resource leaks
- **RT Performance:** No impact on real-time audio performance during normal operation

## Notes

This fix follows the project's architectural rules:
- Clean English code and comments
- Proper signal handling
- RT-safe shutdown sequence
- Conventional Commits style: `fix(shutdown): ensure keepRunning flag is reset during shutdown`
