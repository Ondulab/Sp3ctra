/*
 * synth_additive_runtime.c
 *
 * Runtime configuration implementation for additive synthesis
 *
 * Author: zhonx
 */

#include "synth_additive_runtime.h"
#include "config.h"
#include "logger.h"
#include "wave_generation.h"
#include <stdio.h>
#include <stdlib.h>

/* Global runtime configuration */
synth_runtime_config_t g_synth_runtime = {0};

/* Dynamic buffer pointers (will be allocated at runtime) */
static struct wave *g_waves_dynamic = NULL;
static float *g_unitary_waveform_dynamic = NULL;

int synth_runtime_init(int max_pixels, int pixels_per_note) {
    if (pixels_per_note < 1) {
        fprintf(stderr, "[RUNTIME ERROR] pixels_per_note must be >= 1\n");
        return -1;
    }
    
    if (max_pixels % pixels_per_note != 0) {
        fprintf(stderr, "[RUNTIME ERROR] max_pixels (%d) must be divisible by pixels_per_note (%d)\n",
                max_pixels, pixels_per_note);
        return -1;
    }
    
    g_synth_runtime.max_pixels = max_pixels;
    g_synth_runtime.pixels_per_note = pixels_per_note;
    g_synth_runtime.num_notes = max_pixels / pixels_per_note;
    
    log_info("RUNTIME", "Initialized: %d pixels, %d pixels/note, %d notes",
           g_synth_runtime.max_pixels,
           g_synth_runtime.pixels_per_note,
           g_synth_runtime.num_notes);
    
    return 0;
}

int synth_runtime_allocate_buffers(void) {
    if (g_synth_runtime.num_notes <= 0) {
        fprintf(stderr, "[RUNTIME ERROR] Runtime config not initialized\n");
        return -1;
    }
    
    // Allocate waves array dynamically
    size_t waves_size = g_synth_runtime.num_notes * sizeof(struct wave);
    g_waves_dynamic = (struct wave*)calloc(g_synth_runtime.num_notes, sizeof(struct wave));
    if (!g_waves_dynamic) {
        fprintf(stderr, "[RUNTIME ERROR] Failed to allocate waves array (%zu bytes)\n", waves_size);
        return -1;
    }
    
    // Allocate unitary waveform (size is constant)
    #define WAVEFORM_TABLE_SIZE (10000000)
    size_t waveform_size = WAVEFORM_TABLE_SIZE * sizeof(float);
    g_unitary_waveform_dynamic = (float*)calloc(WAVEFORM_TABLE_SIZE, sizeof(float));
    if (!g_unitary_waveform_dynamic) {
        fprintf(stderr, "[RUNTIME ERROR] Failed to allocate unitary waveform (%zu bytes)\n", waveform_size);
        free(g_waves_dynamic);
        g_waves_dynamic = NULL;
        return -1;
    }
    
    log_info("RUNTIME", "Allocated buffers: waves=%zu bytes, waveform=%zu bytes",
           waves_size, waveform_size);
    
    return 0;
}

void synth_runtime_free_buffers(void) {
    if (g_waves_dynamic) {
        free(g_waves_dynamic);
        g_waves_dynamic = NULL;
    }
    
    if (g_unitary_waveform_dynamic) {
        free(g_unitary_waveform_dynamic);
        g_unitary_waveform_dynamic = NULL;
    }
    
    log_info("RUNTIME", "Freed dynamic buffers");
}

/* Accessor functions for dynamic buffers */
struct wave* synth_runtime_get_waves(void) {
    return g_waves_dynamic;
}

float* synth_runtime_get_unitary_waveform(void) {
    return g_unitary_waveform_dynamic;
}
