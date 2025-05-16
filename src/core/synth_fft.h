/*
 * synth_fft.h
 *
 *  Created on: 16 May 2025
 *      Author: Cline
 */

#ifndef SYNTH_FFT_H
#define SYNTH_FFT_H

#include "config.h"  // For AUDIO_BUFFER_SIZE, SAMPLING_FREQUENCY
#include <pthread.h> // For mutex and cond
#include <stdint.h>  // For uint32_t, etc.

/* Exported types ------------------------------------------------------------*/
typedef struct {
  float data[AUDIO_BUFFER_SIZE];
  volatile int ready; // 0 = not ready, 1 = ready for consumption
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} FftAudioDataBuffer;

/* Exported variables --------------------------------------------------------*/
extern FftAudioDataBuffer fft_audio_buffers[2]; // Double buffer for FFT synth
extern volatile int
    fft_current_buffer_index; // Index of the buffer to be filled by producer
extern pthread_mutex_t
    fft_buffer_index_mutex; // Mutex for fft_current_buffer_index

/* Exported functions prototypes ---------------------------------------------*/
void synth_fftMode_init(void);
void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size);
void *
synth_fftMode_thread_func(void *arg); // Renamed to avoid conflict if
                                      // synth_fftMode_thread is used elsewhere

#endif /* SYNTH_FFT_H */
