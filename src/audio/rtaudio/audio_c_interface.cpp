/* audio_c_interface.cpp
 *
 * C interface implementation for audio system operations.
 * Provides C-compatible wrappers for C++ audio functionality.
 */

#include "audio_c_interface.h"
#include "audio_rtaudio.h"
#include "config.h"
#include "midi_controller.h"
#include <stdio.h>

extern "C" {

void audio_set_master_volume(float volume) {
#ifdef DEBUG_AUDIO_INTERFACE
  printf("[AUDIO_IF] Setting master volume to %.3f\n", volume);
#endif
  if (gAudioSystem) {
    gAudioSystem->setMasterVolume(volume);
#ifdef DEBUG_AUDIO_INTERFACE
    printf("[AUDIO_IF] Volume applied successfully\n");
#endif
  } else {
#ifdef DEBUG_AUDIO_INTERFACE
    printf("[AUDIO_IF] WARNING: gAudioSystem is NULL, volume not applied\n");
#endif
  }
}

int audio_is_initialized(void) {
  int initialized = (gAudioSystem != nullptr) ? 1 : 0;
#ifdef DEBUG_AUDIO_INTERFACE
  printf("[AUDIO_IF] Audio system initialized: %s\n",
         initialized ? "YES" : "NO");
#endif
  return initialized;
}
}
