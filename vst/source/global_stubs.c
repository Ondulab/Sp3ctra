// Stubs temporaires pour permettre la compilation du VST minimal
// Ces variables seront remplacées par une architecture instanciée dans la version finale

#include "../../src/core/context.h"
#include "../../src/config/config_loader.h"
#include "../../src/processing/image_sequencer.h"
#include "../../src/synthesis/luxwave/synth_luxwave.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// CRITICAL: Fixed buffer size - ALWAYS 400 DPI (3456 pixels)
// This allows runtime switching between 200/400 DPI without buffer reallocation
#define FIXED_BUFFER_SIZE_400DPI 3456

// Configuration globale - Now initialized from APVTS (AudioProcessorValueTreeState)
// These values will be set by PluginProcessor based on user preferences
sp3ctra_config_t g_sp3ctra_config = {
    .sampling_frequency = 48000,
    .audio_buffer_size = 512,
    .log_level = 2, // LOG_LEVEL_INFO (default, overridden by APVTS)
    .udp_address = "239.100.100.100",
    .udp_port = 55151,
    .multicast_interface = "",
    .sensor_dpi = 400  // DEFAULT: 400 DPI (can be 200, but buffers stay 3456 pixels)
};

// ✨ BRICK 1 STUBS: Variables globales utilisées par udpThread
// Ces stubs seront remplacés dans les prochaines briques de migration

// Image sequencer global (pour l'instant NULL, brique 3)
ImageSequencer *g_image_sequencer = NULL;

// LuxWave state global (pour l'instant stub, brique 4)
LuxWaveState g_luxwave_state;

// NOTE: g_displayable_synth_R/G/B are already declared in synth_luxstral_state.c as pointers
// BUT synth_luxstral_state.c is NOT compiled in VST, so we define them here
// These will replace the declarations from synth_luxstral_state.c
uint8_t *g_displayable_synth_R = NULL;
uint8_t *g_displayable_synth_G = NULL;
uint8_t *g_displayable_synth_B = NULL;
pthread_mutex_t g_displayable_synth_mutex;

// Constructor to initialize display buffers at module load time
// CRITICAL: ALWAYS allocate for 400 DPI (3456 pixels), regardless of sensor_dpi setting
// This allows runtime switching between 200/400 DPI without buffer reallocation
__attribute__((constructor)) static void init_display_buffers(void) {
    // FIXED allocation: 3456 pixels (400 DPI)
    // Even if sensor_dpi=200, buffers stay 3456 pixels (only first 1728 used)
    g_displayable_synth_R = (uint8_t *)calloc(FIXED_BUFFER_SIZE_400DPI, sizeof(uint8_t));
    g_displayable_synth_G = (uint8_t *)calloc(FIXED_BUFFER_SIZE_400DPI, sizeof(uint8_t));
    g_displayable_synth_B = (uint8_t *)calloc(FIXED_BUFFER_SIZE_400DPI, sizeof(uint8_t));
    
    if (!g_displayable_synth_R || !g_displayable_synth_G || !g_displayable_synth_B) {
        // Fatal error - cannot continue
        fprintf(stderr, "FATAL: Failed to allocate 400 DPI display buffers (%d pixels)\n", 
                FIXED_BUFFER_SIZE_400DPI);
        exit(EXIT_FAILURE);
    }
    
    // Initialize mutex
    pthread_mutex_init(&g_displayable_synth_mutex, NULL);
    
    fprintf(stderr, "Display buffers initialized: %d pixels (400 DPI fixed)\n", 
            FIXED_BUFFER_SIZE_400DPI);
}

// ✨ BRICK 1 STUBS: Fonctions utilisées par udpThread
// Ces stubs permettent la compilation, mais ne font rien (UDP reçoit mais ne traite pas)

// Image preprocessing (brique 3) - Signature correcte
int image_preprocess_frame(const uint8_t* image_r, const uint8_t* image_g, const uint8_t* image_b,
                          PreprocessedImageData* preprocessed) {
    (void)image_r; (void)image_g; (void)image_b; (void)preprocessed;
    // Stub: preprocessing sera ajouté dans brique 3
    return 0;
}

// Image sequencer processing (brique 3)
int image_sequencer_process_frame(ImageSequencer* seq, 
                                   const uint8_t* in_r, const uint8_t* in_g, const uint8_t* in_b,
                                   uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    (void)seq;
    // Stub: passthrough - copie l'entrée vers la sortie
    // Use get_cis_pixels_nb() for correct size (1728 for 200 DPI)
    int pixels = get_cis_pixels_nb();
    memcpy(out_r, in_r, pixels);
    memcpy(out_g, in_g, pixels);
    memcpy(out_b, in_b, pixels);
    return 0;
}

// LuxWave set image line (brique 4)
void synth_luxwave_set_image_line(LuxWaveState* state, const uint8_t* line, int length) {
    (void)state; (void)line; (void)length;
    // Stub: LuxWave sera ajouté dans brique 4
}

// Audio processing (brique 2)
void synth_AudioProcess(const uint8_t* r, const uint8_t* g, const uint8_t* b, void* doubleBuffer) {
    (void)r; (void)g; (void)b; (void)doubleBuffer;
    // Stub: audio processing sera ajouté dans brique 2
    // Pour l'instant, le plugin génère un test tone 440Hz (dans PluginProcessor)
}

// Note: get_cis_pixels_nb, logger_*, and load_luxstral_config sont déjà définis dans:
// - config_instrument.h (get_cis_pixels_nb - inline)
// - logger.c (logger_*)
// - config_loader.c (load_luxstral_config)
// Pas besoin de les redéfinir ici
