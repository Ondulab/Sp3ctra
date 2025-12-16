/* display_globals.c */

#include "display_globals.h"
#include "../utils/logger.h"

/**************************************************************************************
 * Global Display Configuration Instance
 **************************************************************************************/

DisplayConfig g_display_config;

/**************************************************************************************
 * Initialization Function
 **************************************************************************************/

void display_config_init_defaults(void) {
    // Core display parameters
    g_display_config.orientation = DISPLAY_DEFAULT_ORIENTATION;
    g_display_config.udp_scroll_speed = DISPLAY_DEFAULT_UDP_SCROLL_SPEED;
    g_display_config.initial_line_position = DISPLAY_DEFAULT_INITIAL_LINE_POSITION;
    g_display_config.line_thickness = DISPLAY_DEFAULT_LINE_THICKNESS;
    
    // Window dimensions
    g_display_config.window_width = DISPLAY_DEFAULT_WINDOW_WIDTH;
    g_display_config.window_height = DISPLAY_DEFAULT_WINDOW_HEIGHT;
    
    log_info("DISPLAY_CONFIG", "Display configuration initialized with defaults");
}
