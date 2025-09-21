/*
 * synth_additive_state.c
 *
 * State management for additive synthesis
 * Contains freeze/fade functionality and display buffer management
 *
 * Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "synth_additive_state.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Global variables ----------------------------------------------------------*/

/* Synth Data Freeze Feature - Definitions */
volatile int g_is_synth_data_frozen = 0;
int32_t g_frozen_grayscale_buffer[CIS_MAX_PIXELS_NB];
volatile int g_is_synth_data_fading_out = 0;
double g_synth_data_fade_start_time = 0.0;
const double G_SYNTH_DATA_FADE_DURATION_SECONDS = 5.0; // Corresponds to visual fade
pthread_mutex_t g_synth_data_freeze_mutex;

/* Buffers for display to reflect synth data (grayscale converted to RGB) - Definitions */
uint8_t g_displayable_synth_R[CIS_MAX_PIXELS_NB];
uint8_t g_displayable_synth_G[CIS_MAX_PIXELS_NB];
uint8_t g_displayable_synth_B[CIS_MAX_PIXELS_NB];
pthread_mutex_t g_displayable_synth_mutex;

/* Private function implementations ------------------------------------------*/

// Helper function to get current time in seconds
double synth_getCurrentTimeInSeconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts); // CLOCK_MONOTONIC is usually preferred for time differences
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void synth_data_freeze_init(void) {
  if (pthread_mutex_init(&g_synth_data_freeze_mutex, NULL) != 0) {
    perror("Failed to initialize synth data freeze mutex");
    // Handle error appropriately, e.g., exit or log
  }
  memset(g_frozen_grayscale_buffer, 0, sizeof(g_frozen_grayscale_buffer));
}

void synth_data_freeze_cleanup(void) {
  pthread_mutex_destroy(&g_synth_data_freeze_mutex);
}

void displayable_synth_buffers_init(void) {
  if (pthread_mutex_init(&g_displayable_synth_mutex, NULL) != 0) {
    perror("Failed to initialize displayable synth data mutex");
    // Handle error
  }
  memset(g_displayable_synth_R, 0, sizeof(g_displayable_synth_R));
  memset(g_displayable_synth_G, 0, sizeof(g_displayable_synth_G));
  memset(g_displayable_synth_B, 0, sizeof(g_displayable_synth_B));
}

void displayable_synth_buffers_cleanup(void) {
  pthread_mutex_destroy(&g_displayable_synth_mutex);
}
