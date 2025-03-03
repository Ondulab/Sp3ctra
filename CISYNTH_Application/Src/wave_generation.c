/*
 * wave_generation.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "config.h"
#include "shared.h"

#include "stdio.h"
#include "stdlib.h"
#include "math.h"

#include "error.h"
#include "wave_generation.h"

volatile struct waveParams wavesGeneratorParams;

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static float calculate_frequency(uint32_t comma_cnt, volatile struct waveParams *params);
static uint32_t calculate_waveform(uint32_t current_aera_size, uint32_t current_unitary_waveform_cell, uint32_t buffer_len, volatile struct waveParams *params);

/* Private user code ---------------------------------------------------------*/

/**
 * @brief  calculate frequency,
 * @param  comma cnt
 * @retval frequency
 */
static float calculate_frequency(uint32_t comma_cnt, volatile struct waveParams *params)
{
    float frequency = 0.00;
    frequency = params->startFrequency * pow(2, ((float)comma_cnt / (12.0 * ((SEMITONE_PER_OCTAVE * (float)params->commaPerSemitone) / (12.00 / (log(2)) * log((params->startFrequency * 2.00) / (float)params->startFrequency))))));

    return frequency;
}

/**
 * @brief  calculate waveform,
 * @param  current_area_size,
 * @param  current_unitary_waveform_cell,
 * @param  buffer_len,
 * @param  waveParams *params,
 * @retval current_unitary_waveform_cell
 */
static uint32_t calculate_waveform(uint32_t current_aera_size, uint32_t current_unitary_waveform_cell, uint32_t buffer_len, volatile struct waveParams *params)
{
    unitary_waveform[current_unitary_waveform_cell] = 0;
    uint32_t max = 0;
    uint32_t currentValue = 0;
    uint32_t overshootCompensation = 0;

    switch (params->waveformType)
    {
    case SIN_WAVE:
        //fill unitary_waveform buffer with sinusoidal waveform for each comma
        for (uint32_t x = 0; x < current_aera_size; x++)
        {
            //sanity check
            if (current_unitary_waveform_cell < buffer_len)
            {
                unitary_waveform[current_unitary_waveform_cell] = ((sin((x * 2.00 * PI ) / (float)current_aera_size))) * (WAVE_AMP_RESOLUTION / 2.00);
            }
            current_unitary_waveform_cell++;
        }
        break;
    case SAW_WAVE:
        //compute de maximum overshoot value on positive phase
        for (uint32_t x = 0; x < (current_aera_size / 2); x++)
        {
            currentValue = 0;
            //sanity check
            if (current_unitary_waveform_cell < buffer_len)
            {
                //store overshoot value
                for (uint32_t n = 0; n < params->waveformOrder; n++)
                {
                    currentValue += pow(-1, n) * (WAVE_AMP_RESOLUTION / PI) * sin( (n + 1.00) * x * 2.00 * PI / (float)current_aera_size) / ((float)n + 1.00);
                    if (currentValue > max)
                        max = currentValue;
                }
            }
        }
        //compute overshoot compensation
        overshootCompensation = ((max * 2) - WAVE_AMP_RESOLUTION);
        //fill unitary_waveform buffer with saw waveform for each comma
        for (uint32_t x = 0; x < current_aera_size; x++)
        {
            //sanity check
            if (current_unitary_waveform_cell < buffer_len)
            {
                for (uint32_t n = 0; n < params->waveformOrder; n++)
                {
                    unitary_waveform[current_unitary_waveform_cell] += pow(-1, n) * ((WAVE_AMP_RESOLUTION - overshootCompensation) / PI) * sin( (n + 1.00) * x * 2.00 * PI / (float)current_aera_size) / ((float)n + 1.00);
                }
            }
            current_unitary_waveform_cell++;
        }
        break;
    case SQR_WAVE:
        //compute de maximum overshoot value on positive phase
        for (uint32_t x = 0; x < (current_aera_size / 2); x++)
        {
            currentValue = 0;
            //sanity check
            if (current_unitary_waveform_cell < buffer_len)
            {
                //store overshoot value
                for (uint32_t n = 0; n < params->waveformOrder; n++)
                {
                    currentValue += (2 * WAVE_AMP_RESOLUTION / PI) * sin( (2.00 * n + 1.00) * x * 2.00 * PI / (float)current_aera_size) / (2.00 * (float)n + 1.00);
                    if (currentValue > max)
                        max = currentValue;
                }
            }
        }
        //compute overshoot compensation
        overshootCompensation = ((max * 2) - WAVE_AMP_RESOLUTION);
        //fill unitary_waveform buffer with square waveform for each comma
        for (uint32_t x = 0; x < current_aera_size; x++)
        {
            //sanity check
            if (current_unitary_waveform_cell < buffer_len)
            {
                for (uint32_t n = 0; n < params->waveformOrder; n++)
                {
                    unitary_waveform[current_unitary_waveform_cell] += (2 * (WAVE_AMP_RESOLUTION - overshootCompensation) / PI) * sin( (2.00 * n + 1.00) * x * 2.00 * PI / (float)current_aera_size) / (2.00 * (float)n + 1.00);
                }
            }
            current_unitary_waveform_cell++;
        }
        break;
    }

    return current_unitary_waveform_cell;
}

/**
 * @brief  build_waves,
 * @param  unitary_waveform pointer,
 * @param  waves structure pointer,
 * @param  params wave parameters,
 * @retval buffer length on success, negative value otherwise
 */
uint32_t init_waves(volatile float *unitary_waveform, volatile struct wave *waves, volatile struct waveParams *parameters)
{
    uint32_t buffer_len = 0;
    uint32_t note = 0;
    uint32_t current_unitary_waveform_cell = 0;

    printf("---------- WAVES INIT ---------\n");
    printf("-------------------------------\n");

    //compute cell number for storage all oscillators waveform
    for (uint32_t comma_cnt = 0; comma_cnt < (SEMITONE_PER_OCTAVE * parameters->commaPerSemitone); comma_cnt++)
    {
        //store only first octave_coeff frequencies ---- logarithmic distribution
        float frequency = calculate_frequency(comma_cnt, parameters);
        buffer_len += (uint32_t)(SAMPLING_FREQUENCY / frequency);
    }

    //todo add check buffer_len size

    //compute and store the waveform into unitary_waveform only for the reference octave_coeff
    for (uint32_t comma_cnt = 0; comma_cnt < (SEMITONE_PER_OCTAVE * parameters->commaPerSemitone); comma_cnt++)
    {
        //compute frequency for each comma into the reference octave_coeff
        float frequency = calculate_frequency(comma_cnt, parameters);

        //current aera size is the number of char cell for storage a waveform at the current frequency (one pixel per frequency oscillator)
        uint32_t current_aera_size = (uint32_t)((SAMPLING_FREQUENCY / frequency) / 2.00); //To save ram usage we divide per two for store first octave (fundamental octave use octave divider)

        current_unitary_waveform_cell = calculate_waveform(current_aera_size, current_unitary_waveform_cell, buffer_len, parameters);

        //for each octave (only the first octave_coeff stay in RAM, for multiple octave_coeff start_ptr stay on reference octave waveform but current_ptr jump cell according to multiple frequencies)
        for (uint32_t octave = 0; octave <= (NUMBER_OF_NOTES / (SEMITONE_PER_OCTAVE * parameters->commaPerSemitone)); octave++)
        {
            //compute the current pixel to associate an waveform pointer,
            // *** is current pix, --- octave separation
            // *---------*---------*---------*---------*---------*---------*---------*--------- for current comma at each octave
            // ---*---------*---------*---------*---------*---------*---------*---------*------ for the second comma...
            // ------*---------*---------*---------*---------*---------*---------*---------*---
            // ---------*---------*---------*---------*---------*---------*---------*---------*
            note = comma_cnt + (SEMITONE_PER_OCTAVE * parameters->commaPerSemitone) * octave;
            //sanity check, if user demand is't possible
            if (note < NUMBER_OF_NOTES)
            {
                //store frequencies
                waves[note].frequency = frequency * pow(2, octave);
                //store aera size
                waves[note].area_size = current_aera_size;
                //store pointer address
                waves[note].start_ptr = &unitary_waveform[current_unitary_waveform_cell - current_aera_size];
                //set current pointer at the same address
                waves[note].current_idx = 0;

                if (octave == 0)
                {
                    //store octave number
                    waves[note].octave_coeff = 1;
                    //store octave divider
                    waves[note].octave_divider = 2;
                    //store max_volume_increment
                    waves[note].max_volume_increment = ((*(waves[note].start_ptr + 1)) / 2.00) / (WAVE_AMP_RESOLUTION / VOLUME_AMP_RESOLUTION);
                    waves[note].max_volume_decrement = waves[note].max_volume_increment;
                }
                else
                {
                    //store octave number
                    waves[note].octave_coeff = pow(2, octave - 1);
                    //store octave divider
                    waves[note].octave_divider = 1;
                    //store max_volume_increment
                    waves[note].max_volume_increment = (*(waves[note].start_ptr + waves[note].octave_coeff)) / (WAVE_AMP_RESOLUTION / VOLUME_AMP_RESOLUTION);
                    waves[note].max_volume_decrement = waves[note].max_volume_increment;
                }
            }
        }
    }

    if (note < NUMBER_OF_NOTES)
    {
        printf("Configuration fail, current pix : %d\n", (int)note);
        die("wave init failed");
    }

    return buffer_len;
}
