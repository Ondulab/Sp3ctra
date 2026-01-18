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
#include <math.h>

// CRITICAL: Fixed buffer size - ALWAYS 400 DPI (3456 pixels)
// This allows runtime switching between 200/400 DPI without buffer reallocation
#define FIXED_BUFFER_SIZE_400DPI 3456

// 🔧 VST Global buffers for synthesis (used by processBlock)
// These are stub pointers - actual allocation in Sp3ctraCore
#include "../../src/audio/buffers/audio_image_buffers.h"
#include "../../src/threading/multithreading.h"

AudioImageBuffers *g_audioImageBuffers = NULL;
DoubleBuffer *g_doubleBuffer = NULL;
static DoubleBuffer g_doubleBuffer_instance;  // Static instance for VST

// Configuration globale - Now initialized from APVTS (AudioProcessorValueTreeState)
// These values will be set by PluginProcessor based on user preferences
sp3ctra_config_t g_sp3ctra_config = {
    // Audio settings (will be overridden by prepareToPlay)
    .sampling_frequency = 48000,
    .audio_buffer_size = 512,
    
    // Logging
    .log_level = 2, // LOG_LEVEL_INFO (default, overridden by APVTS)
    
    // Network
    .udp_address = "239.100.100.100",
    .udp_port = 55151,
    .multicast_interface = "",
    
    // Sensor
    .sensor_dpi = 400,  // DEFAULT: 400 DPI (can be 200, but buffers stay 3456 pixels)
    
    // 🔧 CRITICAL: LuxStral synthesis parameters (from sp3ctra.ini [synth_luxstral])
    .low_frequency = 65.41f,      // C2
    .high_frequency = 16744.04f,  // ~8 octaves above C2
    .start_frequency = 65.41f,    // Backward compatibility
    .pixels_per_note = 1,         // Maximum resolution
    .num_workers = 8,             // Thread pool workers
    
    // Musical scale
    .semitone_per_octave = 12,
    .comma_per_semitone = 36,
    
    // Envelope parameters (very fast response)
    .tau_up_base_ms = 0.5f,
    .tau_down_base_ms = 0.5f,
    
    // 🔧 CRITICAL: Image processing parameters (from sp3ctra.ini [image_processing_luxstral])
    .invert_intensity = 1,                       // Dark pixels louder
    .additive_enable_non_linear_mapping = 1,     // Gamma enabled
    .additive_gamma_value = 4.8f,                // Gamma exponent
    .additive_contrast_min = 0.21f,              // Min volume for blurred images
    .additive_contrast_adjustment_power = 0.5f,  // Contrast curve exponent
    
    // 🔧 CRITICAL: Volume and dynamics (from sp3ctra.ini [summation_normalization])
    .volume_weighting_exponent = 0.1f,           // Strong oscillator domination
    .summation_response_exponent = 2.0f,         // Compression exponent
    .noise_gate_threshold = 0.005f,              // Noise suppression
    
    // 🔧 CRITICAL: Soft limiter (prevents hard clipping)
    .soft_limit_threshold = 0.8f,                // Start soft limit at 80%
    .soft_limit_knee = 0.2f,                     // Smooth transition
    
    // 🔧 CRITICAL: Stereo processing
    .stereo_mode_enabled = 1,
    .stereo_blue_red_weight = 0.7f,           // Primary color axis weight
    .stereo_cyan_yellow_weight = 0.3f,        // Secondary color axis weight
    .stereo_temperature_amplification = 2.5f, // Temperature effect amplification
    .stereo_temperature_curve_exponent = 0.7f // Non-linear curve exponent (must be > 0!)
};

// ✨ BRICK 1 STUBS: Variables globales utilisées par udpThread
// Ces stubs seront remplacés dans les prochaines briques de migration

// Image sequencer global (pour l'instant NULL, brique 3)
ImageSequencer *g_image_sequencer = NULL;

// LuxWave state global (pour l'instant stub, brique 4)
LuxWaveState g_luxwave_state;

// NOTE: g_displayable_synth_R/G/B are now defined in synth_luxstral_state.c
// Removed stubs - using real LuxStral implementation

// ✨ BRICK 1 STUBS: Fonctions utilisées par udpThread
// Ces stubs permettent la compilation, mais ne font rien (UDP reçoit mais ne traite pas)

// Image preprocessing (LuxStral pipeline)
// NOTE: Real implementation is in src/processing/image_preprocessor.c
// Stub removed to avoid duplicate symbol during VST link.

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

// NOTE: synth_AudioProcess is now defined in synth_luxstral.c
// Removed stub - using real LuxStral implementation

// Note: get_cis_pixels_nb, logger_*, and load_luxstral_config sont déjà définis dans:
// - config_instrument.h (get_cis_pixels_nb - inline)
// - logger.c (logger_*)
// - config_loader.c (load_luxstral_config)
// Pas besoin de les redéfinir ici
