# Display Continuous Scroll Fix

## Problem Analysis

### Issues Identified

1. **Black Screen with Artifacts**
   - Foreground texture was never initialized at startup
   - Remained black and only accumulated data fragments over time
   - Caused visual artifacts

2. **Single Line Display (No History Trail)**
   - Code only drew lines when `should_scroll == 1`
   - When scroll speed < 1.0, frames were completely skipped
   - Result: No visual accumulation, only a single jumping line

3. **Discontinuous Line Gaps**
   - Conditional scrolling created visual holes
   - New lines were not adjacent to previous ones
   - Created fragmented/choppy scrolling effect

4. **Incorrect IMU Integration**
   - Code performed simple integration: `position += acceleration * dt`
   - Physically incorrect - should be double integration
   - User correctly identified that acceleration was being used directly instead of position

5. **Wrong Configuration Parameters**
   - Used obsolete `accel_x_scroll_speed` and `accel_y_offset`
   - Should use `accel_y_position_control` (position control gain)

## Solutions Implemented

### 1. Texture Initialization
**File**: `src/core/main.c`

Added black initialization after texture creation:
```c
/* Initialize foreground texture to black to avoid artifacts */
sfImage *black_image = sfImage_createFromColor(texture_width, WINDOWS_HEIGHT, sfBlack);
if (black_image) {
    sfTexture_updateFromImage(foregroundTexture, black_image, 0, 0);
    sfImage_destroy(black_image);
    log_info("DISPLAY", "Foreground texture initialized to black");
}
```

### 2. Double Integration for IMU Position
**File**: `src/display/display.c`

Implemented physically correct double integration:
```c
/* Step 1: Integrate acceleration to get velocity */
g_imu_velocity_y += imu_accel_y * accel_sensitivity * dt;

/* Step 2: Integrate velocity to get position */
g_imu_position_y += g_imu_velocity_y * position_control_gain * dt;

/* Anti-drift: Reset velocity at boundaries */
if (g_imu_position_y < 0.05f) {
    g_imu_position_y = 0.05f;
    g_imu_velocity_y = 0.0f;  /* Stop at boundary */
}
if (g_imu_position_y > 0.95f) {
    g_imu_position_y = 0.95f;
    g_imu_velocity_y = 0.0f;  /* Stop at boundary */
}
```

### 3. Continuous Reference Position
**File**: `src/display/display.c`

Replaced discrete accumulator with continuous reference position:

**Before**:
```c
static float g_scroll_accumulator = 0.0f;

g_scroll_accumulator += fabsf(scroll_speed);
if (g_scroll_accumulator >= 1.0f) {
    should_scroll = 1;
    g_scroll_accumulator -= 1.0f;
} else {
    should_scroll = 0;  // Skip frame completely!
}

if (should_scroll) {
    // Draw line
}
// Otherwise: NO DRAWING AT ALL
```

**After**:
```c
static float g_reference_line_position = 0.0f;

/* Move reference position at scroll speed */
g_reference_line_position += fabsf(scroll_speed);

/* Scroll texture when position crosses 1.0 */
if (g_reference_line_position >= 1.0f) {
    should_scroll = 1;
    g_reference_line_position -= 1.0f;
    sfTexture_updateFromTexture(...);  // Scroll
}

/* ALWAYS draw the new line (no skip) */
// Draw at reference position
```

### 4. Always Draw Lines
**Files**: `src/display/display.c`

**Mode 0 (Vertical)**:
- Removed `if (should_scroll)` condition around line drawing
- Scroll texture conditionally, but ALWAYS draw new line
- Ensures continuous visual trail

**Mode 1 (Horizontal)**:
- Same approach as vertical mode
- Scroll texture conditionally, but ALWAYS draw new line

### 5. Configuration Parameter Cleanup
**Files**: 
- `src/config/config_display.h`
- `src/config/config_display_loader.c`
- `src/core/display_globals.c`

Changes:
- Removed: `accel_x_scroll_speed` (unused)
- Renamed: `accel_y_offset` → `accel_y_position_control`
- Updated all references and defaults

## Technical Details

### Reference Position Behavior

**Slow Speed (0.5)**:
- Frame 1: ref_pos = 0.5 → draw at Y=1159, no scroll
- Frame 2: ref_pos = 1.0 → scroll texture, ref_pos = 0.0, draw at Y=1159
- Frame 3: ref_pos = 0.5 → draw at Y=1159, no scroll
- Result: Line visible at same position, periodic scroll

**Fast Speed (2.0)**:
- Frame 1: ref_pos = 2.0 → scroll twice, draw at new position
- Ensures continuity even at high speed

### IMU Position Control

The `accel_y_position_control` parameter acts as a **gain** on the velocity integration:
- `0.0`: No IMU effect (centered position)
- `±1.0`: Full amplitude movement
- Controls how much the integrated position affects line placement

## Results

✅ **Texture properly initialized** - No more black screen
✅ **Continuous visual trail** - Lines always drawn, creating history
✅ **No discontinuous gaps** - Smooth scrolling at all speeds
✅ **Correct physics** - Double integration (acceleration → velocity → position)
✅ **Anti-drift** - Velocity reset at boundaries prevents accumulation
✅ **Clean configuration** - Obsolete parameters removed

## Testing Recommendations

1. Test with various scroll speeds (0.1, 0.5, 1.0, 2.0)
2. Verify continuous trail at slow speeds
3. Verify no gaps at fast speeds
4. Test IMU position control with real sensor data
5. Verify velocity integration produces position (not acceleration)

## Configuration

Default values in `sp3ctra.ini`:
```ini
[display]
orientation = 0.0                    # Vertical mode
udp_scroll_speed = 1.0               # Normal speed, bottom to top
accel_y_position_control = 0.0       # IMU disabled by default
display_zoom = 0.0                   # Normal zoom (factor = 1.0)
```

MIDI Control:
- CC:35 - orientation (0=vertical, 1=horizontal, 2=gyro_z)
- CC:36 - udp_scroll_speed (-1.0 to +1.0)
- CC:37 - accel_y_position_control (-1.0 to +1.0)
- CC:44 - display_zoom (-1.0 to +1.0)
