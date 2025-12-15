/* config_display.h */

#ifndef __CONFIG_DISPLAY_H__
#define __CONFIG_DISPLAY_H__

#include <stdint.h>
#include "../core/config.h"  // For CIS_MAX_PIXELS_NB

/**************************************************************************************
 * Display Definitions (Legacy - will be moved to sp3ctra.ini)
 **************************************************************************************/
#define WINDOWS_WIDTH               (CIS_MAX_PIXELS_NB)
#define WINDOWS_HEIGHT              (1160)

/**************************************************************************************
 * Display Configuration Structure
 **************************************************************************************/
typedef struct {
    // Core display parameters
    // orientation: 0.0-0.33=vertical, 0.34-0.66=horizontal, 0.67-1.0=gyro_z
    float orientation;              // 3 modes via MIDI ranges (0-42, 43-84, 85-127)
    float udp_scroll_speed;         // -1.0 to +1.0 (reverse/forward)
    float initial_line_position;    // -1.0 (far), 0.0 (center), +1.0 (near)
    float line_thickness;           // 0.0 (thin) to 1.0 (barcode mode)
    
    // Window dimensions (configurable)
    int window_width;               // Display width in pixels
    int window_height;              // Display height in pixels
} DisplayConfig;

/**************************************************************************************
 * Display Configuration Defaults
 **************************************************************************************/
#define DISPLAY_DEFAULT_ORIENTATION             0.0f
#define DISPLAY_DEFAULT_UDP_SCROLL_SPEED        1.0f
#define DISPLAY_DEFAULT_INITIAL_LINE_POSITION   1.0f
#define DISPLAY_DEFAULT_LINE_THICKNESS          0.0f
#define DISPLAY_DEFAULT_WINDOW_WIDTH            WINDOWS_WIDTH
#define DISPLAY_DEFAULT_WINDOW_HEIGHT           WINDOWS_HEIGHT

/**************************************************************************************
 * Display Mode Thresholds
 **************************************************************************************/
#define DISPLAY_MODE_VERTICAL_MAX       0.33f   // 0.0-0.33: Vertical scroll mode
#define DISPLAY_MODE_HORIZONTAL_MAX     0.66f   // 0.34-0.66: Horizontal scroll mode

#endif // __CONFIG_DISPLAY_H__
