/*
 * synth.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "config.h"

#include "stdlib.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>
#include <Accelerate/Accelerate.h>
#include <pthread.h>

#include "error.h"
#include "wave_generation.h"
#include "shared.h"
#include "audio.h"
#include "synth.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static volatile int32_t* half_audio_ptr;
static volatile int32_t* full_audio_ptr;
static int32_t imageRef[NUMBER_OF_NOTES] = {0};

/* Variable used to get converted value */
//ToChange__IO uint16_t uhADCxConvertedValue = 0;

/* Private function prototypes -----------------------------------------------*/
static uint32_t greyScale(uint8_t *buffer_R,
                          uint8_t *buffer_G,
                          uint8_t *buffer_B,
                          int32_t *gray,
                          uint32_t size);
void synth_IfftMode(int32_t *imageData, float *audioData);
static void synth_DirectMode(volatile int32_t *imageData, volatile int32_t *audioData, uint16_t CV_in);

/* Private user code ---------------------------------------------------------*/

void sub_int32(const int32_t *a, const int32_t *b, int32_t *result, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] - b[i];
    }
}

void clip_int32(int32_t *array, int32_t min, int32_t max, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (array[i] < min) {
            array[i] = min;
        } else if (array[i] > max) {
            array[i] = max;
        }
    }
}

void mult_float(const float *a, const float *b, float *result, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] * b[i];
    }
}

void add_float(const float *a, const float *b, float *result, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] + b[i];
    }
}

void scale_float(float *array, float scale, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        array[i] *= scale;
    }
}

void fill_float(float value, float *array, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        array[i] = value;
    }
}

void fill_int32(int32_t value, int32_t *array, size_t length)
{
    if (array == NULL) {
        return; // Gestion d'erreur si le tableau est NULL
    }

    for (size_t i = 0; i < length; ++i) {
        array[i] = value;
    }
}

int32_t synth_IfftInit(void)
{
    //ToChangestatic DAC_ChannelConfTypeDef sConfig;

    int32_t buffer_len = 0;

    printf("---------- SYNTH INIT ---------\n");
    printf("-------------------------------\n");

    // initialize default parameters
    wavesGeneratorParams.commaPerSemitone = COMMA_PER_SEMITONE;
    wavesGeneratorParams.startFrequency = START_FREQUENCY;
    wavesGeneratorParams.harmonizationType = MAJOR;
    wavesGeneratorParams.harmonizationLevel = 100;
    wavesGeneratorParams.waveformType = SIN_WAVE;
    wavesGeneratorParams.waveformOrder = 1;

    buffer_len = init_waves(unitary_waveform, waves, &wavesGeneratorParams); //24002070 24000C30
    
    int32_t value = VOLUME_INCREMENT;
    
    if (value == 0)
        value = 0;
    if (value > 1000)
        value = 100;
    for (int32_t note = 0; note < NUMBER_OF_NOTES; note++)
    {
        waves[note].volume_increment = 1.00/(float)value * waves[note].max_volume_increment;
    }
    
    value = VOLUME_DECREMENT;
    
    if (value == 0)
        value = 0;
    if (value > 1000)
        value = 100;
    for (int32_t note = 0; note < NUMBER_OF_NOTES; note++)
    {
        waves[note].volume_decrement = 1.00/(float)value * waves[note].max_volume_decrement;
    }

    // start with random index
    for (uint32_t i = 0; i < NUMBER_OF_NOTES; i++)
    {
        uint32_t aRandom32bit = arc4random();
        waves[i].current_idx = aRandom32bit % waves[i].area_size;
        waves[i].current_volume = 0;
    }

    if (buffer_len > (2400000-1))
    {
        printf("RAM overflow");
        die("synth init failed");
        return -1;
    }

    printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);
    printf("Buffer lengh = %d uint16\n", (int)buffer_len);


    uint8_t FreqStr[256] = {0};
    sprintf((char *)FreqStr, " %d -> %dHz      Octave:%d", (int)waves[0].frequency, (int)waves[NUMBER_OF_NOTES - 1].frequency, (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

    printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency, (int)waves[0].area_size);
    printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n", (int)waves[NUMBER_OF_NOTES - 1].frequency, (int)waves[NUMBER_OF_NOTES - 1].area_size / (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff), (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

    printf("-------------------------------\n");

#ifdef PRINT_IFFT_FREQUENCY
    for (uint32_t pix = 0; pix < NUMBER_OF_NOTES; pix++)
    {
        printf("FREQ = %0.2f, SIZE = %d, OCTAVE = %d\n", waves[pix].frequency, (int)waves[pix].area_size, (int)waves[pix].octave_coeff);
#ifdef PRINT_IFFT_FREQUENCY_FULL
        int32_t output = 0;
        for (uint32_t idx = 0; idx < (waves[pix].area_size / waves[pix].octave_coeff); idx++)
        {
            output = *(waves[pix].start_ptr + (idx *  waves[pix].octave_coeff));
            printf("%d\n", output);
        }
#endif
    }
    printf("-------------------------------\n");
    printf("Buffer lengh = %d uint16\n", (int)buffer_len);
    
    
    printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency, (int)waves[0].area_size);
    printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n", (int)waves[NUMBER_OF_NOTES - 1].frequency, (int)waves[NUMBER_OF_NOTES - 1].area_size / (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff), (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));
    
    printf("-------------------------------\n");
#endif

    printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);
    
    fill_int32(65535, (int32_t *)imageRef, NUMBER_OF_NOTES);
    
    return 0;
}

uint32_t greyScale(uint8_t *buffer_R,
                   uint8_t *buffer_G,
                   uint8_t *buffer_B,
                   int32_t *gray,
                   uint32_t size)
{
    uint32_t i = 0;

    for (i = 0; i < size; i++)
    {
        uint32_t r = (uint32_t)buffer_R[i];
        uint32_t g = (uint32_t)buffer_G[i];
        uint32_t b = (uint32_t)buffer_B[i];

        uint32_t weighted = (r * 299 + g * 587 + b * 114);
        // Normalisation en 16 bits (0 - 65535)
        gray[i] = (int32_t)((weighted * 65535UL) / 255000UL);
    }

    return 0;
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @param  htim : TIM handle
 * @retval None
 */
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
void synth_DirectMode(volatile int32_t *imageData, volatile int32_t *audioData, uint16_t CV_in)
{
    static int32_t signal_R;
    static int32_t image_idx_UP = 0;
    static int32_t image_idx_DW = 0;
    static int32_t buff_idx = 0;
    static int32_t idx = 0;
    static int32_t imageBuffer_q31[CIS_MAX_PIXELS_NB+1];
    static int32_t imageData_ref_q31[CIS_MAX_PIXELS_NB+1];
    static const float noScaled_freq = (SAMPLING_FREQUENCY) / CIS_MAX_PIXELS_NB; // 27,7 Hz;
    static float note_freq = 0.0;
    static float scale_factor = 0.0;
    static uint32_t scaledPixel_nb = 0;
    
    static const float LAMIN = 440;
    static const float LAMAX = 880;
    static const float adc_LAMIN = 14290;
    static const float adc_LAMAX = 30630;
    static const float gain = (LAMAX - LAMIN) / (adc_LAMAX - adc_LAMIN);
    static const float offset = -300;//LAMIN - (adc_LAMIN / gain);
    
    static int32_t adc_history[100];
    static int32_t idx_adc = 0;
    static int32_t avrg_adc = 0;
    
    static int32_t glide = 5;
    
    adc_history[idx_adc] = CV_in;
    idx_adc++;
    if (idx_adc > glide - 1)
        idx_adc = 0;
    
    avrg_adc = 0;
    
    for (int i = 0; i < glide; i++)
    {
        avrg_adc += adc_history[i];
    }
    
    CV_in = avrg_adc / glide;
    
    //	printf("ADC_val = %d\n", CV_in);
    
    note_freq = CV_in * gain + offset;
    //	note_freq = 55;
    
    //	printf("Freq = %d\n", (int)note_freq);
    
    if (note_freq < 30)
        note_freq = 30;
    if (note_freq > 20000)
        note_freq = 20000;
    
    scale_factor = note_freq / noScaled_freq;
    
    scaledPixel_nb = (uint32_t)((float)CIS_MAX_PIXELS_NB / scale_factor);
    
    // Sanity check
    if (scaledPixel_nb < 1)
        scale_factor = 1;
    if (scaledPixel_nb > CIS_MAX_PIXELS_NB)
        scaledPixel_nb = CIS_MAX_PIXELS_NB;
    
    if (shared_var.directRead_Mode == DUAL_READ)
    {
        for (idx = CIS_MAX_PIXELS_NB; --idx >= 0;)
        {
            imageData_ref_q31[idx] = imageData[idx];
        }
    }
    
    //imageData_ref_q31[CIS_PIXELS_NB];
    
    for (idx = scaledPixel_nb; --idx >= 0;)
    {
        imageBuffer_q31[idx] = 0;//(greyScale(imageData_ref_q31[(int32_t)(idx * scale_factor)]) - greyScale(imageData[(int32_t)(idx * scale_factor)])) * (16843 / 2); //16843 is factor to translate in a 32bit number
    }
    
    
    //	for (idx = scaledPixel_nb; --idx >= scaledPixel_nb / 2;)
    //	{
    //		imageBuffer_q31[idx] = 0x7FFFFFFF;
    //	}
    //	for (idx = scaledPixel_nb / 2; --idx >= 0;)
    //	{
    //		imageBuffer_q31[idx] = -0x7FFFFFFF;
    //	}
    
    // Fill audio buffer
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++)
    {
        // Check if current idx is below than a complete CIS line
        if (image_idx_UP < scaledPixel_nb)
        {
            signal_R = imageBuffer_q31[image_idx_UP];
            image_idx_UP++;
        }
        // Else fill the audio buffer with mirror CIS image
        else
        {
            if (shared_var.directRead_Mode == NORMAL_REVERSE_READ)
            {
                if (image_idx_DW > 1)
                {
                    image_idx_DW--;
                    signal_R = imageBuffer_q31[image_idx_DW];
                }
                // Restart counters
                else
                {
                    image_idx_UP = 0;
                    image_idx_DW = scaledPixel_nb - 1;
                }
            }
            else
            {
                image_idx_UP = 0;
                image_idx_DW = scaledPixel_nb - 1;
            }
        }
        
        // Buffer copies for right channels
        //		audioData[buff_idx * 2] = signal_R;
        //ToChangeaudioData[buff_idx * 2 + 1] = signal_R;
    }
    
    shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}
#pragma GCC pop_options

/**
 * @brief  Period elapsed callback in non blocking mode
 * @param  htim : TIM handle
 * @retval None
 */
//#pragma GCC push_options
//#pragma GCC optimize ("unroll-loops")
void synth_IfftMode(int32_t *imageData, float *audioData)
{
    static int32_t idx, acc, nbAcc;
    
    static int32_t signal_R;
    static int32_t signal_L;
    
    static int32_t new_idx;
    static int32_t buff_idx;
    static int32_t note;
    
    static int32_t imageBuffer_q31[NUMBER_OF_NOTES];
    static float imageBuffer_f32[NUMBER_OF_NOTES];
    
    static float waveBuffer[AUDIO_BUFFER_SIZE];
    static float ifftBuffer[AUDIO_BUFFER_SIZE];
    static float sumVolumeBuffer[AUDIO_BUFFER_SIZE];
    static float volumeBuffer[AUDIO_BUFFER_SIZE];
    static float maxVolumeBuffer[AUDIO_BUFFER_SIZE];
    static float tmpMaxVolumeBuffer[AUDIO_BUFFER_SIZE];
    
    fill_float(0, ifftBuffer, AUDIO_BUFFER_SIZE);
    fill_float(0, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
    fill_float(0, maxVolumeBuffer, AUDIO_BUFFER_SIZE);
    
    float tmp_audioData[AUDIO_BUFFER_SIZE];

    for (idx = 0; idx < NUMBER_OF_NOTES; idx++)
    {
        imageBuffer_q31[idx] = 0;
        for (acc = 0; acc < PIXELS_PER_NOTE; acc++)
        {
            imageBuffer_q31[idx] += (imageData[idx * PIXELS_PER_NOTE + acc]);
        }
#ifndef COLOR_INVERTED
        imageBuffer_q31[idx] /= PIXELS_PER_NOTE;
#else
        imageBuffer_q31[idx] /= PIXELS_PER_NOTE;
        imageBuffer_q31[idx] = 65535 - imageBuffer_q31[idx];
        if (imageBuffer_q31[idx] < 0)
        {
            imageBuffer_q31[idx] = 0;
        }
        if (imageBuffer_q31[idx] > VOLUME_AMP_RESOLUTION)
        {
            imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION;
        }
#endif
    }
    //correction bug
    imageBuffer_q31[0] = 0;
    
    //fill_int32(0, (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES);
    //imageBuffer_q31[200] = 65535;
    //imageBuffer_q31[40] = 65000;
    //imageBuffer_q31[800] = 65000;
    //imageBuffer_q31[3300] = 65000;
    
    //sub_int32(imageRef, (int32_t *)imageBuffer_q31, (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES);
    //clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION, NUMBER_OF_NOTES);
    
    //handle image / apply different algorithms
#ifdef RELATIVE_MODE
    //relative mode
    sub_int32((int32_t *)imageBuffer_q31, (int32_t *)&imageBuffer_q31[1], (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES - 1);
    clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION, NUMBER_OF_NOTES);
    imageBuffer_q31[NUMBER_OF_NOTES - 1] = 0;
#endif
    
    for (note = 0; note < (NUMBER_OF_NOTES); note++)
    {
        imageBuffer_f32[note] = (float)imageBuffer_q31[note];
        
        for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++)
        {
            //octave_coeff jump current pointer into the fundamental waveform, for example : the 3th octave increment the current pointer 8 per 8 (2^3)
            new_idx = (waves[note].current_idx + waves[note].octave_coeff);
            if (new_idx >= waves[note].area_size)
            {
                new_idx -= waves[note].area_size;
            }
            //fill buffer with current note waveform
            waveBuffer[buff_idx] = (*(waves[note].start_ptr + new_idx));

            waves[note].current_idx = new_idx;
        }
        
#ifdef GAP_LIMITER
        //gap limiter to minimize glitchs
        for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE - 1; buff_idx++)
        {
            if (waves[note].current_volume < imageBuffer_f32[note])
            {
                waves[note].current_volume += waves[note].volume_increment;
                if (waves[note].current_volume > imageBuffer_f32[note])
                {
                    waves[note].current_volume = imageBuffer_f32[note];
                    //fill buffer with current volume evolution
                    break;
                }
            }
            else
            {
                waves[note].current_volume -= waves[note].volume_decrement;
                if (waves[note].current_volume < imageBuffer_f32[note])
                {
                    waves[note].current_volume = imageBuffer_f32[note];
                    //fill buffer with current volume evolution
                    break;
                }
            }
            
            //fill buffer with current volume evolution
            volumeBuffer[buff_idx] = waves[note].current_volume;
        }
        
        //fill constant volume buffer
        if (buff_idx < AUDIO_BUFFER_SIZE)
        {
            fill_float(waves[note].current_volume, &volumeBuffer[buff_idx], AUDIO_BUFFER_SIZE - buff_idx);
        }
        
#else
        //		waves[note].current_volume = imageBuffer[note];
        fill_float(imageBuffer_f32[note], volumeBuffer, AUDIO_BUFFER_SIZE);
#endif
        
        //apply volume scaling at current note waveform
        mult_float(waveBuffer, volumeBuffer, waveBuffer, AUDIO_BUFFER_SIZE);
        
        for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;)
        {
            //store max volume
            if (volumeBuffer[buff_idx] > maxVolumeBuffer[buff_idx])
                maxVolumeBuffer[buff_idx] = volumeBuffer[buff_idx];
        }
        
        //ifft summation
        add_float(waveBuffer, ifftBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
        
        //volume summation
        add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
    }
    
    mult_float(ifftBuffer, maxVolumeBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
    scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);
    
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++)
    {
        if (sumVolumeBuffer[buff_idx] != 0)
            signal_R = (int32_t)(ifftBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
        else
            signal_R = 0;
        
        audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;
        tmp_audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;
    }
    
    shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}
//#pragma GCC pop_options

/**
 *
 *                   |------------------------------|------------------------------|
 *                   |half rfft buffer to audio buff|                              |
 * audio buffer      |------------FILL--------------|-------------PLAY-------------|
 *                   |                              |                              |
 *                   |                              |     fill half rfft buffer    |
 *                   |                              |                              |
 *                   |------------------------------|------------------------------|
 *                                                  ^
 *                                                HALF
 *                                              COMPLETE
 *
 *                   |------------------------------|------------------------------|
 *                   |                              |full rfft buffer to audio buff|
 * audio buffer      |-------------PLAY-------------|-------------FILL-------------|
 *                   |                              |                              |
 *                   |     fill full rfft buffer    |                              |
 *                   |                              |                              |
 *                   |------------------------------|------------------------------|
 *                                                                                 ^
 *                                                                                FULL
 *                                                                              COMPLETE
 */

static float phase = 0.0f;

// Fonction de traitement audio
// Synth process function
void synth_AudioProcess(uint8_t *buffer_R,
                        uint8_t *buffer_G,
                        uint8_t *buffer_B)
{
    int index = __atomic_load_n(&current_buffer_index, __ATOMIC_RELAXED);
    static int32_t g_grayScale[CIS_MAX_PIXELS_NB];

    // Attendre que le buffer destinataire soit libre
    pthread_mutex_lock(&buffers_R[index].mutex);
    while (buffers_R[index].ready != 0)
    {
        pthread_cond_wait(&buffers_R[index].cond, &buffers_R[index].mutex);
    }
    pthread_mutex_unlock(&buffers_R[index].mutex);
#if 1
    greyScale(buffer_R, buffer_G, buffer_B, g_grayScale,CIS_MAX_PIXELS_NB);

    synth_IfftMode(g_grayScale, buffers_R[index].data);  // Process synthesis
#endif
    
#if 0
    // Remplissage du buffer
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
    {
        buffers_R[index].data[i] = sinf(phase);
        phase += (TWO_PI * 440) / SAMPLING_FREQUENCY;

        // si on veut empêcher phase de devenir trop grand
        if (phase >= TWO_PI)
        {
            phase -= TWO_PI;
        }
    }
#endif
    
    // Marquer le buffer comme prêt
    pthread_mutex_lock(&buffers_R[index].mutex);
    buffers_R[index].ready = 1;
    pthread_mutex_unlock(&buffers_R[index].mutex);

    // Changer l'indice pour que le callback lise le buffer rempli et que le prochain écriture se fasse sur l'autre buffer
    __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}

