/* config_debug.h */

#ifndef __CONFIG_DEBUG_H__
#define __CONFIG_DEBUG_H__

/**************************************************************************************
 * DEBUG IMAGE PROCESSING
 * 
 * Debug d'images contrôlé par arguments runtime :
 * --debug-image[=LINES]                    Enable raw scanner capture debug (default: 1000 lines)
 * --debug-additive-osc-image[=SAMPLES[,m]] Enable oscillator volume capture debug (default: 48000 samples, m=markers)
 * --debug-additive-osc=<N|N-M>            Debug one or a range of additive oscillators (e.g., 56 or 23-89)
 **************************************************************************************/

// Configuration pour le debug d'images
#define DEBUG_IMAGE_OUTPUT_DIR "./debug_images/"              // Debug image output directory (relative to executable)

#endif // __CONFIG_DEBUG_H__
