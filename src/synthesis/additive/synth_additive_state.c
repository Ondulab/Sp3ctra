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
#include "../../config/config_instrument.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Global variables ----------------------------------------------------------*/

/* Synth Data Freeze Feature - Definitions */
volatile int g_is_synth_data_frozen = 0;
float *g_frozen_grayscale_buffer = NULL;  // Dynamic allocation
volatile int g_is_synth_data_fading_out = 0;
double g_synth_data_fade_start_time = 0.0;
const double G_SYNTH_DATA_FADE_DURATION_SECONDS = 5.0; // Corresponds to visual fade
pthread_mutex_t g_synth_data_freeze_mutex;

/* Buffers for display to reflect synth data (grayscale converted to RGB) - Definitions */
uint8_t *g_displayable_synth_R = NULL;  // Dynamic allocation
uint8_t *g_displayable_synth_G = NULL;  // Dynamic allocation
uint8_t *g_displayable_synth_B = NULL;  // Dynamic allocation
pthread_mutex_t g_displayable_synth_mutex;

/* Private function implementations ------------------------------------------*/

// Helper function to get current time in seconds
double synth_getCurrentTimeInSeconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts); // CLOCK_MONOTONIC is usually preferred for time differences
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void synth_data_freeze_init(void) {
  int nb_pixels = get_cis_pixels_nb();
  
  if (pthread_mutex_init(&g_synth_data_freeze_mutex, NULL) != 0) {
    perror("Failed to initialize synth data freeze mutex");
    // Handle error appropriately, e.g., exit or log
    return;
  }
  
  // Allocate frozen grayscale buffer
  g_frozen_grayscale_buffer = (float *)calloc(nb_pixels, sizeof(float));
  if (g_frozen_grayscale_buffer == NULL) {
    perror("Failed to allocate frozen grayscale buffer");
    pthread_mutex_destroy(&g_synth_data_freeze_mutex);
    return;
  }
}

void synth_data_freeze_cleanup(void) {
  pthread_mutex_destroy(&g_synth_data_freeze_mutex);
  
  // Free frozen grayscale buffer
  if (g_frozen_grayscale_buffer != NULL) {
    free(g_frozen_grayscale_buffer);
    g_frozen_grayscale_buffer = NULL;
  }
}

void displayable_synth_buffers_init(void) {
  int nb_pixels = get_cis_pixels_nb();
  
  if (pthread_mutex_init(&g_displayable_synth_mutex, NULL) != 0) {
    perror("Failed to initialize displayable synth data mutex");
    return;
  }
  
  // Allocate displayable RGB buffers
  g_displayable_synth_R = (uint8_t *)calloc(nb_pixels, sizeof(uint8_t));
  g_displayable_synth_G = (uint8_t *)calloc(nb_pixels, sizeof(uint8_t));
  g_displayable_synth_B = (uint8_t *)calloc(nb_pixels, sizeof(uint8_t));
  
  if (g_displayable_synth_R == NULL || g_displayable_synth_G == NULL || g_displayable_synth_B == NULL) {
    perror("Failed to allocate displayable synth RGB buffers");
    pthread_mutex_destroy(&g_displayable_synth_mutex);
    
    // Clean up any successful allocations
    if (g_displayable_synth_R != NULL) {
      free(g_displayable_synth_R);
      g_displayable_synth_R = NULL;
    }
    if (g_displayable_synth_G != NULL) {
      free(g_displayable_synth_G);
      g_displayable_synth_G = NULL;
    }
    if (g_displayable_synth_B != NULL) {
      free(g_displayable_synth_B);
      g_displayable_synth_B = NULL;
    }
    return;
  }
}

void displayable_synth_buffers_cleanup(void) {
  pthread_mutex_destroy(&g_displayable_synth_mutex);
  
  // Free displayable RGB buffers
  if (g_displayable_synth_R != NULL) {
    free(g_displayable_synth_R);
    g_displayable_synth_R = NULL;
  }
  if (g_displayable_synth_G != NULL) {
    free(g_displayable_synth_G);
    g_displayable_synth_G = NULL;
  }
  if (g_displayable_synth_B != NULL) {
    free(g_displayable_synth_B);
    g_displayable_synth_B = NULL;
  }
}
