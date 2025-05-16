/*
 * synth_fft.c
 *
 *  Created on: 16 May 2025
 *      Author: Cline
 */

#include "synth_fft.h"
#include "config.h" // For SAMPLING_FREQUENCY, AUDIO_BUFFER_SIZE
#include "error.h"  // For die() or other error handling
#include <errno.h>  // For ETIMEDOUT
#include <math.h>   // For sinf, M_PI
#include <pthread.h>
#include <stdio.h>  // For printf (debugging)
#include <string.h> // For memset

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#define TWO_PI (2.0 * M_PI)

// Global variables defined in synth_fft.h
FftAudioDataBuffer fft_audio_buffers[2];
volatile int fft_current_buffer_index = 0;
pthread_mutex_t fft_buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

// Internal state for sine wave generation
static float sine_phase = 0.0f;
static float sine_phase_increment = 0.0f;

// Global flag to keep threads running (assuming it's defined elsewhere, e.g.
// main.c or context.h) If not, this thread will run indefinitely until the
// program exits. Consider passing a context or a running flag as an argument to
// synth_fftMode_thread_func
extern volatile int keepRunning; // Declaration, assuming it's defined globally

void synth_fftMode_init(void) {
  printf("Initializing synth_fftMode...\n");

  // Calculate phase increment for 440 Hz sine wave
  sine_phase_increment = TWO_PI * 440.0f / (float)SAMPLING_FREQUENCY;
  sine_phase = 0.0f;

  // Initialize mutex and condition variables for each buffer
  for (int i = 0; i < 2; ++i) {
    if (pthread_mutex_init(&fft_audio_buffers[i].mutex, NULL) != 0) {
      die("Failed to initialize FFT audio buffer mutex");
    }
    if (pthread_cond_init(&fft_audio_buffers[i].cond, NULL) != 0) {
      die("Failed to initialize FFT audio buffer condition variable");
    }
    fft_audio_buffers[i].ready = 0; // Mark as not ready initially
    memset(fft_audio_buffers[i].data, 0, AUDIO_BUFFER_SIZE * sizeof(float));
  }

  if (pthread_mutex_init(&fft_buffer_index_mutex, NULL) != 0) {
    die("Failed to initialize FFT buffer index mutex");
  }
  fft_current_buffer_index = 0;

  printf("synth_fftMode initialized.\n");
}

void synth_fftMode_process(float *audio_buffer, unsigned int buffer_size) {
  if (audio_buffer == NULL) {
    fprintf(stderr, "synth_fftMode_process: audio_buffer is NULL\n");
    return;
  }

  for (unsigned int i = 0; i < buffer_size; ++i) {
    audio_buffer[i] = 0.5f * sinf(sine_phase); // 0.5f amplitude
    sine_phase += sine_phase_increment;
    if (sine_phase >= TWO_PI) {
      sine_phase -= TWO_PI;
    }
  }
}

void *synth_fftMode_thread_func(void *arg) {
  (void)arg; // Mark arg as unused for now

  printf("synth_fftMode_thread_func started.\n");

  while (keepRunning) { // Use a global running flag or pass one via arg
    int local_producer_idx;

    // Get the current buffer to write to
    pthread_mutex_lock(&fft_buffer_index_mutex);
    local_producer_idx = fft_current_buffer_index;
    pthread_mutex_unlock(&fft_buffer_index_mutex);

    // Wait for the consumer (audio callback) to finish with this buffer
    pthread_mutex_lock(&fft_audio_buffers[local_producer_idx].mutex);
    while (fft_audio_buffers[local_producer_idx].ready == 1 && keepRunning) {
      // Buffer is still marked as ready by producer, meaning consumer hasn't
      // finished Or, it was just filled and consumer hasn't picked it up yet.
      // Wait for consumer to mark it not ready.
      // Adding a timeout to pthread_cond_wait can prevent deadlocks if
      // keepRunning changes.
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 1; // Wait for 1 second
      int wait_ret = pthread_cond_timedwait(
          &fft_audio_buffers[local_producer_idx].cond,
          &fft_audio_buffers[local_producer_idx].mutex, &ts);
      if (wait_ret == ETIMEDOUT && !keepRunning) {
        pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);
        goto cleanup_thread;
      }
    }
    // We have the lock, and buffer is not ready (or ready to be overwritten)

    // Generate audio data
    synth_fftMode_process(fft_audio_buffers[local_producer_idx].data,
                          AUDIO_BUFFER_SIZE);

    // Mark buffer as ready and signal consumer
    fft_audio_buffers[local_producer_idx].ready = 1;
    pthread_cond_signal(&fft_audio_buffers[local_producer_idx].cond);
    pthread_mutex_unlock(&fft_audio_buffers[local_producer_idx].mutex);

    // Switch to the other buffer for next iteration
    pthread_mutex_lock(&fft_buffer_index_mutex);
    fft_current_buffer_index = 1 - local_producer_idx;
    pthread_mutex_unlock(&fft_buffer_index_mutex);

    // Add a small sleep if CPU usage is too high, though for audio this might
    // introduce latency usleep(1000); // e.g., 1ms, adjust as needed or remove
  }

cleanup_thread:
  printf("synth_fftMode_thread_func stopping.\n");
  return NULL;
}
