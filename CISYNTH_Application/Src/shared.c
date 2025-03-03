/**
 ******************************************************************************
 * @file           : shared.c
 * @brief          : shared data structure for both cpu
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "shared.h"
#include "config.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

struct shared_var shared_var;

volatile int32_t cvData[NUMBER_OF_NOTES / IMAGE_WEIGHT];

volatile int32_t imageData[CIS_MAX_PIXELS_NB];

volatile int32_t audioBuff[AUDIO_BUFFER_SIZE * 4];

volatile struct wave waves[NUMBER_OF_NOTES];

volatile float unitary_waveform[WAVEFORM_TABLE_SIZE];

/* Private function prototypes -----------------------------------------------*/

/* Private user code ---------------------------------------------------------*/
