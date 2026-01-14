/* config.h */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// Basic and debug configurations
#include "../config/config_debug.h"

// Hardware configurations
// clang-format off
#include "../config/config_instrument.h"
#include "../config/config_audio.h"
// clang-format on

// Synthesis mode configurations
#include "../config/config_synth_luxstral.h"
#include "../config/config_synth_luxsynth.h"

#endif // __CONFIG_H__
