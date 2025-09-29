/*
 * synth_additive_runtime.h
 *
 * Runtime configuration for additive synthesis
 * Manages dynamic allocation based on pixels_per_note parameter
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_RUNTIME_H__
#define __SYNTH_ADDITIVE_RUNTIME_H__

#include <stdint.h>

/* Runtime configuration structure */
typedef struct {
    int max_pixels;           // CIS_MAX_PIXELS_NB (constant: 3456)
    int pixels_per_note;      // From config file (runtime)
    int num_notes;            // Calculated: max_pixels / pixels_per_note
} synth_runtime_config_t;

/* Global runtime configuration instance */
extern synth_runtime_config_t g_synth_runtime;

/**
 * @brief Initialize runtime configuration
 * @param max_pixels Maximum number of pixels (CIS_MAX_PIXELS_NB)
 * @param pixels_per_note Pixels per note from config
 * @return 0 on success, -1 on error
 */
int synth_runtime_init(int max_pixels, int pixels_per_note);

/**
 * @brief Get current number of notes
 * @return Number of notes
 */
static inline int synth_runtime_get_num_notes(void) {
    return g_synth_runtime.num_notes;
}

/**
 * @brief Allocate dynamic buffers for additive synthesis
 * Must be called after synth_runtime_init and before any synthesis
 * @return 0 on success, -1 on error
 */
int synth_runtime_allocate_buffers(void);

/**
 * @brief Free all dynamically allocated buffers
 */
void synth_runtime_free_buffers(void);

/**
 * @brief Get dynamically allocated waves array
 * @return Pointer to waves array (NULL if not allocated)
 */
struct wave* synth_runtime_get_waves(void);

/**
 * @brief Get dynamically allocated unitary waveform
 * @return Pointer to unitary waveform (NULL if not allocated)
 */
float* synth_runtime_get_unitary_waveform(void);

#endif /* __SYNTH_ADDITIVE_RUNTIME_H__ */
