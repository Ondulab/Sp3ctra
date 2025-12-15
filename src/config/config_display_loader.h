/* config_display_loader.h */

#ifndef CONFIG_DISPLAY_LOADER_H
#define CONFIG_DISPLAY_LOADER_H

#include "config_loader.h"

/**
 * Load display configuration from INI file
 * Parses the [display] section and populates g_display_config
 * 
 * @param config_file_path Path to the configuration file
 * @return CONFIG_SUCCESS on success, error code on failure
 */
int load_display_config(const char* config_file_path);

/**
 * Validate display configuration parameters
 * 
 * @return CONFIG_SUCCESS if valid, CONFIG_ERROR_VALIDATION_FAILED otherwise
 */
int validate_display_config(void);

#endif // CONFIG_DISPLAY_LOADER_H
