# Display Bidirectional Scrolling - Behavior Explanation

## Date
18 Décembre 2025

## Current Behavior Analysis

Based on diagnostic logs, the bidirectional scrolling system is working **as designed**.

### Example from logs

```
[DISPLAY_ZONES] birth_line_y=2320.0 | upper_h=2320 | lower_start=2320 lower_h=0 | buf_h=2320
[DISPLAY_DEBUG] Mode: VERTICAL | Speed: 1.00 | Birth: 1.00 | Thickness: 0.00
```

**Analysis**:
- `Birth: 1.00` → birth line at **bottom** of buffer (y=2320)
- `upper_h=2320` → **entire buffer** is upper zone (scrolls UP)
- `lower_h=0` → **no lower zone** exists

**This is correct behavior!** With the birth line at the bottom, there is no space below it for a lower zone.

## How Bidirectional Scrolling Works

### Birth Line Position Effects

The `initial_line_position` parameter (range: -1.0 to 1.0) controls where content is "born":

| Birth Value | Normalized Position | Upper Zone | Lower Zone | Visual Result |
|-------------|-------------------|------------|------------|---------------|
| `-1.0` | 0% (top) | 0% | 100% | Only scrolls DOWN |
| `-0.5` | 25% | 25% | 75% | Mostly scrolls DOWN |
| `0.0` | 50% (middle) | 50% | 50% | **Perfect symmetry** |
| `0.5` | 75% | 75% | 25% | Mostly scrolls UP |
| `1.0` | 100% (bottom) | 100% | 0% | Only scrolls UP |

### Zone Calculations

```c
float pos_norm = (pos_param + 1.0f) / 2.0f;  // -1.0→1.0 becomes 0.0→1.0
float birth_line_y = pos_norm * g_buffer_height;

// Upper zone: [0, birth_line_y) - scrolls UP
int upper_height = (int)birth_line_y;

// Lower zone: [birth_line_y, buffer_height) - scrolls DOWN  
int lower_start_y = (int)birth_line_y;
int lower_height = g_buffer_height - lower_start_y;
```

## Expected Behavior Examples

### Case 1: Birth at Top (`Birth: -1.0`)
```
Buffer (2320px):
+---------------------------+
|===========================| ← Birth line at y=0
|                           |
|   Lower Zone (2320px)     |
|   Scrolls DOWN ↓          |
|                           |
+---------------------------+

Logs: upper_h=0 | lower_h=2320
Result: Content only scrolls DOWN
```

### Case 2: Birth at Middle (`Birth: 0.0`)
```
Buffer (2320px):
+---------------------------+
|                           |
|   Upper Zone (1160px)     |
|   Scrolls UP ↑            |
|===========================| ← Birth line at y=1160
|                           |
|   Lower Zone (1160px)     |
|   Scrolls DOWN ↓          |
+---------------------------+

Logs: upper_h=1160 | lower_h=1160
Result: Perfect bidirectional symmetry
```

### Case 3: Birth at Bottom (`Birth: 1.0`)
```
Buffer (2320px):
+---------------------------+
|                           |
|   Upper Zone (2320px)     |
|   Scrolls UP ↑            |
|                           |
|===========================| ← Birth line at y=2320
+---------------------------+

Logs: upper_h=2320 | lower_h=0
Result: Content only scrolls UP
```

## Line Thickness Impact

The `line_thickness` parameter (0.0 to 1.0) only affects the **visual thickness** of the birth line, not the scrolling zones:

- `thickness=0.0` → 1 pixel line
- `thickness=0.5` → ~580 pixels (half window height)
- `thickness=1.0` → 1160 pixels (full window height)

**Important**: Thickness does NOT change zone boundaries. The zones are always split at `birth_line_y`, regardless of thickness.

## Troubleshooting

### "Only one side scrolls"

**Check the Birth position**:
- If Birth ≈ 1.0 → only upper zone exists (scrolls UP)
- If Birth ≈ -1.0 → only lower zone exists (scrolls DOWN)
- **Solution**: Set Birth ≈ 0.0 for symmetric bidirectional scrolling

### "Thin line causes asymmetry"

This is likely a **perception issue**:
- With a very thin line (thickness ≈ 0.0), the birth line is barely visible
- The viewport is centered on the birth line
- If Birth is not at 0.0, the zones are naturally asymmetric

**Solution**: 
1. Set `Birth: 0.0` for perfect symmetry
2. Increase thickness slightly to make the birth line more visible

## Diagnostic Logs

Use the `DISPLAY_ZONES` logs to verify behavior:

```
[DISPLAY_ZONES] birth_line_y=XXX.X | upper_h=YYY | lower_start=YYY lower_h=ZZZ | buf_h=TOTAL
```

**Healthy bidirectional scrolling** (Birth ≈ 0.0):
- `upper_h ≈ buf_h / 2`
- `lower_h ≈ buf_h / 2`

**Asymmetric scrolling** (Birth ≠ 0.0):
- One zone much larger than the other
- This is **expected behavior** based on Birth position

## Conclusion

The bidirectional scrolling system is working correctly. The perceived "asymmetry" is due to the Birth line position, not a bug.

**For perfect bidirectional symmetry**: Set `initial_line_position = 0.0` in configuration.
