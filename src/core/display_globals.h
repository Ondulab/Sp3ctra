/* display_globals.h */

#ifndef DISPLAY_GLOBALS_H
#define DISPLAY_GLOBALS_H

#include "../config/config_display.h"

/**************************************************************************************
 * Global Display Configuration
 **************************************************************************************/

// Global display configuration instance
extern DisplayConfig g_display_config;

/**
 * Initialize display configuration with default values
 */
void display_config_init_defaults(void);

#endif // DISPLAY_GLOBALS_H
