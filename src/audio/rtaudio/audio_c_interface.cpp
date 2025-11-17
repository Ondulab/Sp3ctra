/* audio_c_interface.cpp
 *
 * C interface implementation for audio system operations.
 * Provides C-compatible wrappers for C++ audio functionality.
 */

#include "audio_c_interface.h"
#include "audio_rtaudio.h"
#include "config.h"
#include "midi_controller.h"
#include "../../utils/logger.h"
#include <stdio.h>

extern "C" {

void audio_set_master_volume(float volume) {
  if (gAudioSystem) {
    gAudioSystem->setMasterVolume(volume);
    log_info("AUDIO", "Master volume set to %.3f", volume);
  } else {
    log_warning("AUDIO", "Cannot set master volume: audio system not initialized");
  }
}

int audio_is_initialized(void) {
  int initialized = (gAudioSystem != nullptr) ? 1 : 0;
  return initialized;
}
}
