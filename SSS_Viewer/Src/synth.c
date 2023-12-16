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

#include "shared.h"
#include "synth.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static volatile int32_t* half_audio_ptr;
static volatile int32_t* full_audio_ptr;
static int32_t imageRef[NUMBER_OF_NOTES] = {0};

/* Variable used to get converted value */
//ToChange__IO uint16_t uhADCxConvertedValue = 0;

/* Private function prototypes -----------------------------------------------*/
static uint32_t greyScale(uint32_t rbg888);
static void synth_IfftMode(volatile int32_t *imageData, volatile int32_t *audioData);
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

/**
 * @brief  synth ifft init.
 * @param
 * @retval Error
 */
int32_t synth_IfftInit(void)
{
    //ToChangestatic DAC_ChannelConfTypeDef sConfig;
    
    //	printf("---------- SYNTH INIT ---------\n");
    //	printf("-------------------------------\n");
    
    printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);
    //	printf("Buffer lengh = %d uint16\n", (int)buffer_len);
    
    
    //	printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency, (int)waves[0].area_size);
    //	printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n", (int)waves[NUMBER_OF_NOTES - 1].frequency, (int)waves[NUMBER_OF_NOTES - 1].area_size / (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff), (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));
    
    //	printf("-------------------------------\n");
    
#ifdef PRINT_IFFT_FREQUENCY
    for (uint32_t note = 0; note < NUMBER_OF_NOTES; note++)
    {
        printf("FREQ = %0.2f, SIZE = %d, OCTAVE = %d\n", waves[note].frequency, (int)waves[note].area_size, (int)waves[note].octave_coeff);
#ifdef PRINT_IFFT_FREQUENCY_FULL
        uint16_t output = 0;
        for (uint32_t idx = 0; idx < (waves[note].area_size / waves[note].octave_coeff); idx++)
        {
            output = *(waves[note].start_ptr + (idx *  waves[note].octave_coeff));
            printf("%d\n", output);
        }
#endif
    }
    printf("-------------------------------\n");
#endif
    
    /* Initialize the DAC peripheral ######################################*/
    
    //#ToChange	audio_Init();
    //#ToChange	half_audio_ptr = audio_GetDataPtr(0);
    //#ToChange	full_audio_ptr = audio_GetDataPtr(AUDIO_BUFFER_SIZE * 2);
    
    //#ToChange	arm_fill_q31(65535, (int32_t *)imageRef, NUMBER_OF_NOTES);
    
    return 0;
}

/**
 * @brief  Get Image buffer data
 * @param  Index
 * @retval Value
 */
int32_t synth_GetImageData(uint32_t index)
{
    //	if (index >= RFFT_BUFFER_SIZE)
    //		Error_Handler();
    return imageData[index];
}

/**
 * @brief  Set Image buffer data
 * @param  Index
 * @retval Value
 */
int32_t synth_SetImageData(uint32_t index, int32_t value)
{
    //	if (index >= RFFT_BUFFER_SIZE)
    //		Error_Handler();
    imageData[index] = value;
    return 0;
}

uint32_t greyScale(uint32_t rbg888)
{
    static uint32_t grey, r, g, b;
    
    r = rbg888 			& 0xFF; // ___________XXXXX
    g = (rbg888 >> 8) 	& 0xFF; // _____XXXXXX_____
    b = (rbg888 >> 12) 	& 0xFF; // XXXXX___________
    
    return grey = (r * 299 + g * 587 + b * 114);
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
    static int32_t imageBuffer_q31[CIS_PIXELS_NB+1];
    static int32_t imageData_ref_q31[CIS_PIXELS_NB+1];
    static const float noScaled_freq = (SAMPLING_FREQUENCY) / CIS_PIXELS_NB; // 27,7 Hz;
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
    
    scaledPixel_nb = (uint32_t)((float)CIS_PIXELS_NB / scale_factor);
    
    // Sanity check
    if (scaledPixel_nb < 1)
        scale_factor = 1;
    if (scaledPixel_nb > CIS_PIXELS_NB)
        scaledPixel_nb = CIS_PIXELS_NB;
    
    if (shared_var.directRead_Mode == DUAL_READ)
    {
        for (idx = CIS_PIXELS_NB; --idx >= 0;)
        {
            imageData_ref_q31[idx] = imageData[idx];
        }
    }
    
    //imageData_ref_q31[CIS_PIXELS_NB];
    
    for (idx = scaledPixel_nb; --idx >= 0;)
    {
        imageBuffer_q31[idx] = (greyScale(imageData_ref_q31[(int32_t)(idx * scale_factor)]) - greyScale(imageData[(int32_t)(idx * scale_factor)])) * (16843 / 2); //16843 is factor to translate in a 32bit number
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
        audioData[buff_idx * 2 + 1] = signal_R;
    }
    
    shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}
#pragma GCC pop_options

/**
 * @brief  Period elapsed callback in non blocking mode
 * @param  htim : TIM handle
 * @retval None
 */
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
void synth_IfftMode(volatile int32_t *imageData, volatile int32_t *audioData)
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
    
    for (idx = NUMBER_OF_NOTES; --idx >= 0;)
    {
        imageBuffer_q31[idx] = 0;
        nbAcc = 0;
        for (acc = 6; --acc >= 4;)
        {
            nbAcc++;
            imageBuffer_q31[idx] += greyScale(imageData[(idx * PIXELS_PER_NOTE + acc)]) >> 2;
        }
        imageBuffer_q31[idx] /= nbAcc;
    }
    
    sub_int32(imageRef, (int32_t *)imageBuffer_q31, (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES);
    clip_int32((int32_t *)imageBuffer_q31, 0, 65535, NUMBER_OF_NOTES);
    
    //handle image / apply different algorithms
#ifdef RELATIVE_MODE
    //relative mode
    sub_int32((int32_t *)imageBuffer_q31, (int32_t *)&imageBuffer_q31[1], (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES - 1);
    clip_int32((int32_t *)imageBuffer_q31, 0, 65535, NUMBER_OF_NOTES);
#endif
    
    for (note = 0; note < NUMBER_OF_NOTES; note++)
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
            if (waves[note].octave_divider == 2)
                waveBuffer[++buff_idx] = (*(waves[note].start_ptr + new_idx));
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
        fill_float(imageBuffer[note], volumeBuffer, AUDIO_BUFFER_SIZE);
#endif
        
        //apply volume scaling at current note waveform
        mult_float(waveBuffer, volumeBuffer, waveBuffer, AUDIO_BUFFER_SIZE);
        
        for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;)
        {
            //store max volume
            if (volumeBuffer[buff_idx] > maxVolumeBuffer[buff_idx])
                maxVolumeBuffer[buff_idx] = volumeBuffer[buff_idx];
        }
        
        //		arm_sub_f32(volumeBuffer, maxVolumeBuffer, tmpMaxVolumeBuffer, AUDIO_BUFFER_SIZE);
        //		arm_clip_f32(tmpMaxVolumeBuffer, tmpMaxVolumeBuffer, 0, 65535, AUDIO_BUFFER_SIZE);
        //		arm_add_f32(maxVolumeBuffer, tmpMaxVolumeBuffer, maxVolumeBuffer, AUDIO_BUFFER_SIZE);
        
        
        //ifft summation
        add_float(waveBuffer, ifftBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
        
        //volume summation
        add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
    }
    
    mult_float(ifftBuffer, maxVolumeBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
    scale_float(sumVolumeBuffer, 256, AUDIO_BUFFER_SIZE);
    
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++)
    {
        if (sumVolumeBuffer[buff_idx] != 0)
            signal_R = (int32_t)(ifftBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
        else
            signal_R = 0;
        
        //		audioData[buff_idx * 2] = signal_R;
        audioData[buff_idx * 2 + 1] = signal_R;
    }
    
    shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}
#pragma GCC pop_options

/**
 * @brief  Manages Audio process.
 * @param  None
 * @retval Audio error
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
void synth_AudioProcess(void)
{
    /* 1st half buffer played; so fill it and continue playing from bottom*/
    if(0);//ToChange*audio_GetBufferState() == AUDIO_BUFFER_OFFSET_HALF)
    {
        //ToChangeaudio_ResetBufferState();
        //ToChangeudp_serverReceiveImage(imageData);
        if (shared_var.mode == IFFT_MODE)
            synth_IfftMode(imageData, half_audio_ptr);
        //if (shared_var.mode == DWAVE_MODE)
            //ToChangesynth_DirectMode(imageData, half_audio_ptr, uhADCxConvertedValue);
        
        //SCB_CleanDCache_by_Addr((uint32_t *)half_audio_ptr, AUDIO_BUFFER_SIZE * 8);
        //else
        //{
            //SCB_InvalidateDCache_by_Addr((uint32_t *)unitary_waveform, WAVEFORM_TABLE_SIZE * 2);
        //}
    }
    
    /* 2nd half buffer played; so fill it and continue playing from top */
    if(0);//ToChange*audio_GetBufferState() == AUDIO_BUFFER_OFFSET_FULL)
    {
        //ToChangeaudio_ResetBufferState();
        //ToChangeudp_serverReceiveImage(imageData);
        /*CM7 try to take the HW sempahore 0*/
        if (shared_var.mode == IFFT_MODE)
            synth_IfftMode(imageData, full_audio_ptr);
        //if (shared_var.mode == DWAVE_MODE)
            //ToChangesynth_DirectMode(imageData, full_audio_ptr, uhADCxConvertedValue);
        
        //SCB_CleanDCache_by_Addr((uint32_t *)full_audio_ptr, AUDIO_BUFFER_SIZE * 8);
        //else
        //{
            //SCB_InvalidateDCache_by_Addr((uint32_t *)unitary_waveform, WAVEFORM_TABLE_SIZE * 2);
            //SCB_InvalidateDCache_by_Addr((uint32_t *)waves, sizeof(waves));
        //}
    }
    
#ifdef CV
    
    static uint32_t cnt = 0;
    static uint32_t last_TRG_state = 0;
    static uint32_t last_RST_state = 0;
    static int32_t i = 0;
    uint32_t tmp_accumulation = 0;
    
    if (HAL_GPIO_ReadPin(ARD_D2_GPIO_Port, ARD_D2_Pin) == FALSE)
    {
        if (last_RST_state == TRUE)
            cnt = 0;
        last_RST_state = FALSE;
    }
    else
    {
        last_RST_state = TRUE;
    }
    if (HAL_GPIO_ReadPin(ARD_D7_GPIO_Port, ARD_D7_Pin) == TRUE)
    {
        if (last_TRG_state != TRUE)
            cnt += IMAGE_WEIGHT;
        
        last_TRG_state = TRUE;
    }
    else
    {
        last_TRG_state = FALSE;
    }
    
    if (cnt >= (NUMBER_OF_NOTES))
        cnt = 0;
    
    /*##-5- Set DAC channel1 DHR12RD register ################################################*/
    
    for (i = 0; i < IMAGE_WEIGHT; i++)
    {
        if ((cnt + i) < NUMBER_OF_NOTES)
            tmp_accumulation += (imageData[cnt + i] >> 4);
    }
    
    tmp_accumulation /= i;
    cvData[cnt / IMAGE_WEIGHT - 1] = tmp_accumulation;
    
    if (HAL_DAC_SetValue(&hdac1, DAC1_CHANNEL_1, DAC_ALIGN_12B_R, tmp_accumulation) != HAL_OK)
    {
        /* Setting value Error */
        Error_Handler();
    }
#endif
}

/**
 * @brief  Conversion complete callback in non blocking mode
 * @param  AdcHandle : AdcHandle handle
 * @note   This example shows a simple way to report end of conversion, and
 *         you can add your own implementation.
 * @retval None
 */
void HAL_ADC_ConvCpltCallback()//ToChangeADC_HandleTypeDef* AdcHandle)
{
    /* Get the converted value of regular channel */
    //ToChangeuhADCxConvertedValue = HAL_ADC_GetValue(AdcHandle);
}

