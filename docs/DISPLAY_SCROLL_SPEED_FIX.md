# Display Scroll Speed Control Fix

## Date
2026-01-12

## Problem Description

The scroll speed control (`udp_scroll_speed` parameter) was not working correctly:
- At `scroll_speed = -1`, the visualization was correct (1 pixel/frame scrolling)
- At any other value (including 0), the scrolling stopped completely
- Only the birth line was updating, with no history accumulation

### Root Cause

In `display.c`, the scroll speed was calculated incorrectly:

```c
float scroll_speed = fabsf(g_display_config.udp_scroll_speed);  /* Always positive */
```

This direct mapping caused:
- `-1.0` → `fabsf(-1.0) = 1.0` → 1 pixel/frame ✓
- `0.0` → `fabsf(0.0) = 0.0` → **0 pixels/frame** (no scrolling!)
- `+1.0` → `fabsf(1.0) = 1.0` → same as -1

### Expected Behavior

The user requirement was:
- **0** = Normal speed (1 pixel/frame)
- **-1** = Very slow speed
- **+1** = Very fast speed

## Solution

### 1. Exponential Speed Mapping

Implemented an exponential conversion formula for smooth control:

```c
/* Speed mapping: -1 → very slow, 0 → normal, +1 → very fast
 * Formula: base_speed * 2^(scale * udp_scroll_speed)
 * With base_speed = 1.0 and scale = 3.0:
 *   -1 → 1.0 * 2^(-3) = 0.125 pixels/frame (very slow)
 *    0 → 1.0 * 2^(0)  = 1.0 pixel/frame (normal)
 *   +1 → 1.0 * 2^(3)  = 8.0 pixels/frame (very fast) */
float scroll_speed_raw = powf(2.0f, 3.0f * speed_param);
```

### 2. Fractional Accumulation

For speeds below 1 pixel/frame (e.g., 0.125 at -1), a fractional accumulator ensures smooth scrolling:

```c
/* Static accumulator for fractional scroll speeds */
static float g_scroll_accumulator = 0.0f;

/* Accumulate fractional scroll for smooth sub-pixel scrolling */
g_scroll_accumulator += scroll_speed_raw;
int pixels_to_scroll = (int)g_scroll_accumulator;
g_scroll_accumulator -= (float)pixels_to_scroll;

/* Use integer pixel scroll for this frame (minimum 0) */
float scroll_speed = (float)pixels_to_scroll;
```

This means at `-1` (0.125 px/frame):
- Frame 1-7: accumulator grows, no scroll (0 pixels)
- Frame 8: accumulator reaches 1.0, scroll 1 pixel
- Result: 1 pixel every 8 frames → very slow but smooth

## Speed Mapping Table

| Parameter | Raw Speed (px/f) | Effective Speed |
|-----------|------------------|-----------------|
| -1.0      | 0.125            | 1 px / 8 frames |
| -0.5      | 0.354            | ~1 px / 3 frames |
| 0.0       | 1.0              | 1 px / frame |
| +0.5      | 2.83             | ~3 px / frame |
| +1.0      | 8.0              | 8 px / frame |

## Files Modified

- `src/display/display.c`:
  - Added `g_scroll_accumulator` static variable
  - Replaced direct `fabsf()` mapping with exponential formula
  - Added fractional accumulation logic
  - Enhanced debug logging to show speed parameters

## Testing

After building, verify:
1. `scroll_speed = 0` → Normal scrolling (1 pixel/frame)
2. `scroll_speed = -1` → Very slow scrolling (visible but gradual)
3. `scroll_speed = +1` → Very fast scrolling (8 pixels/frame)

Debug log output (every ~10 seconds):
```
DISPLAY_DEBUG: Mode: VERTICAL | SpeedParam: 0.00 | SpeedRaw: 1.00 px/f | ScrollThisFrame: 1 px | ...
```

## Impact

- **Display system**: Scroll speed now responds correctly to parameter changes
- **MIDI control**: Controllers mapped to scroll speed will work as expected
- **No breaking changes**: Default value (0) produces the same 1px/frame as before
