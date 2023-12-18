/* config.h */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/**************************************************************************************/
/********************            QSPI FLASH definitions            ********************/
/**************************************************************************************/

/**************************************************************************************/
/********************              debug definitions               ********************/
/**************************************************************************************/
//#define PRINT_IFFT_FREQUENCY
//#define PRINT_IFFT_FREQUENCY_FULL
//#define PRINT_CIS_CALIBRATION

/**************************************************************************************/
/******************              Ethernet definitions               *******************/
/**************************************************************************************/
#define UDP_HEADER_SIZE                          (1)//uint32
#define UDP_NB_PACKET_PER_LINE                   (6)
#define UDP_PACKET_SIZE                          (((CIS_PIXELS_NB) / UDP_NB_PACKET_PER_LINE) + (UDP_HEADER_SIZE))

#define PORT                                     (55151)    //The port on which to listen for incoming data

/**************************************************************************************/
/********************                  CV MODE                     ********************/
/**************************************************************************************/
#define IMAGE_WEIGHT                            (25)

/**************************************************************************************/
/********************              DAC definitions                 ********************/
/**************************************************************************************/
#define SAMPLING_FREQUENCY                        (44100)
#define AUDIO_CHANNEL                               (2)
#define AUDIO_BUFFER_SIZE                         (512)

/**************************************************************************************/
/***************************         CISdefinitions          *****************************/
/**************************************************************************************/
#ifdef CIS_400DPI
#define CIS_PIXELS_PER_LINE                        (1152)
#else
#define CIS_PIXELS_PER_LINE                        (576)
#endif

#define CIS_ADC_OUT_LINES                        (3)

#define CIS_PIXELS_NB                            ((CIS_PIXELS_PER_LINE * CIS_ADC_OUT_LINES))

/**************************************************************************************/
/********************              Synth definitions               ********************/
/**************************************************************************************/
#define SIN                                        //SIN-SAW-SQR

#define GAP_LIMITER

//#define STEREO_1
#define RELATIVE_MODE

#define PI 3.14159265358979323846

/**************************************************************************************/
/********************         Wave generation definitions          ********************/
/**************************************************************************************/

#define WAVE_AMP_RESOLUTION                     (16777215)        //in decimal
#define VOLUME_AMP_RESOLUTION                   (65535)           //in decimal
#define START_FREQUENCY                         (20)
#define MAX_OCTAVE_NUMBER                       (20)
#define SEMITONE_PER_OCTAVE                     (12)
#define COMMA_PER_SEMITONE                      (16)

#define VOLUME_INCREMENT                        (70)
#define VOLUME_DECREMENT                        (70)

#define PIXELS_PER_NOTE                         (1)
#define NUMBER_OF_NOTES                         (((CIS_PIXELS_PER_LINE) * (CIS_ADC_OUT_LINES)) / (PIXELS_PER_NOTE))


/**************************************************************************************/
/********************             Display definitions              ********************/
/**************************************************************************************/
#define WINDOWS_WIDTH                            (CIS_PIXELS_NB)
#define WINDOWS_HEIGHT                           (1160)

#endif // __CONFIG_H__

