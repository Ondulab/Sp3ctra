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

/* Static variables for GPU-accelerated bidirectional scrolling */
#ifndef NO_SFML
static sfRenderTexture *g_history_buffer_a = NULL;
static sfRenderTexture *g_history_buffer_b = NULL;
static int g_current_buffer = 0;  /* 0 = A is source, 1 = B is source */
static sfTexture *g_line_texture_h = NULL;  /* Horizontal line texture */
static sfTexture *g_line_texture_v = NULL;  /* Vertical line texture */
static sfSprite *g_line_sprite = NULL;
static sfSprite *g_content_sprite = NULL;
static sfSprite *g_display_sprite = NULL;
#endif

static unsigned int g_buffer_width = 0;
static unsigned int g_buffer_height = 0;
static unsigned int g_last_win_width = 0;
static unsigned int g_last_win_height = 0;

/* Static variables for debug logging (currently disabled) */
// static uint64_t g_frame_counter = 0;
// static const uint64_t DEBUG_LOG_INTERVAL = 600;  /* Log every 600 frames (~10 seconds at 60fps) */

/* Static accumulator for fractional scroll speeds */
static float g_scroll_accumulator = 0.0f;

int display_Init(sfRenderWindow *window) {
  if (window) {
    log_info("DISPLAY", "SFML window detected in CLI mode, using it for display");
    log_info("DISPLAY", "SFML CONFIGURED IN BIDIRECTIONAL SCROLLING MODE");
  } else {
    log_info("DISPLAY", "Running in CLI mode, no SFML window required");
  }
  return 0;
}

// Nouvelle fonction printImageRGB (GPU Accelerated + Bidirectional Scrolling)
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

    /* Configuration */
    int is_horizontal_mode = (g_display_config.orientation >= 0.5f);
    
    /* Speed mapping: -1 → very slow, 0 → normal, +1 → very fast
     * Formula: base_speed * 2^(scale * udp_scroll_speed)
     * With base_speed = 1.0 and scale = 3.0:
     *   -1 → 1.0 * 2^(-3) = 0.125 pixels/frame (very slow)
     *    0 → 1.0 * 2^(0)  = 1.0 pixel/frame (normal)
     *   +1 → 1.0 * 2^(3)  = 8.0 pixels/frame (very fast) */
    float speed_param = g_display_config.udp_scroll_speed;
    if (speed_param < -1.0f) speed_param = -1.0f;
    if (speed_param > 1.0f) speed_param = 1.0f;
    float scroll_speed_raw = powf(2.0f, 3.0f * speed_param);
    
    /* Accumulate fractional scroll for smooth sub-pixel scrolling */
    g_scroll_accumulator += scroll_speed_raw;
    int pixels_to_scroll = (int)g_scroll_accumulator;
    g_scroll_accumulator -= (float)pixels_to_scroll;
    
    /* Use integer pixel scroll for this frame (minimum 0) */
    float scroll_speed = (float)pixels_to_scroll;

    /* Initialize or Resize GPU Resources */
    if (!g_history_buffer_a || win_width != g_last_win_width || win_height != g_last_win_height) {
        /* Clean up old resources */
        if (g_history_buffer_a) sfRenderTexture_destroy(g_history_buffer_a);
        if (g_history_buffer_b) sfRenderTexture_destroy(g_history_buffer_b);
        if (g_line_texture_h) sfTexture_destroy(g_line_texture_h);
        if (g_line_texture_v) sfTexture_destroy(g_line_texture_v);
        if (g_line_sprite) sfSprite_destroy(g_line_sprite);
        if (g_content_sprite) sfSprite_destroy(g_content_sprite);
        if (g_display_sprite) sfSprite_destroy(g_display_sprite);

        /* Calculate buffer dimensions (2x window size in scroll direction) */
        unsigned int new_buffer_width = is_horizontal_mode ? (win_width * 2) : win_width;
        unsigned int new_buffer_height = is_horizontal_mode ? win_height : (win_height * 2);

        /* Create Double-Sized Linear Buffers (NO repeat mode) */
        g_history_buffer_a = sfRenderTexture_create(new_buffer_width, new_buffer_height, sfFalse);
        g_history_buffer_b = sfRenderTexture_create(new_buffer_width, new_buffer_height, sfFalse);
        
        if (!g_history_buffer_a || !g_history_buffer_b) {
            log_error("DISPLAY", "Failed to create Render Textures");
            return;
        }
        
        /* Clear both buffers to black initially */
        sfRenderTexture_clear(g_history_buffer_a, sfBlack);
        sfRenderTexture_display(g_history_buffer_a);
        sfRenderTexture_clear(g_history_buffer_b, sfBlack);
        sfRenderTexture_display(g_history_buffer_b);
        
        /* Create Line Textures */
        g_line_texture_h = sfTexture_create(nb_pixels, 1);  /* Horizontal line */
        g_line_texture_v = sfTexture_create(1, nb_pixels);  /* Vertical line */
        
        /* Create Sprites */
        g_line_sprite = sfSprite_create();
        g_content_sprite = sfSprite_create();
        g_display_sprite = sfSprite_create();
        
        g_buffer_width = new_buffer_width;
        g_buffer_height = new_buffer_height;
        g_last_win_width = win_width;
        g_last_win_height = win_height;
        g_current_buffer = 0;
        
        log_info("DISPLAY", "GPU Resources Initialized: Buffer=%ux%u, Window=%ux%u, Mode=%s",
                 new_buffer_width, new_buffer_height, win_width, win_height,
                 is_horizontal_mode ? "HORIZONTAL" : "VERTICAL");
    }

    /* Calculate Birth Line Position */
    float pos_param = g_display_config.initial_line_position;
    if (pos_param < -1.0f) pos_param = -1.0f;
    if (pos_param > 1.0f) pos_param = 1.0f;
    float pos_norm = (pos_param + 1.0f) / 2.0f;  /* Normalize to 0.0-1.0 */
    
    /* Calculate Thickness */
    float thickness_param = g_display_config.line_thickness;
    if (thickness_param < 0.0f) thickness_param = 0.0f;
    if (thickness_param > 1.0f) thickness_param = 1.0f;

    /* Get source and destination buffers */
    sfRenderTexture *src_buffer = g_current_buffer ? g_history_buffer_b : g_history_buffer_a;
    sfRenderTexture *dst_buffer = g_current_buffer ? g_history_buffer_a : g_history_buffer_b;

    /* Clear destination buffer */
    sfRenderTexture_clear(dst_buffer, sfBlack);

    /* Set content sprite to use source buffer texture */
    sfSprite_setTexture(g_content_sprite, (sfTexture*)sfRenderTexture_getTexture(src_buffer), sfTrue);

    if (!is_horizontal_mode) {
        /* === VERTICAL MODE === */
        float birth_line_y = pos_norm * g_buffer_height;
        float thickness_px = 1.0f + (thickness_param * (win_height - 1));
        
        /* Ensure minimum thickness to cover the gap created by bidirectional scrolling.
         * The gap is 2 * scroll_speed pixels (upper shifts -scroll_speed, lower shifts +scroll_speed).
         * Without this, thin lines leave black gaps between the two scrolling zones. */
        float min_thickness = 2.0f * scroll_speed + 1.0f;
        if (thickness_px < min_thickness) {
            thickness_px = min_thickness;
        }

        /* Shift upper zone UP by scroll_speed */
        int upper_height = (int)birth_line_y;
        if (upper_height > 0) {
            sfIntRect upper_rect = {0, 0, (int)g_buffer_width, upper_height};
            sfSprite_setTextureRect(g_content_sprite, upper_rect);
            sfSprite_setPosition(g_content_sprite, (sfVector2f){0, -scroll_speed});
            sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
        }

        /* Shift lower zone DOWN by scroll_speed */
        int lower_start_y = (int)birth_line_y;
        int lower_height = g_buffer_height - lower_start_y;
        if (lower_height > 0) {
            sfIntRect lower_rect = {0, lower_start_y, (int)g_buffer_width, lower_height};
            sfSprite_setTextureRect(g_content_sprite, lower_rect);
            sfSprite_setPosition(g_content_sprite, (sfVector2f){0, lower_start_y + scroll_speed});
            sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
        }

        /* Debug logging every 600 frames */
        /*
        if (g_frame_counter % DEBUG_LOG_INTERVAL == 0) {
            log_info("DISPLAY_ZONES", "birth_line_y=%.1f | upper_h=%d | lower_start=%d lower_h=%d | buf_h=%u",
                     birth_line_y, upper_height, lower_start_y, lower_height, g_buffer_height);
        }
        */

        /* Update horizontal line texture */
        sfImage *line_image = sfImage_create(nb_pixels, 1);
        if (line_image) {
            for (int x = 0; x < nb_pixels; x++) {
                sfImage_setPixel(line_image, x, 0, sfColor_fromRGB(buffer_R[x], buffer_G[x], buffer_B[x]));
            }
            sfTexture_updateFromImage(g_line_texture_h, line_image, 0, 0);
            sfImage_destroy(line_image);

            /* Draw new line at birth position */
            sfSprite_setTexture(g_line_sprite, g_line_texture_h, sfTrue);
            sfSprite_setTextureRect(g_line_sprite, (sfIntRect){0, 0, nb_pixels, 1});
            float scale_x = (float)win_width / nb_pixels;
            sfSprite_setScale(g_line_sprite, (sfVector2f){scale_x, thickness_px});
            float y_pos = birth_line_y - (thickness_px / 2.0f);
            sfSprite_setPosition(g_line_sprite, (sfVector2f){0, y_pos});
            sfRenderTexture_drawSprite(dst_buffer, g_line_sprite, NULL);
        }

        /* Calculate viewport centered on birth line */
        int viewport_y = (int)(birth_line_y - win_height / 2.0f);
        if (viewport_y < 0) viewport_y = 0;
        if (viewport_y > (int)(g_buffer_height - win_height)) {
            viewport_y = g_buffer_height - win_height;
        }

        /* Display to window */
        sfRenderTexture_display(dst_buffer);
        sfRenderWindow_clear(window, sfBlack);
        sfSprite_setTexture(g_display_sprite, (sfTexture*)sfRenderTexture_getTexture(dst_buffer), sfTrue);
        sfSprite_setTextureRect(g_display_sprite, (sfIntRect){0, viewport_y, (int)win_width, (int)win_height});
        sfSprite_setPosition(g_display_sprite, (sfVector2f){0, 0});
        sfSprite_setScale(g_display_sprite, (sfVector2f){1.0f, 1.0f});
        sfRenderWindow_drawSprite(window, g_display_sprite, NULL);

    } else {
        /* === HORIZONTAL MODE === */
        float birth_line_x = pos_norm * g_buffer_width;
        float thickness_px = 1.0f + (thickness_param * (win_width - 1));
        
        /* Ensure minimum thickness to cover the gap created by bidirectional scrolling.
         * The gap is 2 * scroll_speed pixels (left shifts -scroll_speed, right shifts +scroll_speed).
         * Without this, thin lines leave black gaps between the two scrolling zones. */
        float min_thickness = 2.0f * scroll_speed + 1.0f;
        if (thickness_px < min_thickness) {
            thickness_px = min_thickness;
        }

        /* Shift left zone LEFT by scroll_speed */
        int left_width = (int)birth_line_x;
        if (left_width > 0) {
            sfIntRect left_rect = {0, 0, left_width, (int)g_buffer_height};
            sfSprite_setTextureRect(g_content_sprite, left_rect);
            sfSprite_setPosition(g_content_sprite, (sfVector2f){-scroll_speed, 0});
            sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
        }

        /* Shift right zone RIGHT by scroll_speed */
        int right_start_x = (int)birth_line_x;
        int right_width = g_buffer_width - right_start_x;
        if (right_width > 0) {
            sfIntRect right_rect = {right_start_x, 0, right_width, (int)g_buffer_height};
            sfSprite_setTextureRect(g_content_sprite, right_rect);
            sfSprite_setPosition(g_content_sprite, (sfVector2f){right_start_x + scroll_speed, 0});
            sfRenderTexture_drawSprite(dst_buffer, g_content_sprite, NULL);
        }

        /* Debug logging every 600 frames */
        /*
        if (g_frame_counter % DEBUG_LOG_INTERVAL == 0) {
            log_info("DISPLAY_ZONES", "birth_line_x=%.1f | left_w=%d | right_start=%d right_w=%d | buf_w=%u",
                     birth_line_x, left_width, right_start_x, right_width, g_buffer_width);
        }
        */

        /* Update vertical line texture (1 × nb_pixels) */
        sfImage *line_image = sfImage_create(1, nb_pixels);
        if (line_image) {
            for (int y = 0; y < nb_pixels; y++) {
                sfImage_setPixel(line_image, 0, y, sfColor_fromRGB(buffer_R[y], buffer_G[y], buffer_B[y]));
            }
            sfTexture_updateFromImage(g_line_texture_v, line_image, 0, 0);
            sfImage_destroy(line_image);

            /* Draw new line at birth position (NO ROTATION NEEDED) */
            sfSprite_setTexture(g_line_sprite, g_line_texture_v, sfTrue);
            sfSprite_setTextureRect(g_line_sprite, (sfIntRect){0, 0, 1, nb_pixels});
            float scale_y = (float)win_height / nb_pixels;
            sfSprite_setScale(g_line_sprite, (sfVector2f){thickness_px, scale_y});
            float x_pos = birth_line_x - (thickness_px / 2.0f);
            sfSprite_setPosition(g_line_sprite, (sfVector2f){x_pos, 0});
            sfRenderTexture_drawSprite(dst_buffer, g_line_sprite, NULL);
        }

        /* Calculate viewport centered on birth line */
        int viewport_x = (int)(birth_line_x - win_width / 2.0f);
        if (viewport_x < 0) viewport_x = 0;
        if (viewport_x > (int)(g_buffer_width - win_width)) {
            viewport_x = g_buffer_width - win_width;
        }

        /* Display to window */
        sfRenderTexture_display(dst_buffer);
        sfRenderWindow_clear(window, sfBlack);
        sfSprite_setTexture(g_display_sprite, (sfTexture*)sfRenderTexture_getTexture(dst_buffer), sfTrue);
        sfSprite_setTextureRect(g_display_sprite, (sfIntRect){viewport_x, 0, (int)win_width, (int)win_height});
        sfSprite_setPosition(g_display_sprite, (sfVector2f){0, 0});
        sfSprite_setScale(g_display_sprite, (sfVector2f){1.0f, 1.0f});
        sfRenderWindow_drawSprite(window, g_display_sprite, NULL);
    }

    sfRenderWindow_display(window);

    /* Swap buffers for next frame */
    g_current_buffer = 1 - g_current_buffer;

    /* Periodic logging (disabled) */
    // g_frame_counter++;
    /*
    if (g_frame_counter % DEBUG_LOG_INTERVAL == 0) {
         log_info("DISPLAY_DEBUG", "Mode: %s | SpeedParam: %.2f | SpeedRaw: %.2f px/f | ScrollThisFrame: %d px | Birth: %.2f | Thickness: %.2f", 
                  is_horizontal_mode ? "HORIZONTAL" : "VERTICAL",
                  speed_param,
                  scroll_speed_raw,
                  pixels_to_scroll,
                  pos_param,
                  thickness_param);
    }
    */
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

/**
 * @brief Cleanup display GPU resources
 * 
 * This function MUST be called during shutdown to release all SFML GPU resources
 * (RenderTextures, Textures, Sprites) that were created for bidirectional scrolling.
 * Failure to call this function will prevent the process from terminating properly
 * as the OpenGL context remains active.
 */
void display_cleanup(void) {
#ifndef NO_SFML
  log_info("DISPLAY", "Cleaning up GPU scrolling resources...");
  
  // Destroy RenderTextures (GPU-resident framebuffers)
  if (g_history_buffer_a) {
    sfRenderTexture_destroy(g_history_buffer_a);
    g_history_buffer_a = NULL;
  }
  if (g_history_buffer_b) {
    sfRenderTexture_destroy(g_history_buffer_b);
    g_history_buffer_b = NULL;
  }
  
  // Destroy Textures
  if (g_line_texture_h) {
    sfTexture_destroy(g_line_texture_h);
    g_line_texture_h = NULL;
  }
  if (g_line_texture_v) {
    sfTexture_destroy(g_line_texture_v);
    g_line_texture_v = NULL;
  }
  
  // Destroy Sprites
  if (g_line_sprite) {
    sfSprite_destroy(g_line_sprite);
    g_line_sprite = NULL;
  }
  if (g_content_sprite) {
    sfSprite_destroy(g_content_sprite);
    g_content_sprite = NULL;
  }
  if (g_display_sprite) {
    sfSprite_destroy(g_display_sprite);
    g_display_sprite = NULL;
  }
  
  // Reset state variables
  g_buffer_width = 0;
  g_buffer_height = 0;
  g_last_win_width = 0;
  g_last_win_height = 0;
  g_current_buffer = 0;
  g_scroll_accumulator = 0.0f;
  
  log_info("DISPLAY", "GPU scrolling resources cleaned up");
#endif
}
