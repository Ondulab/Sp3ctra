/* config_synth_poly.h */

#ifndef __CONFIG_SYNTH_POLY_H__
#define __CONFIG_SYNTH_POLY_H__

#define DISABLE_POLYPHONIC

/**************************************************************************************
 * Synthesis Mode Configuration - Resource Optimization
 **************************************************************************************/

// Automatic optimization flags
#define AUTO_DISABLE_POLYPHONIC_WITHOUT_MIDI 1              // Auto-disable polyphonic if no MIDI detected

// MIDI polling optimization
#define ENABLE_MIDI_POLLING          1                      // Set to 0 to disable MIDI polling entirely

#endif // __CONFIG_SYNTH_POLY_H__
