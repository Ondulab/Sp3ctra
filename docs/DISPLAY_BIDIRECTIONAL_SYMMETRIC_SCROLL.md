# Display Bidirectional Symmetric Scrolling Implementation

## Date
18 Décembre 2025

## Overview

Implementation of a **truly symmetric bidirectional scrolling system** where content scrolls away from a fixed "birth line" in both directions with **identical absolute speed**. This eliminates the need for negative scroll speeds and creates perfectly symmetric visual behavior.

## Key Changes from Previous Implementation

### Before (Circular Buffer)
- Used `sfTexture_setRepeated = true` causing wrap artifacts
- Required negative speed values to control direction
- Visible "rotation effect" when birth line was centered
- Single buffer with modulo-based positioning

### After (Bidirectional Linear Buffer)
- **Double-sized linear buffer** (2× window dimension in scroll direction)
- **Ping-pong buffer system** (A/B swap) for GPU-accelerated pixel shifting
- **Always positive scroll speed** via `fabsf()`
- **Perfectly symmetric scrolling** on both sides of birth line
- **No rotation needed** in horizontal mode (native vertical texture)

## Architecture

### Buffer Structure

**Vertical Mode**:
```
Linear Buffer (height = 2 × window_height)
+---------------------------+
|                           |
|   Upper Zone              |  ← Scrolls UP at +speed
|   (above birth line)      |
|                           |
|===========================| ← Birth Line (FIXED)
|                           |
|   Lower Zone              |  ← Scrolls DOWN at +speed
|   (below birth line)      |
|                           |
+---------------------------+

Viewport: Centered on birth line
```

**Horizontal Mode**:
```
Linear Buffer (width = 2 × window_width)
+-------------+===+-------------+
|             | B |             |
|  Left Zone  | i | Right Zone  |
|             | r |             |
| ← LEFT      | t | RIGHT →     |
|  +speed     | h |  +speed     |
+-------------+===+-------------+

Viewport: Centered on birth line
```

## Implementation Details

### Static Variables

```c
static sfRenderTexture *g_history_buffer_a = NULL;
static sfRenderTexture *g_history_buffer_b = NULL;
static int g_current_buffer = 0;  /* 0 = A is source, 1 = B is source */
static sfTexture *g_line_texture_h = NULL;  /* Horizontal line texture */
static sfTexture *g_line_texture_v = NULL;  /* Vertical line texture */
static sfSprite *g_line_sprite = NULL;
static sfSprite *g_content_sprite = NULL;
static sfSprite *g_display_sprite = NULL;
```

### Ping-Pong Buffer Algorithm

Each frame:
1. **Read** from source buffer (A or B)
2. **Shift** upper/left zone in one direction
3. **Shift** lower/right zone in opposite direction
4. **Draw** new line at birth position
5. **Write** to destination buffer (B or A)
6. **Swap** buffers for next frame

### Pixel Shifting (Vertical Mode Example)

```c
/* Get source and destination buffers */
sfRenderTexture *src_buffer = g_current_buffer ? g_history_buffer_b : g_history_buffer_a;
sfRenderTexture *dst_buffer = g_current_buffer ? g_history_buffer_a : g_history_buffer_b;

/* Calculate birth line position */
float birth_line_y = pos_norm * g_buffer_height;

/* Shift upper zone UP by scroll_speed */
if (birth_line_y > 0) {
    sfIntRect upper_rect = {0, 0, (int)g_buffer_width, (int)birth_line_y};
    sfSprite_setTextureRect(g_content_sprite, upper_rect);
    sfSprite_setPosition(g_content_sprite, (sfVector2f){0, -scroll_speed});
    sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
}

/* Shift lower zone DOWN by scroll_speed */
if (birth_line_y < g_buffer_height) {
    int lower_start_y = (int)birth_line_y;
    int lower_height = g_buffer_height - lower_start_y;
    sfIntRect lower_rect = {0, lower_start_y, (int)g_buffer_width, lower_height};
    sfSprite_setTextureRect(g_content_sprite, lower_rect);
    sfSprite_setPosition(g_content_sprite, (sfVector2f){0, lower_start_y + scroll_speed});
    sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
}

/* Swap buffers */
g_current_buffer = 1 - g_current_buffer;
```

### Speed Handling

```c
/* Always positive speed */
float scroll_speed = fabsf(g_display_config.udp_scroll_speed);
```

**No more negative speeds needed!** The direction is automatically determined by the zone position relative to the birth line.

### Viewport Centering

The viewport is always centered on the birth line, showing equal amounts of content on both sides:

```c
/* Vertical mode */
int viewport_y = (int)(birth_line_y - win_height / 2.0f);
if (viewport_y < 0) viewport_y = 0;
if (viewport_y > (int)(g_buffer_height - win_height)) {
    viewport_y = g_buffer_height - win_height;
}
```

### Horizontal Mode - No Rotation

Instead of rotating a horizontal texture, we create a **native vertical texture**:

```c
/* Create vertical line texture (1 × nb_pixels) */
g_line_texture_v = sfTexture_create(1, nb_pixels);

/* Fill with pixel data */
sfImage *line_image = sfImage_create(1, nb_pixels);
for (int y = 0; y < nb_pixels; y++) {
    sfImage_setPixel(line_image, 0, y, sfColor_fromRGB(buffer_R[y], buffer_G[y], buffer_B[y]));
}
sfTexture_updateFromImage(g_line_texture_v, line_image, 0, 0);

/* Scale and draw - NO ROTATION NEEDED */
float scale_y = (float)win_height / nb_pixels;
sfSprite_setScale(g_line_sprite, (sfVector2f){thickness_px, scale_y});
```

## Configuration

### sp3ctra.ini

```ini
[display]
orientation = 0.0                    # 0=vertical, 1=horizontal
udp_scroll_speed = 1.0               # Speed magnitude (ALWAYS POSITIVE)
initial_line_position = 0.0          # -1.0 (top/left) to +1.0 (bottom/right)
line_thickness = 0.0                 # 0.0 (thin) to 1.0 (full window)
```

### MIDI Control

- **CC:35** - orientation (0=vertical, 1=horizontal)
- **CC:36** - udp_scroll_speed (magnitude only, always positive)
- **CC:38** - initial_line_position (birth line position)
- **CC:39** - line_thickness

## Behavior Examples

### Birth Line at Center (initial_line_position = 0.0)

**Perfect Symmetry:**
- Upper half scrolls UP at speed = 1.0
- Lower half scrolls DOWN at speed = 1.0
- Both zones move at **identical absolute speed**
- Visual result: Content "splits" symmetrically from center

### Birth Line at Top (initial_line_position = -1.0)

- Upper zone = 0 pixels (no content above)
- Lower zone = full buffer (all content scrolls DOWN)
- Equivalent to traditional single-direction scrolling

### Birth Line at Bottom (initial_line_position = +1.0)

- Upper zone = full buffer (all content scrolls UP)
- Lower zone = 0 pixels (no content below)
- Equivalent to traditional single-direction scrolling (opposite direction)

## Technical Benefits

✅ **Perfect Symmetry**: Identical absolute speed on both sides  
✅ **No Wrap Artifacts**: Linear buffer eliminates circular wrap  
✅ **Simplified Speed Control**: Always positive, intuitive  
✅ **GPU-Accelerated**: Texture copying via hardware  
✅ **No Rotation Overhead**: Native vertical textures in horizontal mode  
✅ **Mathematically Clean**: No modulo, no wrap logic  

## Memory Usage

- **Previous**: 1× window dimension (e.g., 1160×1160 = ~5.4 MB RGBA)
- **Current**: 2× scroll dimension (e.g., 1160×2320 = ~10.8 MB RGBA)
- **Ping-Pong**: 2× buffers = ~21.6 MB total
- **Impact**: Acceptable for modern GPUs, enables artifact-free scrolling

## Performance

- **GPU-accelerated** texture copying
- **O(1)** viewport calculation
- **No CPU-side pixel manipulation**
- **Efficient** ping-pong buffer swap (pointer swap only)

## Files Modified

- `src/display/display.c` - Complete rewrite of scrolling system

## Testing Recommendations

1. Test with `initial_line_position = 0.0` (center) to verify perfect symmetry
2. Test with various `udp_scroll_speed` values (all positive)
3. Test with `initial_line_position` at extremes (-1.0, +1.0)
4. Verify no visual artifacts or wrap effects
5. Test both vertical and horizontal modes
6. Verify line thickness rendering at birth position

## References

- Original specification: `docs/DISPLAY_BIDIRECTIONAL_SCROLL.md`
- Display system spec: `docs/DISPLAY_SYSTEM_SPECIFICATION.md`
- Previous circular buffer: `docs/DISPLAY_CONTINUOUS_SCROLL_FIX.md`
