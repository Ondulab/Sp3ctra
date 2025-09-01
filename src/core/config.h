/* config.h */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// Configurations de base et de débogage
#include "../config/config_debug.h"

// Configurations matérielles
// clang-format off
#include "../config/config_instrument.h"
#include "../config/config_display.h"
#include "../config/config_audio.h"
#include "../config/config_dmx.h"
// clang-format on

// Configurations des modes de synthèse
#include "../config/config_synth_additive.h"
#include "../config/config_synth_poly.h"

#endif // __CONFIG_H__
