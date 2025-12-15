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
    g_display_config.accel_y_position_control = DISPLAY_DEFAULT_ACCEL_Y_POSITION_CONTROL;
    g_display_config.initial_line_position = DISPLAY_DEFAULT_INITIAL_LINE_POSITION;
    g_display_config.line_thickness = DISPLAY_DEFAULT_LINE_THICKNESS;
    
    // Advanced display parameters
    g_display_config.transition_time_ms = DISPLAY_DEFAULT_TRANSITION_TIME_MS;
    g_display_config.accel_sensitivity = DISPLAY_DEFAULT_ACCEL_SENSITIVITY;
    g_display_config.fade_strength = DISPLAY_DEFAULT_FADE_STRENGTH;
    g_display_config.line_persistence = DISPLAY_DEFAULT_LINE_PERSISTENCE;
    g_display_config.display_zoom = DISPLAY_DEFAULT_DISPLAY_ZOOM;
    g_display_config.history_buffer_size = DISPLAY_DEFAULT_HISTORY_BUFFER_SIZE;
    
    // Window dimensions
    g_display_config.window_width = DISPLAY_DEFAULT_WINDOW_WIDTH;
    g_display_config.window_height = DISPLAY_DEFAULT_WINDOW_HEIGHT;
    
    // IMU rotation parameters (used in Mode 2 - Gyro Z)
    g_display_config.gyro_rotation_sensitivity = DISPLAY_DEFAULT_GYRO_ROTATION_SENSITIVITY;
    g_display_config.rotation_smoothing = DISPLAY_DEFAULT_ROTATION_SMOOTHING;
    
    // Runtime state (initialize to match defaults)
    g_display_config.current_scroll_speed = DISPLAY_DEFAULT_UDP_SCROLL_SPEED;
    g_display_config.current_zoom = DISPLAY_DEFAULT_DISPLAY_ZOOM;
    g_display_config.current_offset = DISPLAY_DEFAULT_ACCEL_Y_POSITION_CONTROL;
    
    log_info("DISPLAY_CONFIG", "Display configuration initialized with defaults");
}
