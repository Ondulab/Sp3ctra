# Display Bidirectional Scrolling Implementation

## Overview

Implementation of a bidirectional scrolling display system where content scrolls away from a fixed "birth line" position in both directions simultaneously. This eliminates the circular wrap artifact and creates a more natural scrolling behavior.

## Problem Solved

### Previous Circular Buffer Issues

The previous implementation used a circular buffer with `sfTexture_setRepeated = true`, which caused:
- **Visible wrap artifact**: When the birth line was positioned in the middle, content scrolling off one edge would immediately reappear on the opposite edge
- **"Rotation effect"**: The wrap created an unnatural visual rotation of the image
- **Direction dependency**: Required explicit direction control via `udp_scroll_speed` sign

### New Linear Buffer Solution

The new implementation uses a **double-sized linear buffer** (2× window dimension in the scroll direction):
- **No wrap artifact**: Content scrolls into empty space, no circular reappearance
- **Natural bidirectional flow**: Content above the birth line scrolls up, content below scrolls down
- **Position-determined direction**: Birth line position automatically determines scroll behavior

## Architecture

### Buffer Structure

**Vertical Mode** (orientation < 0.5):
```
Linear Buffer (height = 2 × window_height)
+---------------------------+
|                           |
|   Upper Zone              |  ← Content scrolls UP
|   (above birth line)      |
|                           |
|===========================| ← Birth Line (fixed position)
|                           |
|   Lower Zone              |  ← Content scrolls DOWN
|   (below birth line)      |
|                           |
+---------------------------+

Viewport (window_height)
     ↕
Centered on birth line
```

**Horizontal Mode** (orientation >= 0.5):
```
Linear Buffer (width = 2 × window_width)
+---------------------------+===+---------------------------+
|                           |   |                           |
|   Left Zone               | B |   Right Zone              |
|   (left of birth line)    | i |   (right of birth line)   |
|                           | r |                           |
|   ← Scrolls LEFT          | t |   Scrolls RIGHT →         |
|                           | h |                           |
+---------------------------+===+---------------------------+
                              ↕
                        Viewport (window_width)
                        Centered on birth line
```

### Key Components

**Static Variables** (`src/display/display.c`):
```c
static sfRenderTexture *g_history_buffer = NULL;  // Double-sized buffer
static unsigned int g_buffer_width = 0;           // 2×win_width (horizontal) or win_width (vertical)
static unsigned int g_buffer_height = 0;          // 2×win_height (vertical) or win_height (horizontal)
```

**Configuration Parameters**:
- `initial_line_position`: -1.0 (top/left) to +1.0 (bottom/right)
  - Determines birth line position in buffer
  - Automatically controls scroll direction distribution
- `udp_scroll_speed`: Now only controls speed magnitude (always positive via `fabsf()`)
- `line_thickness`: 0.0 to 1.0, controls visual thickness of birth line

## Implementation Details

### Buffer Initialization

```c
/* Calculate buffer dimensions (2x window size in scroll direction) */
unsigned int new_buffer_width = is_horizontal_mode ? (win_width * 2) : win_width;
unsigned int new_buffer_height = is_horizontal_mode ? win_height : (win_height * 2);

/* Create Double-Sized Linear Buffer (NO repeat mode) */
g_history_buffer = sfRenderTexture_create(new_buffer_width, new_buffer_height, sfFalse);

/* NO setRepeated - linear buffer without wrap */
```

### Birth Line Position Calculation

```c
/* Calculate Birth Line Position */
float pos_param = g_display_config.initial_line_position;  // -1.0 to +1.0
float pos_norm = (pos_param + 1.0f) / 2.0f;                // Normalize to 0.0-1.0

/* Position in buffer */
float birth_line_y = pos_norm * g_buffer_height;  // Vertical mode
float birth_line_x = pos_norm * g_buffer_width;   // Horizontal mode
```

### Viewport Centering

The viewport is always centered on the birth line, showing equal amounts of content above and below (or left and right):

```c
/* Calculate viewport centered on birth line */
int viewport_y = (int)(birth_line_y - win_height / 2.0f);

/* Clamp to buffer boundaries */
if (viewport_y < 0) viewport_y = 0;
if (viewport_y > (int)(g_buffer_height - win_height)) {
    viewport_y = g_buffer_height - win_height;
}
```

### Line Drawing

New scan lines are drawn at the fixed birth line position:

```c
/* Draw new line at birth position */
float y_pos = birth_line_y - (thickness_px / 2.0f);
sfSprite_setPosition(g_line_sprite, (sfVector2f){0, y_pos});
sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
```

## Behavior Examples

### Birth Line at Top (initial_line_position = -1.0)

```
Buffer:
+---------------------------+
|===========================| ← Birth line (Y=0)
|                           |
|   All content scrolls     |
|   DOWN ↓                  |
|                           |
|                           |
+---------------------------+

Viewport: Shows birth line at top edge
```

### Birth Line at Center (initial_line_position = 0.0)

```
Buffer:
+---------------------------+
|                           |
|   Content scrolls UP ↑    |
|                           |
|===========================| ← Birth line (Y=center)
|                           |
|   Content scrolls DOWN ↓  |
|                           |
+---------------------------+

Viewport: Shows birth line at center
```

### Birth Line at Bottom (initial_line_position = +1.0)

```
Buffer:
+---------------------------+
|                           |
|                           |
|   All content scrolls     |
|   UP ↑                    |
|                           |
|===========================| ← Birth line (Y=max)
+---------------------------+

Viewport: Shows birth line at bottom edge
```

## Current Implementation Status

### ✅ Completed (All Phases)

- [x] **Phase 1-3**: Double-sized linear buffer creation
- [x] **Phase 1-3**: Birth line positioning system
- [x] **Phase 1-3**: Viewport centering on birth line
- [x] **Phase 1-3**: Line thickness rendering
- [x] **Phase 1-3**: Horizontal and vertical mode support
- [x] **Phase 1-3**: Elimination of circular wrap artifact
- [x] **Phase 4**: Ping-pong buffer system (A/B swap)
- [x] **Phase 4**: Actual pixel shifting implementation
  - Upper zone shifts upward, lower zone shifts downward
  - GPU-accelerated texture copying with offset
- [x] **Phase 5**: Speed-based scrolling via `udp_scroll_speed`
- [x] **Phase 6**: Rotation elimination in horizontal mode
  - Direct vertical texture creation (1 × nb_pixels)
  - No `sfSprite_setRotation()` needed
- [x] **Phase 7**: Complete bidirectional scrolling system

## Implementation Details

### Ping-Pong Buffer System

The implementation uses two render textures that alternate roles each frame:

```c
static sfRenderTexture *g_history_buffer_a = NULL;
static sfRenderTexture *g_history_buffer_b = NULL;
static int g_current_buffer = 0;  /* 0 = A is source, 1 = B is source */

/* Each frame */
sfRenderTexture *src_buffer = g_current_buffer ? g_history_buffer_b : g_history_buffer_a;
sfRenderTexture *dst_buffer = g_current_buffer ? g_history_buffer_a : g_history_buffer_b;

/* Process: Read from src, write to dst, then swap */
g_current_buffer = 1 - g_current_buffer;
```

### Pixel Shifting Algorithm (Vertical Mode)

```c
/* Calculate birth line position in buffer */
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

/* Draw new line at birth position */
/* ... */
```

### Rotation Elimination (Horizontal Mode)

Instead of rotating a horizontal texture, we create a native vertical texture:

```c
/* Create vertical line texture (1 × nb_pixels) */
static sfTexture *g_line_texture_v = NULL;
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
udp_scroll_speed = 1.0               # Speed magnitude (always positive now)
initial_line_position = 0.0          # -1.0 (top/left) to +1.0 (bottom/right)
line_thickness = 0.0                 # 0.0 (thin) to 1.0 (full window)
```

### MIDI Control

- **CC:35** - orientation (0=vertical, 1=horizontal)
- **CC:36** - udp_scroll_speed (magnitude only)
- **CC:38** - initial_line_position (birth line position)
- **CC:39** - line_thickness

## Technical Notes

### Memory Usage

- **Previous**: 1× window dimension (e.g., 1160×1160 = ~5.4 MB for RGBA)
- **Current**: 2× scroll dimension (e.g., 1160×2320 = ~10.8 MB for RGBA)
- **Impact**: Acceptable for modern GPUs, enables artifact-free scrolling

### Performance Considerations

- Linear buffer eliminates modulo operations
- No texture repeat overhead
- Viewport rect calculation is O(1)
- Future pixel shifting will be GPU-accelerated via texture copies

### Compatibility

- Maintains same API signature for `printImageRGB()`
- Backward compatible with existing configuration
- Automatic mode detection (vertical/horizontal)

## References

- Original circular buffer: `docs/DISPLAY_CONTINUOUS_SCROLL_FIX.md`
- Display system spec: `docs/DISPLAY_SYSTEM_SPECIFICATION.md`
- Implementation: `src/display/display.c`
