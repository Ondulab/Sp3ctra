//
//  Display.c
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#include "config.h"
#include <stdint.h> // Pour uint8_t, uint32_t et autres types entiers de taille fixe

#ifdef __LINUX__
// On Linux, check if SFML is explicitly disabled
#ifndef NO_SFML
// SFML/CSFML disponible sur Linux
// Use SFML/CSFML headers for C files
// Note: Homebrew installs CSFML headers in the same directory as SFML
#include <SFML/Graphics.h>
#include <SFML/Network.h> // If SFML/Network is used
#endif                    // NO_SFML
#else                     // Pas __LINUX__ (par exemple macOS)
// Sur les autres plateformes (comme macOS), on suppose que SFML/CSFML est
// available unless NO_SFML is also defined for these platforms.
#ifndef NO_SFML
// Use SFML/CSFML headers for C files
// Note: Homebrew installs CSFML headers in the same directory as SFML
#include <SFML/Graphics.h>
#include <SFML/Network.h> // If SFML/Network is used
#endif                    // NO_SFML
#endif                    // __LINUX__
#include <errno.h>
#include <fcntl.h>
#include <math.h>  // For fabsf()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h> // For clock_gettime
#include <unistd.h>

#include "../core/context.h"
#include "display.h"
#include "display_buffer.h"
#include "error.h"
#include "../config/config_instrument.h"
#include "../core/display_globals.h"
#include "../utils/logger.h"

/* Static variables for GPU-accelerated circular buffer */
#ifndef NO_SFML
static sfRenderTexture *g_history_buffer = NULL;
static sfTexture *g_line_texture = NULL;
static sfSprite *g_line_sprite = NULL;
static sfSprite *g_history_sprite = NULL;
#endif

static float g_scroll_offset = 0.0f;
static unsigned int g_last_tex_width = 0;
static unsigned int g_last_tex_height = 0;

/* Static variables for debug logging */
static uint64_t g_frame_counter = 0;
static const uint64_t DEBUG_LOG_INTERVAL = 600;  /* Log every 600 frames (~10 seconds at 60fps) */

int display_Init(sfRenderWindow *window) {
  if (window) {
    log_info("DISPLAY", "SFML window detected in CLI mode, using it for display");
    log_info("DISPLAY", "SFML CONFIGURED IN GPU CIRCULAR BUFFER MODE");
  } else {
    log_info("DISPLAY", "Running in CLI mode, no SFML window required");
  }
  return 0;
}

// Nouvelle fonction printImageRGB (GPU Accelerated + Circular Buffer)
void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G,
                   uint8_t *buffer_B, sfTexture *background_texture,
                   sfTexture *foreground_texture) {
#ifndef NO_SFML
  // In CLI mode, SFML window presence is optional.
  if (!window) return;

  // Unused parameters in this new GPU implementation, but kept for signature compatibility
  (void)background_texture;
  (void)foreground_texture;

  {
    int nb_pixels = get_cis_pixels_nb();
    sfVector2u window_size = sfRenderWindow_getSize(window);
    unsigned int win_width = window_size.x;
    unsigned int win_height = window_size.y;

    /* Initialize or Resize GPU Resources */
    if (!g_history_buffer || win_width != g_last_tex_width || win_height != g_last_tex_height) {
        if (g_history_buffer) sfRenderTexture_destroy(g_history_buffer);
        if (g_line_texture) sfTexture_destroy(g_line_texture);
        if (g_line_sprite) sfSprite_destroy(g_line_sprite);
        if (g_history_sprite) sfSprite_destroy(g_history_sprite);

        // Create Render Texture (History Buffer)
        g_history_buffer = sfRenderTexture_create(win_width, win_height, sfFalse);
        if (!g_history_buffer) {
            log_error("DISPLAY", "Failed to create Render Texture");
            return;
        }
        
        // Enable Repeat for Circular Buffer effect
        sfTexture_setRepeated((sfTexture*)sfRenderTexture_getTexture(g_history_buffer), sfTrue);
        
        // Clear to black initially
        sfRenderTexture_clear(g_history_buffer, sfBlack);
        sfRenderTexture_display(g_history_buffer);
        
        // Create Reusable Line Texture (1xNbPixels or NbPixelsx1 depending on usage, max size)
        // We make it large enough to hold the scan line.
        // We will update it every frame.
        g_line_texture = sfTexture_create(nb_pixels, 1); 
        
        // Create Sprites
        g_line_sprite = sfSprite_create();
        sfSprite_setTexture(g_line_sprite, g_line_texture, sfTrue);
        
        g_history_sprite = sfSprite_create();
        sfSprite_setTexture(g_history_sprite, (sfTexture*)sfRenderTexture_getTexture(g_history_buffer), sfTrue);
        
        g_last_tex_width = win_width;
        g_last_tex_height = win_height;
        g_scroll_offset = 0.0f;
        
        log_info("DISPLAY", "GPU Resources Initialized: %ux%u", win_width, win_height);
    }

    /* Configuration */
    int is_horizontal_mode = (g_display_config.orientation >= 0.5f);
    float scroll_speed = g_display_config.udp_scroll_speed;
    
    /* Update Scroll Offset */
    /* If speed is positive, we scroll "forward". */
    /* The offset represents the "camera position" on the circular buffer. */
    /* To scroll UP (visual move up), camera moves UP (offset increases). */
    g_scroll_offset += scroll_speed;
    
    /* Normalize offset to avoid float precision issues */
    float max_dim = is_horizontal_mode ? (float)win_width : (float)win_height;
    if (g_scroll_offset > max_dim) g_scroll_offset -= max_dim;
    if (g_scroll_offset < 0.0f) g_scroll_offset += max_dim;

    /* Prepare New Line Image */
    sfImage *line_image = NULL;
    
    /* Calculate Line Position on Screen */
    float pos_param = g_display_config.initial_line_position;
    if (pos_param < -1.0f) pos_param = -1.0f;
    if (pos_param > 1.0f) pos_param = 1.0f;
    float pos_norm = (pos_param + 1.0f) / 2.0f; // 0.0 to 1.0
    
    /* Calculate Thickness */
    float thickness_param = g_display_config.line_thickness;
    if (thickness_param < 0.0f) thickness_param = 0.0f;
    if (thickness_param > 1.0f) thickness_param = 1.0f;

    /* Update Line Texture */
    if (!is_horizontal_mode) {
        /* === VERTICAL MODE (Lines are Horizontal) === */
        /* We create a 1-pixel high image of the scan line */
        line_image = sfImage_create(nb_pixels, 1);
        if (line_image) {
            for (int x = 0; x < nb_pixels; x++) {
                sfImage_setPixel(line_image, x, 0, sfColor_fromRGB(buffer_R[x], buffer_G[x], buffer_B[x]));
            }
            // Update the reusable texture (resize if needed or just update rect)
            // Ideally we created g_line_texture with enough width.
            sfTexture_updateFromImage(g_line_texture, line_image, 0, 0);
            sfImage_destroy(line_image);
            
            /* Calculate Drawing Position in Texture */
            /* Screen_Y = Target_Y */
            /* Texture_Y = (Screen_Y + Offset) % Height */
            
            float target_screen_y = pos_norm * (win_height); // Target Y on screen
            float draw_y = target_screen_y + g_scroll_offset;
            while (draw_y >= win_height) draw_y -= win_height;
            
            /* Calculate Thickness in Pixels */
            float thickness_px = 1.0f + (thickness_param * (win_height - 1));
            
            /* Setup Sprite for Drawing into History */
            /* We want to draw the line stretched to thickness */
            sfSprite_setTextureRect(g_line_sprite, (sfIntRect){0, 0, nb_pixels, 1});
            
            /* Scale: Stretch width to Window Width, Stretch height to Thickness */
            float scale_x = (float)win_width / nb_pixels;
            sfSprite_setScale(g_line_sprite, (sfVector2f){scale_x, thickness_px});
            
            /* Position: Centered on draw_y */
            /* Note: Origin is top-left. So we position at draw_y - thickness/2 */
            float y_pos = draw_y - (thickness_px / 2.0f);
            
            /* Draw to Render Texture */
            /* Handle Wrapping: If drawing crosses boundary, draw twice */
            
            // Draw 1 (Main)
            sfSprite_setPosition(g_line_sprite, (sfVector2f){0, y_pos});
            sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            
            // Draw 2 (Wrap Bottom) - if y_pos + thick > height
            if (y_pos + thickness_px > win_height) {
                sfSprite_setPosition(g_line_sprite, (sfVector2f){0, y_pos - win_height});
                sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            }
            
            // Draw 3 (Wrap Top) - if y_pos < 0
            if (y_pos < 0) {
                sfSprite_setPosition(g_line_sprite, (sfVector2f){0, y_pos + win_height});
                sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            }
        }
    } else {
        /* === HORIZONTAL MODE (Lines are Vertical) === */
        /* We create a 1-pixel wide image (column) */
        /* Note: g_line_texture is nb_pixels wide. We reuse it but as 1xHeight? */
        /* Actually simpler to create a 1xNbPixels image and rotate the sprite? */
        /* Or create a temporary image. */
        /* Let's use the existing g_line_texture (horizontal) and rotate the sprite 90 deg! */
        
        line_image = sfImage_create(nb_pixels, 1);
        if (line_image) {
             for (int i = 0; i < nb_pixels; i++) {
                sfImage_setPixel(line_image, i, 0, sfColor_fromRGB(buffer_R[i], buffer_G[i], buffer_B[i]));
            }
            sfTexture_updateFromImage(g_line_texture, line_image, 0, 0);
            sfImage_destroy(line_image);
            
            float target_screen_x = pos_norm * (win_width);
            float draw_x = target_screen_x + g_scroll_offset;
            while (draw_x >= win_width) draw_x -= win_width;
            
            float thickness_px = 1.0f + (thickness_param * (win_width - 1));
            
            /* Setup Sprite */
            sfSprite_setTextureRect(g_line_sprite, (sfIntRect){0, 0, nb_pixels, 1});
            
            /* Rotation: 90 degrees clockwise */
            sfSprite_setRotation(g_line_sprite, 90.0f);
            
            /* Scale: X becomes Height (stretched to Window Height), Y becomes Width (stretched to Thickness) */
            /* Before rotation: X is along scan line (pixels), Y is along 1px. */
            /* After rotation 90: X axis points down. Y axis points left. */
            /* We want length to match Window Height. We want width (old Y) to match Thickness. */
            float scale_len = (float)win_height / nb_pixels;
            sfSprite_setScale(g_line_sprite, (sfVector2f){scale_len, thickness_px});
            
            /* Position */
            /* After 90 deg rot, origin is at Top-Left of sprite, which is Top-Right of visual line if unshifted? */
            /* Let's verify origin. Standard is (0,0). Rot 90 around (0,0). */
            /* (10, 0) becomes (0, 10). (0, 1) becomes (-1, 0). */
            /* So the sprite extends DOWN and LEFT from the position. */
            /* We want it to extend DOWN (along Y) and RIGHT (thickness). */
            /* So we should rotate -90 (270) or adjust position? */
            /* Let's rotate 90. Sprite goes (0,0) -> (0, Height). Thickness extends to (-Thickness). */
            /* So we position at (draw_x + thickness/2, 0). */
            /* This puts the line from (draw_x + thick/2) to (draw_x - thick/2) ? Yes. */
            
            float x_pos = draw_x + (thickness_px / 2.0f);
            
            sfSprite_setPosition(g_line_sprite, (sfVector2f){x_pos, 0});
            sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            
            /* Wrap */
            // If x_pos - thickness < 0? No, checking logic of draw_x
            // Draw logic is slightly complex with rotation.
            // Simplified: Draw at x_pos. If visible part wraps, draw again.
            
            // Check boundaries relative to draw_x (center)
            float left_edge = draw_x - thickness_px/2.0f;
            float right_edge = draw_x + thickness_px/2.0f;
            
            if (right_edge > win_width) {
                sfSprite_setPosition(g_line_sprite, (sfVector2f){x_pos - win_width, 0});
                sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            }
            if (left_edge < 0) {
                sfSprite_setPosition(g_line_sprite, (sfVector2f){x_pos + win_width, 0});
                sfRenderTexture_drawSprite(g_history_buffer, g_line_sprite, NULL);
            }
            
            /* Reset rotation for next frame safety */
            sfSprite_setRotation(g_line_sprite, 0.0f);
        }
    }
    
    /* Finalize History Buffer Update */
    sfRenderTexture_display(g_history_buffer);
    
    /* Draw History Buffer to Window */
    sfRenderWindow_clear(window, sfBlack);
    
    /* Set Texture Rect to create scrolling effect */
    /* We want to view the texture starting at 'offset'. */
    /* Because Texture is Repeated, we can just specify the rect. */
    /* Rect(0, offset, width, height) */
    
    sfIntRect view_rect;
    if (!is_horizontal_mode) {
        view_rect = (sfIntRect){0, (int)g_scroll_offset, (int)win_width, (int)win_height};
    } else {
        view_rect = (sfIntRect){(int)g_scroll_offset, 0, (int)win_width, (int)win_height};
    }
    
    sfSprite_setTextureRect(g_history_sprite, view_rect);
    sfSprite_setScale(g_history_sprite, (sfVector2f){1.0f, 1.0f}); // Reset scale just in case
    sfSprite_setPosition(g_history_sprite, (sfVector2f){0, 0});
    
    sfRenderWindow_drawSprite(window, g_history_sprite, NULL);
    sfRenderWindow_display(window);

    /* Periodic logging */
    g_frame_counter++;
    if (g_frame_counter % DEBUG_LOG_INTERVAL == 0) {
         log_info("DISPLAY_DEBUG", "GPU Mode: %s | Speed: %.2f | Offset: %.2f", 
                  is_horizontal_mode ? "HORIZONTAL" : "VERTICAL",
                  scroll_speed,
                  g_scroll_offset);
    }
  }
#else
  // NO_SFML is defined, do nothing.
  (void)window;
  (void)buffer_R;
  (void)buffer_G;
  (void)buffer_B;
  (void)background_texture;
  (void)foreground_texture;
#endif
}
