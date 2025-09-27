/*
 * wave_generation.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "config.h"
#include "wave_generation.h"
#include "../../config/config_loader.h"

#include "math.h"
#include "stdio.h"
#include "stdlib.h"

#include "error.h"
#include "wave_generation.h"

#define PI (3.14159265358979323846)

/* Global variables definitions (moved from shared.c) */
volatile struct waveParams wavesGeneratorParams;
volatile struct wave waves[MAX_NUMBER_OF_NOTES];
volatile float unitary_waveform[WAVEFORM_TABLE_SIZE];
volatile q24_t unitary_waveform_q24[WAVEFORM_TABLE_SIZE] Q24_CACHE_ALIGN;

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static float calculate_frequency(uint32_t comma_cnt,
                                 volatile struct waveParams *params);
static uint32_t calculate_waveform(uint32_t current_aera_size,
                                   uint32_t current_unitary_waveform_cell,
                                   uint32_t buffer_len,
                                   volatile struct waveParams *params);
static uint32_t calculate_waveform_q24(uint32_t current_aera_size,
                                       uint32_t current_unitary_waveform_cell,
                                       uint32_t buffer_len,
                                       volatile struct waveParams *params);
static void convert_float_to_q24_waveform(void);
static void init_waves_q24_parameters(volatile struct wave *waves);

/* Private user code ---------------------------------------------------------*/

static float calculate_frequency(uint32_t comma_cnt,
                                 volatile struct waveParams *params) {
  float frequency = 0.00;
  frequency =
      params->startFrequency *
      pow(2, ((float)comma_cnt /
              (12.0 * ((g_additive_config.semitone_per_octave * (float)params->commaPerSemitone) /
                       (12.00 / (log(2)) *
                        log((params->startFrequency * 2.00) /
                            (float)params->startFrequency))))));

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

uint32_t init_waves(volatile float *unitary_waveform,
                    volatile struct wave *waves,
                    volatile struct waveParams *parameters) {
  uint32_t buffer_len = 0;
  uint32_t note = 0;
  uint32_t current_unitary_waveform_cell = 0;

  printf("---------- WAVES INIT ---------\n");
  printf("-------------------------------\n");

  // compute cell number for storage all oscillators waveform
  for (uint32_t comma_cnt = 0;
       comma_cnt < (g_additive_config.semitone_per_octave * parameters->commaPerSemitone);
       comma_cnt++) {
    // store only first octave_coeff frequencies ---- logarithmic distribution
    float frequency = calculate_frequency(comma_cnt, parameters);
    buffer_len += (uint32_t)(SAMPLING_FREQUENCY / frequency);
  }

  // todo add check buffer_len size

  // compute and store the waveform into unitary_waveform only for the reference
  // octave_coeff
  for (uint32_t comma_cnt = 0;
       comma_cnt < (g_additive_config.semitone_per_octave * parameters->commaPerSemitone);
       comma_cnt++) {
    // compute frequency for each comma into the reference octave_coeff
    float frequency = calculate_frequency(comma_cnt, parameters);

    // current aera size is the number of char cell for storage a waveform at
    // the current frequency (one pixel per frequency oscillator)
    uint32_t current_aera_size = (uint32_t)((SAMPLING_FREQUENCY / frequency));

    current_unitary_waveform_cell =
        calculate_waveform(current_aera_size, current_unitary_waveform_cell,
                           buffer_len, parameters);

    // for each octave (only the first octave_coeff stay in RAM, for multiple
    // octave_coeff start_ptr stay on reference octave waveform but current_ptr
    // jump cell according to multiple frequencies)
    for (uint32_t octave = 0;
         octave <= (get_current_number_of_notes() /
                    (g_additive_config.semitone_per_octave * parameters->commaPerSemitone));
         octave++) {
      // compute the current pixel to associate an waveform pointer,
      //  *** is current pix, --- octave separation
      //  *---------*---------*---------*---------*---------*---------*---------*---------
      //  for current comma at each octave
      //  ---*---------*---------*---------*---------*---------*---------*---------*------
      //  for the second comma...
      //  ------*---------*---------*---------*---------*---------*---------*---------*---
      //  ---------*---------*---------*---------*---------*---------*---------*---------*
      note = comma_cnt +
             (g_additive_config.semitone_per_octave * parameters->commaPerSemitone) * octave;
      // sanity check, if user demand is't possible
      if ((int)note < get_current_number_of_notes()) {
        // store frequencies
        waves[note].frequency = frequency * pow(2, octave);
        // store aera size
        waves[note].area_size = current_aera_size;
        // store pointer address
        waves[note].start_ptr =
            &unitary_waveform[current_unitary_waveform_cell -
                              current_aera_size];
        // set current pointer at the same address
        waves[note].current_idx = 0;

        // store octave number
        waves[note].octave_coeff = pow(2, octave);
        // store octave divider
        waves[note].octave_divider = 1;
        // store max_volume_increment
        waves[note].max_volume_increment =
            (*(waves[note].start_ptr + waves[note].octave_coeff)) /
            (WAVE_AMP_RESOLUTION / VOLUME_AMP_RESOLUTION);
        waves[note].max_volume_decrement = waves[note].max_volume_increment;
      }
    }
  }

  if ((int)note < get_current_number_of_notes()) {
    printf("Configuration fail, current pix : %d\n", (int)note);
    die("wave init failed");
  }

  // Initialize Q24 waveform table and parameters
  convert_float_to_q24_waveform();
  init_waves_q24_parameters(waves);
  
  printf("Q24 waveform conversion completed\n");

  return buffer_len;
}

/**
 * @brief Generate Q24 sinusoidal waveform
 * @param current_aera_size Size of current waveform area
 * @param current_unitary_waveform_cell Current cell index
 * @param buffer_len Total buffer length
 * @param params Wave parameters (unused)
 * @retval Updated cell index
 */
static uint32_t calculate_waveform_q24(uint32_t current_aera_size,
                                       uint32_t current_unitary_waveform_cell,
                                       uint32_t buffer_len,
                                       volatile struct waveParams *params) {
  (void)params; // Suppress unused parameter warning

  unitary_waveform_q24[current_unitary_waveform_cell] = 0;

  // Generate sinusoidal waveform in Q24 format
  for (uint32_t x = 0; x < current_aera_size; x++) {
    // sanity check
    if (current_unitary_waveform_cell < buffer_len) {
      // Calculate sine value and convert to Q24
      float sine_value = sin((x * 2.00 * PI) / (float)current_aera_size);
      // Scale by Q24_ONE/2 (equivalent to WAVE_AMP_RESOLUTION_Q24/2)
      unitary_waveform_q24[current_unitary_waveform_cell] = 
          FLOAT_TO_Q24(sine_value * 0.5f);
    }
    current_unitary_waveform_cell++;
  }

  return current_unitary_waveform_cell;
}

/**
 * @brief Convert existing float waveform to Q24 format
 * @retval None
 */
static void convert_float_to_q24_waveform(void) {
  printf("Converting float waveform to Q24...\n");
  
  for (uint32_t i = 0; i < WAVEFORM_TABLE_SIZE; i++) {
    // Convert float sample to Q24
    // Normalize by WAVE_AMP_RESOLUTION since float samples are scaled by this value
    float normalized_sample = unitary_waveform[i] / (WAVE_AMP_RESOLUTION / 2.0f);
    unitary_waveform_q24[i] = FLOAT_TO_Q24(normalized_sample);
  }
  
  printf("Q24 waveform conversion: %u samples converted\n", WAVEFORM_TABLE_SIZE);
}

/**
 * @brief Initialize Q24 parameters for all waves
 * @param waves Wave array to initialize
 * @retval None
 */
static void init_waves_q24_parameters(volatile struct wave *waves) {
  printf("Initializing Q24 wave parameters...\n");
  
  int current_notes = get_current_number_of_notes();
  
  for (int note = 0; note < current_notes; note++) {
    // Set Q24 waveform pointer (offset from float pointer)
    ptrdiff_t offset = waves[note].start_ptr - unitary_waveform;
    waves[note].start_ptr_q24 = &unitary_waveform_q24[offset];
    
    // Convert volume parameters to Q24 (normalize legacy float units by VOLUME_AMP_RESOLUTION)
    waves[note].target_volume_q24 = FLOAT_TO_Q24(waves[note].target_volume / (float)VOLUME_AMP_RESOLUTION);
    waves[note].current_volume_q24 = FLOAT_TO_Q24(waves[note].current_volume / (float)VOLUME_AMP_RESOLUTION);
    waves[note].volume_increment_q24 = FLOAT_TO_Q24(waves[note].volume_increment / (float)VOLUME_AMP_RESOLUTION);
    waves[note].max_volume_increment_q24 = FLOAT_TO_Q24(waves[note].max_volume_increment / (float)VOLUME_AMP_RESOLUTION);
    waves[note].volume_decrement_q24 = FLOAT_TO_Q24(waves[note].volume_decrement / (float)VOLUME_AMP_RESOLUTION);
    waves[note].max_volume_decrement_q24 = FLOAT_TO_Q24(waves[note].max_volume_decrement / (float)VOLUME_AMP_RESOLUTION);
    
    // Convert stereo parameters to Q24
    waves[note].pan_position_q24 = FLOAT_TO_Q24(waves[note].pan_position);
    waves[note].left_gain_q24 = FLOAT_TO_Q24(waves[note].left_gain);
    waves[note].right_gain_q24 = FLOAT_TO_Q24(waves[note].right_gain);
  }
  
  printf("Q24 wave parameters initialized for %d notes\n", current_notes);
}
