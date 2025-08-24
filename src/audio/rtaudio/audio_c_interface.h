/* audio_c_interface.h
 *
 * C interface for audio system operations.
 * Provides C-compatible wrappers for C++ audio functionality.
 */

#ifndef AUDIO_C_INTERFACE_H
#define AUDIO_C_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set master volume (0.0 to 1.0) */
void audio_set_master_volume(float volume);

/* Check if audio system is initialized */
int audio_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_C_INTERFACE_H */
