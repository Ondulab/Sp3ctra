# Display Bidirectional Scrolling - Thin Line Fix

## Date
01 Décembre 2026

## Issue Description

When `line_thickness` is set to a very small value (thin line), the bidirectional scrolling loses symmetry and one side becomes black.

## Root Cause Analysis

The bidirectional scrolling creates a **gap between the two zones** at each frame:

```
Upper zone: shifts UP by scroll_speed    → position = -scroll_speed
Lower zone: shifts DOWN by scroll_speed  → position = birth_line_y + scroll_speed
```

This creates a gap of `2 × scroll_speed` pixels between the two zones. The destination buffer is cleared to black at the start of each frame, so this gap appears black.

The birth line is supposed to cover this gap, but when `thickness_px` is smaller than the gap size, black areas become visible.

### Visual Explanation

```
With thick line (OK):              With thin line (BUG):
+----------------------+          +----------------------+
|  Zone UP (shift -1)  |          |  Zone UP (shift -1)  |
|======================| ← line   |----------------------| ← black gap!
|       (covers gap)   |          |......................| ← thin line
|======================|          |----------------------| ← black gap!
|  Zone DOWN (shift +1)|          |  Zone DOWN (shift +1)|
+----------------------+          +----------------------+
```

## Solution Implemented

Enforce a **minimum thickness** for the birth line to always cover the scrolling gap:

```c
/* Ensure minimum thickness to cover the gap created by bidirectional scrolling.
 * The gap is 2 * scroll_speed pixels (upper shifts -scroll_speed, lower shifts +scroll_speed).
 * Without this, thin lines leave black gaps between the two scrolling zones. */
float min_thickness = 2.0f * scroll_speed + 1.0f;
if (thickness_px < min_thickness) {
    thickness_px = min_thickness;
}
```

This fix is applied to both **vertical mode** and **horizontal mode**.

### Example Calculation

With `scroll_speed = 1.0`:
- Gap size = 2 × 1.0 = 2 pixels
- Minimum thickness = 2 × 1.0 + 1 = 3 pixels
- Birth line will always be at least 3 pixels thick, covering the 2-pixel gap

With `scroll_speed = 2.0`:
- Gap size = 2 × 2.0 = 4 pixels
- Minimum thickness = 2 × 2.0 + 1 = 5 pixels

## Files Modified

- `src/display/display.c` - Added minimum thickness enforcement in both vertical and horizontal modes

## Previous Attempt (Reverted)

A previous attempt tried to exclude the birth line area from pixel shifting operations, but this **broke the content flow** through the birth line. The current solution correctly addresses the root cause without affecting the scrolling behavior.

## Related Documentation

- `docs/DISPLAY_BIDIRECTIONAL_SYMMETRIC_SCROLL.md` - Main implementation documentation
- `docs/DISPLAY_BIDIRECTIONAL_SCROLL.md` - Original specification
- `docs/DISPLAY_BIDIRECTIONAL_BEHAVIOR_EXPLAINED.md` - Behavior explanation
- `docs/DISPLAY_BIDIRECTIONAL_SCROLL.md` - Original specification
