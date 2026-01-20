/*
 * wave_generation.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "vst_adapters_c.h"
#include "wave_generation.h"
#include "synth_luxstral_algorithms.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include <stdatomic.h>

#include "logger.h"
#include "wave_generation.h"

#define PI (3.14159265358979323846)

/* Global variables definitions (moved from shared.c) */
volatile struct waveParams wavesGeneratorParams;
volatile struct wave *waves = NULL;  // Now a pointer (allocated in synth_luxstral_runtime.c)
volatile float *unitary_waveform = NULL;  // Now a pointer (allocated in synth_luxstral_runtime.c)

/**************************************************************************************
 * Hot-reload frequency range - Simplified state machine
 * Only 2 states: IDLE and PENDING (regeneration happens immediately)
 * Global fade provides smooth transitions automatically
 **************************************************************************************/
typedef enum {
    FREQ_REINIT_IDLE = 0,       // No reinit pending
    FREQ_REINIT_PENDING,        // Reinit requested, will process next buffer
} freq_reinit_state_t;

static _Atomic int g_freq_reinit_state = FREQ_REINIT_IDLE;

// Global fade coefficient for smooth transitions (0.0 to 1.0)
// Applied to the entire output signal, not individual oscillators
static float g_global_fade_current = 1.0f;
static float g_global_fade_target = 1.0f;

// Exponential fade coefficient (sample-by-sample smoothing)
// tau = 50ms at 48kHz → alpha = 1 - exp(-1/(0.05*48000)) ≈ 0.000416
#define GLOBAL_FADE_ALPHA 0.0004f

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static float calculate_frequency_for_note(int note, int total_notes, float low_freq, float high_freq);
static uint32_t calculate_waveform(uint32_t current_aera_size,
                                   uint32_t current_unitary_waveform_cell,
                                   uint32_t buffer_len,
                                   volatile struct waveParams *params);

/* Private user code ---------------------------------------------------------*/

/**
 * @brief Calculate frequency for a specific note using logarithmic distribution
 * 
 * NEW IMPLEMENTATION: Direct logarithmic interpolation between low_frequency and high_frequency
 * Formula: freq = low_freq * pow(high_freq / low_freq, note / (total_notes - 1))
 * 
 * This gives independent control of both frequency bounds.
 * 
 * @param note Note index (0 to total_notes-1)
 * @param total_notes Total number of notes
 * @param low_freq Lower frequency bound
 * @param high_freq Upper frequency bound
 * @return Frequency in Hz
 */
static float calculate_frequency_for_note(int note, int total_notes, float low_freq, float high_freq) {
    if (total_notes <= 1) return low_freq;
    
    // Logarithmic interpolation: freq = low * (high/low)^(note/(total-1))
    float ratio = (float)note / (float)(total_notes - 1);
    float frequency = low_freq * powf(high_freq / low_freq, ratio);
    
    return frequency;
}

static uint32_t calculate_waveform(uint32_t current_aera_size,
                                   uint32_t current_unitary_waveform_cell,
                                   uint32_t buffer_len,
                                   volatile struct waveParams *params) {
  (void)params; // Suppress unused parameter warning

  unitary_waveform[current_unitary_waveform_cell] = 0;

  // Generate sinusoidal waveform (SIN is now implicit)
  for (uint32_t x = 0; x < current_aera_size; x++) {
    // sanity check
    if (current_unitary_waveform_cell < buffer_len) {
      unitary_waveform[current_unitary_waveform_cell] =
          ((sin((x * 2.00 * PI) / (float)current_aera_size))) *
          (WAVE_AMP_RESOLUTION / 2.00);
    }
    current_unitary_waveform_cell++;
  }

  return current_unitary_waveform_cell;
}

/**
 * @brief Initialize waves with logarithmic distribution between low_frequency and high_frequency
 * 
 * REFACTORED: Uses g_sp3ctra_config.low_frequency and high_frequency directly
 * for independent control of both frequency bounds.
 * 
 * MEMORY OPTIMIZATION: Waveforms are generated only for the first octave,
 * then higher octaves reuse them with octave_coeff multiplier.
 */
uint32_t init_waves(volatile float *unitary_waveform,
                    volatile struct wave *waves,
                    volatile struct waveParams *parameters) {
  uint32_t buffer_len = 0;
  uint32_t note = 0;
  uint32_t current_unitary_waveform_cell = 0;
  
  const int total_notes = get_current_number_of_notes();
  const float low_freq = g_sp3ctra_config.low_frequency;
  const float high_freq = g_sp3ctra_config.high_frequency;
  const int sample_rate = g_sp3ctra_config.sampling_frequency;
  
  // Calculate number of octaves and notes per octave for memory optimization
  float num_octaves = log2f(high_freq / low_freq);
  int num_full_octaves = (int)ceilf(num_octaves);
  if (num_full_octaves < 1) num_full_octaves = 1;
  
  // Notes in first octave (reference octave for waveform generation)
  int notes_per_octave = total_notes / num_full_octaves;
  if (notes_per_octave < 1) notes_per_octave = 1;
  
  // Effective comma per semitone for info
  float effective_comma = (float)notes_per_octave / 12.0f;

  log_info("SYNTH", "---------- WAVES INIT ---------");
  log_info("SYNTH", "Freq range: %.1f - %.1f Hz (%.2f octaves)", low_freq, high_freq, num_octaves);
  log_info("SYNTH", "Notes: %d, Notes/octave: %d, Effective commas/semitone: %.2f", 
           total_notes, notes_per_octave, effective_comma);

  // First pass: Calculate buffer_len needed for first octave waveforms only
  for (int comma_cnt = 0; comma_cnt < notes_per_octave; comma_cnt++) {
    // First octave: frequencies from low_freq to low_freq * 2
    float ratio = (float)comma_cnt / (float)notes_per_octave;
    float frequency = low_freq * powf(2.0f, ratio);  // Within first octave
    uint32_t area_size = (uint32_t)(sample_rate / frequency);
    if (area_size < 2) area_size = 2;
    buffer_len += area_size;
  }

  log_info("SYNTH", "Waveform buffer: %u samples (first octave only)", buffer_len);

  // Second pass: Generate waveforms for first octave and assign to all notes
  current_unitary_waveform_cell = 0;
  
  for (int comma_cnt = 0; comma_cnt < notes_per_octave; comma_cnt++) {
    // Calculate base frequency for this comma (first octave)
    float ratio = (float)comma_cnt / (float)notes_per_octave;
    float base_frequency = low_freq * powf(2.0f, ratio);
    
    // Calculate area size for base frequency
    uint32_t current_area_size = (uint32_t)(sample_rate / base_frequency);
    if (current_area_size < 2) current_area_size = 2;
    
    // Generate waveform for this base frequency
    current_unitary_waveform_cell = 
        calculate_waveform(current_area_size, current_unitary_waveform_cell,
                           buffer_len, parameters);

    // Assign this waveform to all octaves
    for (int octave = 0; octave < num_full_octaves + 1; octave++) {
      note = comma_cnt + notes_per_octave * octave;
      
      if ((int)note < total_notes) {
        // Calculate actual frequency for this note using logarithmic distribution
        float note_frequency = calculate_frequency_for_note(note, total_notes, low_freq, high_freq);
        
        // Store frequency
        waves[note].frequency = note_frequency;
        // Store area size (from base frequency)
        waves[note].area_size = current_area_size;
        // Store pointer to base waveform
        waves[note].start_ptr = &unitary_waveform[current_unitary_waveform_cell - current_area_size];
        // Reset index
        waves[note].current_idx = 0;
        
        // Octave coefficient: how much faster to step through waveform
        // Higher octave = 2x frequency = step through waveform 2x faster
        waves[note].octave_coeff = (uint32_t)powf(2.0f, (float)octave);
        waves[note].octave_divider = 1;
      }
    }
  }

  // Log first and last note info
  if (total_notes > 0) {
    log_info("SYNTH", "First note: %.2f Hz, area_size=%u, oct_coeff=%u", 
             waves[0].frequency, waves[0].area_size, waves[0].octave_coeff);
    log_info("SYNTH", "Last note: %.2f Hz, area_size=%u, oct_coeff=%u", 
             waves[total_notes-1].frequency, waves[total_notes-1].area_size, 
             waves[total_notes-1].octave_coeff);
  }

  // Sanity check
  if ((int)note < total_notes - 1) {
    log_warning("SYNTH", "Wave generation: only %d notes configured (expected %d)", 
                (int)note + 1, total_notes);
  }

  log_info("SYNTH", "-------------------------------");

  return buffer_len;
}

/**************************************************************************************
 * Hot-reload frequency range - Simplified Implementation
 * Global fade handles all transitions automatically
 **************************************************************************************/

/**
 * @brief Request frequency range reinit from UI thread
 * Immediately triggers fade-out via global fade, regeneration happens next buffer
 */
void request_frequency_reinit(void) {
    // Only request if not already in progress
    int expected = FREQ_REINIT_IDLE;
    if (atomic_compare_exchange_strong(&g_freq_reinit_state, &expected, FREQ_REINIT_PENDING)) {
        // Start fade-out immediately (global fade will handle smooth transition)
        g_global_fade_target = 0.0f;
        log_info("FREQ_REINIT", "Frequency reinit requested - starting global fade out");
    } else {
        log_warning("FREQ_REINIT", "Reinit already in progress, ignoring request");
    }
}

/**
 * @brief Check if frequency reinit is currently in progress
 * @return 1 if pending, 0 otherwise
 */
int is_frequency_reinit_fading_out(void) {
    return atomic_load(&g_freq_reinit_state) == FREQ_REINIT_PENDING;
}

/**
 * @brief Check and process pending frequency reinit
 * Called at the beginning of synth_IfftMode() BEFORE workers start
 * Regenerates immediately - global fade ensures smooth transitions
 * @return 1 if reinit was performed, 0 otherwise
 */
int check_and_process_frequency_reinit(void) {
    int state = atomic_load(&g_freq_reinit_state);
    
    if (state == FREQ_REINIT_IDLE) {
        return 0;  // Nothing to do
    }
    
    if (state == FREQ_REINIT_PENDING) {
        // CRITICAL: Workers are waiting on start_barrier, safe to regenerate!
        
        // Update wavesGeneratorParams with new frequency from config
        wavesGeneratorParams.startFrequency = (uint32_t)g_sp3ctra_config.start_frequency;
        wavesGeneratorParams.commaPerSemitone = g_sp3ctra_config.comma_per_semitone;
        
        log_info("FREQ_REINIT", "Regenerating waveforms for freq range %.1f - %.1f Hz",
                 g_sp3ctra_config.low_frequency, g_sp3ctra_config.high_frequency);
        
        // Regenerate all waveforms
        init_waves(unitary_waveform, waves, &wavesGeneratorParams);
        
        // Recompute GAP_LIMITER coefficients (depend on frequency)
        update_gap_limiter_coefficients();
        
        // CRITICAL: Randomize phase to avoid constructive interference
        // Without this, all oscillators start at phase 0 → harsh sound
        int num_notes = get_current_number_of_notes();
        for (int note = 0; note < num_notes; note++) {
#ifdef __APPLE__
            uint32_t aRandom32bit = arc4random();
#else
            uint32_t aRandom32bit = (uint32_t)rand();
#endif
            waves[note].current_idx = aRandom32bit % waves[note].area_size;
            waves[note].current_volume = 0.0f;
            // target_volume will be set by image data naturally
        }
        
        // Set global fade target back to 1.0 for smooth fade-in
        g_global_fade_target = 1.0f;
        
        // Done!
        atomic_store(&g_freq_reinit_state, FREQ_REINIT_IDLE);
        log_info("FREQ_REINIT", "Frequency reinit complete - global fade will handle transition");
        
        return 1;  // Reinit was performed
    }
    
    return 0;
}

/**************************************************************************************
 * Global fade functions - Applied to output signal for smooth transitions
 **************************************************************************************/

/**
 * @brief Get and update global fade factor for a single sample
 * Call this for each output sample to apply smooth exponential fade
 * @return Current fade factor (0.0 to 1.0)
 */
float get_global_fade_factor_and_update(void) {
    // Exponential smoothing: current += alpha * (target - current)
    g_global_fade_current += GLOBAL_FADE_ALPHA * (g_global_fade_target - g_global_fade_current);
    
    // Clamp to avoid denormals
    if (g_global_fade_current < 0.0001f && g_global_fade_target == 0.0f) {
        g_global_fade_current = 0.0f;
    }
    if (g_global_fade_current > 0.9999f && g_global_fade_target == 1.0f) {
        g_global_fade_current = 1.0f;
    }
    
    return g_global_fade_current;
}

/**
 * @brief Get current global fade factor without updating
 * @return Current fade factor (0.0 to 1.0)
 */
float get_global_fade_factor(void) {
    return g_global_fade_current;
}
