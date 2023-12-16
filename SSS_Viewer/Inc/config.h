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
/********************             Display definitions              ********************/
/**************************************************************************************/
#define DISPLAY_REFRESH_FPS						(5)

#define WINDOWS_WIDTH                            (CIS_PIXELS_NB)
#define WINDOWS_HEIGHT                            (1160)

/**************************************************************************************/
/********************              Synth definitions               ********************/
/**************************************************************************************/
#define SIN										//SIN-SAW-SQR

#define GAP_LIMITER
#define IFFT_GAP_PER_LOOP_INCREASE				(1)
#define IFFT_GAP_PER_LOOP_DECREASE				(1)

#define NOISE_REDUCER							(2)
//#define STEREO_1
#define RELATIVE_MODE

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
#define IMAGE_WEIGHT							(25)

/**************************************************************************************/
/********************              DAC definitions                 ********************/
/**************************************************************************************/
#define SAMPLING_FREQUENCY				      	(48000)
#define AUDIO_BUFFER_SIZE             			(256)
#define FRAME_LENGHT							(AUDIO_BUFFER_SIZE)
#define ACTIVE_FRAME_LENGHT						((AUDIO_BUFFER_SIZE) / 2)
#define VOLUME									(70)

/**************************************************************************************/
/********************         Wave generation definitions          ********************/
/**************************************************************************************/
#define CIS_ACTIVE_PIXELS_PER_LINE				(576)
#define CIS_ADC_OUT_LINES						(3)

#define WAVE_AMP_RESOLUTION 					(16777215)   		//in decimal
#define VOLUME_AMP_RESOLUTION 					(65535)   		//in decimal
#define START_FREQUENCY     					(70)
#define MAX_OCTAVE_NUMBER   					(20)
#define SEMITONE_PER_OCTAVE 					(12)
#define COMMA_PER_SEMITONE  					(5)

#define CIS_PIXELS_NB							((CIS_ACTIVE_PIXELS_PER_LINE * CIS_ADC_OUT_LINES))
#define PIXELS_PER_NOTE							(8)
#define NUMBER_OF_NOTES     					(((CIS_ACTIVE_PIXELS_PER_LINE) * (CIS_ADC_OUT_LINES)) / (PIXELS_PER_NOTE))

/**************************************************************************************/
/***************************         CISdefinitions          *****************************/
/**************************************************************************************/

#ifdef CIS_400DPI
#define CIS_PIXELS_PER_LINE                        (1152)
#else
#define CIS_PIXELS_PER_LINE                        (576)
#endif

#define CIS_ADC_OUT_LINES                        (3)
#define CIS_PIXELS_NB                             (CIS_PIXELS_PER_LINE * CIS_ADC_OUT_LINES)

#define PIXELS_PER_NOTE                            (16)
#define NUMBER_OF_NOTES                         (((CIS_PIXELS_PER_LINE) * (CIS_ADC_OUT_LINES)) / (PIXELS_PER_NOTE))

#endif // __CONFIG_H__

