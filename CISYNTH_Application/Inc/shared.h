/**
 ******************************************************************************
 * @file           : shared.h
 * @brief          : shared data structure for both cpu
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SHARED_H__
#define __SHARED_H__

/* Includes ------------------------------------------------------------------*/

/* Private includes ----------------------------------------------------------*/
#include "stdint.h"
#include "config.h"

/* Exported types ------------------------------------------------------------*/

typedef enum {
    IFFT_MODE = 0,
    DWAVE_MODE,
    MENU_MODE
}synthModeTypeDef;

typedef enum {
    CV_ON = 0,
    KEYBOARD_ON
}synthCVTypeDef;

typedef enum {
    NORMAL_READ = 0,
    NORMAL_REVERSE_READ,
    DUAL_READ
}synthReadModeTypeDef;

struct wave {
    volatile float *start_ptr;
    uint32_t current_idx;
    uint32_t area_size;
    uint32_t octave_coeff;
    uint32_t octave_divider;
    float current_volume;
    float volume_increment;
    float max_volume_increment;
    float volume_decrement;
    float max_volume_decrement;
    float frequency;
};

struct params {
    int32_t start_frequency;
    int32_t comma_per_semitone;
    int32_t ifft_attack;
    int32_t ifft_release;
    int32_t volume;
};

struct shared_var {
    synthModeTypeDef mode;
    synthCVTypeDef CV_or_Keyboard;
    synthReadModeTypeDef directRead_Mode;
    int32_t synth_process_cnt;
    //ToChangeSAI_HandleTypeDef haudio_out_sai;
};

/* Exported constants --------------------------------------------------------*/
#define WAVEFORM_TABLE_SIZE        (10000000)

extern struct shared_var shared_var;
extern volatile struct params params;
extern volatile int32_t cvData[];
extern volatile int32_t imageData[];
extern volatile int32_t audioBuff[];
extern volatile struct wave waves[NUMBER_OF_NOTES];
extern volatile float unitary_waveform[WAVEFORM_TABLE_SIZE];

extern int params_size;

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

#endif /*__SHARED_H__*/
