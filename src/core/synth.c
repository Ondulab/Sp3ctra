/*
 * synth.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

/* Includes ------------------------------------------------------------------*/
#include "config.h"

#include "stdio.h"
#include "stdlib.h"
#include <string.h> // For memset, memcpy
/* Commenté pour éviter l'erreur avec cblas.h */
/* #include <Accelerate/Accelerate.h> */
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_c_api.h"
#include "error.h"
#include "shared.h"
#include "synth.h"
#include "wave_generation.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Synth Data Freeze Feature - Definitions */
volatile int g_is_synth_data_frozen = 0;
int32_t g_frozen_grayscale_buffer[CIS_MAX_PIXELS_NB];
volatile int g_is_synth_data_fading_out = 0;
double g_synth_data_fade_start_time = 0.0;
const double G_SYNTH_DATA_FADE_DURATION_SECONDS =
    5.0; // Corresponds to visual fade
pthread_mutex_t g_synth_data_freeze_mutex;

// Helper function to get current time in seconds
static double synth_getCurrentTimeInSeconds() {
  struct timespec ts;
  clock_gettime(
      CLOCK_MONOTONIC,
      &ts); // CLOCK_MONOTONIC is usually preferred for time differences
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

/* Buffers for display to reflect synth data (grayscale converted to RGB) -
 * Definitions */
uint8_t g_displayable_synth_R[CIS_MAX_PIXELS_NB];
uint8_t g_displayable_synth_G[CIS_MAX_PIXELS_NB];
uint8_t g_displayable_synth_B[CIS_MAX_PIXELS_NB];
pthread_mutex_t g_displayable_synth_mutex;

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
/* End Synth Data Freeze Feature */

/* Private variables ---------------------------------------------------------*/

// Variables pour la limitation des logs (affichage périodique)
static uint32_t log_counter = 0;
#define LOG_FREQUENCY                                                          \
  (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE) // Environ 1 seconde

// static volatile int32_t *half_audio_ptr; // Unused variable
// static volatile int32_t *full_audio_ptr; // Unused variable
static int32_t imageRef[NUMBER_OF_NOTES] = {0};

/* Variable used to get converted value */
// ToChange__IO uint16_t uhADCxConvertedValue = 0;

/* Private function prototypes -----------------------------------------------*/
static uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G,
                          uint8_t *buffer_B, int32_t *gray, uint32_t size);
void synth_IfftMode(int32_t *imageData, float *audioData);

static float calculate_contrast(int32_t *imageData, size_t size);

/* Private user code ---------------------------------------------------------*/

void sub_int32(const int32_t *a, const int32_t *b, int32_t *result,
               size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] - b[i];
  }
}

void clip_int32(int32_t *array, int32_t min, int32_t max, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (array[i] < min) {
      array[i] = min;
    } else if (array[i] > max) {
      array[i] = max;
    }
  }
}

void mult_float(const float *a, const float *b, float *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] * b[i];
  }
}

void add_float(const float *a, const float *b, float *result, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    result[i] = a[i] + b[i];
  }
}

void scale_float(float *array, float scale, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    array[i] *= scale;
  }
}

void fill_float(float value, float *array, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

void fill_int32(int32_t value, int32_t *array, size_t length) {
  if (array == NULL) {
    return; // Gestion d'erreur si le tableau est NULL
  }

  for (size_t i = 0; i < length; ++i) {
    array[i] = value;
  }
}

int32_t synth_IfftInit(void) {
  // ToChangestatic DAC_ChannelConfTypeDef sConfig;

  int32_t buffer_len = 0;

  printf("---------- SYNTH INIT ---------\n");
  printf("-------------------------------\n");

  // initialize default parameters
  wavesGeneratorParams.commaPerSemitone = COMMA_PER_SEMITONE;
  wavesGeneratorParams.startFrequency =
      (uint32_t)START_FREQUENCY; // Cast to uint32_t
  wavesGeneratorParams.harmonization = MAJOR;
  wavesGeneratorParams.harmonizationLevel = 100;
  wavesGeneratorParams.waveform = SIN_WAVE;
  wavesGeneratorParams.waveformOrder = 1;

  buffer_len = init_waves(unitary_waveform, waves,
                          &wavesGeneratorParams); // 24002070 24000C30

  int32_t value = VOLUME_INCREMENT;

  if (value == 0)
    value = 0;
  if (value > 1000)
    value = 100;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_increment =
        1.00 / (float)value * waves[note].max_volume_increment;
  }

  value = VOLUME_DECREMENT;

  if (value == 0)
    value = 0;
  if (value > 1000)
    value = 100;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_decrement =
        1.00 / (float)value * waves[note].max_volume_decrement;
  }

  // start with random index
  for (uint32_t i = 0; i < NUMBER_OF_NOTES; i++) {
#ifdef __APPLE__
    uint32_t aRandom32bit = arc4random();
#else
    // Use standard random function on Linux
    uint32_t aRandom32bit = rand();
#endif
    waves[i].current_idx = aRandom32bit % waves[i].area_size;
    waves[i].current_volume = 0;
  }

  if (buffer_len > (2400000 - 1)) {
    printf("RAM overflow");
    die("synth init failed");
    return -1;
  }

  printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);
  printf("Buffer lengh = %d uint16\n", (int)buffer_len);

  uint8_t FreqStr[256] = {0};
  sprintf((char *)FreqStr, " %d -> %dHz      Octave:%d",
          (int)waves[0].frequency, (int)waves[NUMBER_OF_NOTES - 1].frequency,
          (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

  printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency,
         (int)waves[0].area_size);
  printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n",
         (int)waves[NUMBER_OF_NOTES - 1].frequency,
         (int)waves[NUMBER_OF_NOTES - 1].area_size /
             (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff),
         (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

  printf("-------------------------------\n");

#ifdef PRINT_IFFT_FREQUENCY
  for (uint32_t pix = 0; pix < NUMBER_OF_NOTES; pix++) {
    printf("FREQ = %0.2f, SIZE = %d, OCTAVE = %d\n", waves[pix].frequency,
           (int)waves[pix].area_size, (int)waves[pix].octave_coeff);
#ifdef PRINT_IFFT_FREQUENCY_FULL
    int32_t output = 0;
    for (uint32_t idx = 0;
         idx < (waves[pix].area_size / waves[pix].octave_coeff); idx++) {
      output = *(waves[pix].start_ptr + (idx * waves[pix].octave_coeff));
      printf("%d\n", output);
    }
#endif
  }
  printf("-------------------------------\n");
  printf("Buffer lengh = %d uint16\n", (int)buffer_len);

  printf("First note Freq = %dHz\nSize = %d\n", (int)waves[0].frequency,
         (int)waves[0].area_size);
  printf("Last  note Freq = %dHz\nSize = %d\nOctave = %d\n",
         (int)waves[NUMBER_OF_NOTES - 1].frequency,
         (int)waves[NUMBER_OF_NOTES - 1].area_size /
             (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff),
         (int)sqrt(waves[NUMBER_OF_NOTES - 1].octave_coeff));

  printf("-------------------------------\n");
#endif

  printf("Note number  = %d\n", (int)NUMBER_OF_NOTES);

  fill_int32(65535, (int32_t *)imageRef, NUMBER_OF_NOTES);

  return 0;
}

uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B,
                   int32_t *gray, uint32_t size) {
  uint32_t i = 0;

  for (i = 0; i < size; i++) {
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
 * Calcule le contraste d'une image en mesurant la variance des valeurs de
 * pixels Optimisé pour performance avec échantillonnage Retourne une valeur
 * entre 0.2 (faible contraste) et 1.0 (fort contraste)
 */
static float calculate_contrast(int32_t *imageData, size_t size) {
  // Protection contre les entrées invalides
  if (imageData == NULL || size == 0) {
    printf("ERREUR: Données d'image invalides dans calculate_contrast\n");
    return 1.0f; // Valeur par défaut = volume maximum
  }

  // Échantillonnage - ne traite pas tous les pixels pour optimiser performance
  const size_t sample_stride =
      (size_t)CONTRAST_STRIDE > 0 ? (size_t)CONTRAST_STRIDE : 1;
  const size_t sample_count = size / sample_stride;

  if (sample_count == 0) {
    printf("ERREUR: Aucun échantillon valide dans calculate_contrast\n");
    return 1.0f; // Valeur par défaut = volume maximum
  }

  // Calcul de la moyenne et de la variance en une seule passe
  float sum = 0.0f;
  float sum_sq = 0.0f;
  size_t valid_samples = 0;

  for (size_t i = 0; i < size; i += sample_stride) {
    float val = (float)imageData[i];
    // Protection contre les valeurs invalides (version robuste sans
    // isnan/isinf)
    if (val != val ||
        val * 0.0f != 0.0f) // équivalent à isnan(val) || isinf(val)
      continue;

    sum += val;
    sum_sq += val * val;
    valid_samples++;
  }

  // Protection contre aucun échantillon valide
  if (valid_samples == 0) {
    printf("ERREUR: Aucun échantillon valide dans calculate_contrast\n");
    return 1.0f; // Valeur par défaut = volume maximum
  }

  // Calcul statistique
  float mean = sum / valid_samples;

  // Calcul de variance avec protection contre les erreurs d'arrondi
  float raw_variance = (sum_sq / valid_samples) - (mean * mean);
  float variance = raw_variance > 0.0f ? raw_variance : 0.0f;

  // Normalisation avec seuils min-max pour stabilité
  float max_possible_variance =
      ((float)VOLUME_AMP_RESOLUTION * (float)VOLUME_AMP_RESOLUTION) / 4.0f;

  if (max_possible_variance <= 0.0f) {
    printf("ERREUR: Variance maximale invalide dans calculate_contrast\n");
    return 1.0f; // Valeur par défaut = volume maximum
  }

  float contrast_ratio = sqrtf(variance) / sqrtf(max_possible_variance);

  // Protection contre NaN et infinité (version robuste sans isnan/isinf)
  if (contrast_ratio != contrast_ratio || contrast_ratio * 0.0f != 0.0f) {
    printf("ERREUR: Ratio de contraste invalide: %f / %f = %f\n",
           sqrtf(variance), sqrtf(max_possible_variance), contrast_ratio);
    return 1.0f; // Valeur par défaut = volume maximum
  }

  // Application d'une courbe de réponse pour meilleure perception
  float adjusted_contrast = powf(contrast_ratio, CONTRAST_ADJUSTMENT_POWER);

  // Limiter entre valeur min et 1.0 (maximum)
  float result = CONTRAST_MIN + (1.0f - CONTRAST_MIN) * adjusted_contrast;
  if (result > 1.0f)
    result = 1.0f;
  if (result < CONTRAST_MIN)
    result = CONTRAST_MIN;

  // Logs limités pour améliorer les performances
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("Contraste calculé: mean=%.2f, variance=%.2f, result=%.2f\n",
    // mean,
    //        variance, result); // Supprimé ou commenté
  }

  // Afficher les valeurs min et max de imageData pour le diagnostic
  int32_t min_image_value = imageData[0];
  int32_t max_image_value = imageData[0];
  for (size_t i = 1; i < size; i++) {
    if (imageData[i] < min_image_value) {
      min_image_value = imageData[i];
    }
    if (imageData[i] > max_image_value) {
      max_image_value = imageData[i];
    }
  }
  // printf("Image data: min=%d, max=%d\n", min_image_value, max_image_value);
  // // Supprimé ou commenté

  return result;
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @param  htim : TIM handle
 * @retval None
 */
// #pragma GCC push_options
// #pragma GCC optimize ("unroll-loops")
void synth_IfftMode(
    int32_t *imageData,
    float *audioData) { // imageData is now potentially frozen/faded g_grayScale
  // Mode IFFT (logs limités)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== IFFT Mode appelé =====\n"); // Supprimé ou commenté
  }
  static int32_t idx, acc; // nbAcc is unused
  static int32_t signal_R; // Restored as it's used locally
  // static int32_t signal_L; // Unused variable
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
  // static float tmpMaxVolumeBuffer[AUDIO_BUFFER_SIZE]; // Unused variable

  fill_float(0, ifftBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, maxVolumeBuffer, AUDIO_BUFFER_SIZE);

  float tmp_audioData[AUDIO_BUFFER_SIZE];

  for (idx = 0; idx < NUMBER_OF_NOTES; idx++) {
    imageBuffer_q31[idx] = 0;
    for (acc = 0; acc < PIXELS_PER_NOTE; acc++) {
      imageBuffer_q31[idx] += (imageData[idx * PIXELS_PER_NOTE + acc]);
    }
#ifndef COLOR_INVERTED
    imageBuffer_q31[idx] /= PIXELS_PER_NOTE;
#else
    imageBuffer_q31[idx] /= PIXELS_PER_NOTE;
    imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[idx];
    if (imageBuffer_q31[idx] < 0) {
      imageBuffer_q31[idx] = 0;
    }
    if (imageBuffer_q31[idx] > VOLUME_AMP_RESOLUTION) {
      imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION;
    }
#endif
  }
  // Correction bug
  imageBuffer_q31[0] = 0;

#ifdef RELATIVE_MODE
  sub_int32((int32_t *)imageBuffer_q31, (int32_t *)&imageBuffer_q31[1],
            (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES - 1);
  clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION,
             NUMBER_OF_NOTES);
  imageBuffer_q31[NUMBER_OF_NOTES - 1] = 0;
#endif

  for (note = 0; note < NUMBER_OF_NOTES; note++) {
    imageBuffer_f32[note] = (float)imageBuffer_q31[note];

#if ENABLE_NON_LINEAR_MAPPING
    {
      float normalizedIntensity =
          imageBuffer_f32[note] / (float)VOLUME_AMP_RESOLUTION;
      float gamma = GAMMA_VALUE; // Gamma value, adjustable as needed
      normalizedIntensity = powf(normalizedIntensity, gamma);
      imageBuffer_f32[note] = normalizedIntensity * VOLUME_AMP_RESOLUTION;
    }
#endif

    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      new_idx = (waves[note].current_idx + waves[note].octave_coeff);
      if ((uint32_t)new_idx >=
          waves[note].area_size) { // Cast new_idx to uint32_t for comparison
        new_idx -= waves[note].area_size;
      }
      // Fill buffer with current note waveform
      waveBuffer[buff_idx] = (*(waves[note].start_ptr + new_idx));
      waves[note].current_idx = new_idx;
    }

#ifdef GAP_LIMITER
    // Gap limiter to minimize glitches
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE - 1; buff_idx++) {
      if (waves[note].current_volume < imageBuffer_f32[note]) {
        waves[note].current_volume += waves[note].volume_increment;
        if (waves[note].current_volume > imageBuffer_f32[note]) {
          waves[note].current_volume = imageBuffer_f32[note];
          break;
        }
      } else {
        waves[note].current_volume -= waves[note].volume_decrement;
        if (waves[note].current_volume < imageBuffer_f32[note]) {
          waves[note].current_volume = imageBuffer_f32[note];
          break;
        }
      }
      volumeBuffer[buff_idx] = waves[note].current_volume;
    }
    // Fill constant volume buffer
    if (buff_idx < AUDIO_BUFFER_SIZE) {
      fill_float(waves[note].current_volume, &volumeBuffer[buff_idx],
                 AUDIO_BUFFER_SIZE - buff_idx);
    }
#else
    fill_float(imageBuffer_f32[note], volumeBuffer, AUDIO_BUFFER_SIZE);
#endif

    // Apply volume scaling to the current note waveform
    mult_float(waveBuffer, volumeBuffer, waveBuffer, AUDIO_BUFFER_SIZE);

    for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;) {
      if (volumeBuffer[buff_idx] > maxVolumeBuffer[buff_idx]) {
        maxVolumeBuffer[buff_idx] = volumeBuffer[buff_idx];
      }
    }

    // IFFT summation
    add_float(waveBuffer, ifftBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
    // Volume summation
    add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer,
              AUDIO_BUFFER_SIZE);
  }

  mult_float(ifftBuffer, maxVolumeBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
  scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);

  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    if (sumVolumeBuffer[buff_idx] != 0) {
      signal_R = (int32_t)(ifftBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
    } else {
      signal_R = 0;
    }

    // Stocker la valeur originale
    tmp_audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;
  }

  // Calculer le facteur de contraste basé sur l'image
  float contrast_factor = calculate_contrast(imageData, CIS_MAX_PIXELS_NB);
  // Apply contrast modulation
  float min_level = 0.0f, max_level = 0.0f;
  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    // Apply contrast factor
    // if (contrast_factor < 0.0f) {
    //  audioData[buff_idx] = 0.0f;
    //} else {
    audioData[buff_idx] = tmp_audioData[buff_idx] * contrast_factor;
    //}

    // Track min/max for debug
    if (buff_idx == 0 || audioData[buff_idx] < min_level)
      min_level = audioData[buff_idx];
    if (buff_idx == 0 || audioData[buff_idx] > max_level)
      max_level = audioData[buff_idx];
  }

  // Audio output prêt
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("Audio output: min=%.6f, max=%.6f, contrast=%.2f\n", min_level,
    //        max_level, contrast_factor); // Supprimé ou commenté
  }

  // Incrémenter le compteur global pour la limitation des logs
  log_counter++;

  shared_var.synth_process_cnt += AUDIO_BUFFER_SIZE;
}
// #pragma GCC pop_options

// Fonction de traitement audio
// Synth process function
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G,
                        uint8_t *buffer_B) {
  // Traitement audio (logs limités)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Audio Process appelé =====\n"); // Supprimé ou commenté
  }

  // Vérifier que les buffers d'entrée ne sont pas NULL
  if (!buffer_R || !buffer_G || !buffer_B) {
    printf("ERREUR: Un des buffers d'entrée est NULL!\n");
    return;
  }
  int index = __atomic_load_n(&current_buffer_index, __ATOMIC_RELAXED);
  static int32_t
      g_grayScale_live[CIS_MAX_PIXELS_NB]; // Buffer for live grayscale data
  int32_t processed_grayScale[CIS_MAX_PIXELS_NB]; // Buffer for data to be
                                                  // passed to synth_IfftMode

  // Attendre que le buffer destinataire soit libre
  pthread_mutex_lock(&buffers_R[index].mutex);
  while (buffers_R[index].ready != 0) {
    pthread_cond_wait(&buffers_R[index].cond, &buffers_R[index].mutex);
  }
  pthread_mutex_unlock(&buffers_R[index].mutex);

#if 1
  // On lance la conversion en niveaux de gris
  greyScale(buffer_R, buffer_G, buffer_B, g_grayScale_live, CIS_MAX_PIXELS_NB);

  // --- Synth Data Freeze/Fade Logic ---
  pthread_mutex_lock(&g_synth_data_freeze_mutex);
  int local_is_frozen = g_is_synth_data_frozen;
  int local_is_fading = g_is_synth_data_fading_out;

  static int prev_frozen_state_synth = 0;
  if (local_is_frozen && !prev_frozen_state_synth && !local_is_fading) {
    memcpy(g_frozen_grayscale_buffer, g_grayScale_live,
           sizeof(g_grayScale_live));
  }
  prev_frozen_state_synth = local_is_frozen;

  static int prev_fading_state_synth = 0;
  if (local_is_fading && !prev_fading_state_synth) {
    g_synth_data_fade_start_time = synth_getCurrentTimeInSeconds();
  }
  prev_fading_state_synth = local_is_fading;
  pthread_mutex_unlock(&g_synth_data_freeze_mutex);

  float alpha_blend = 1.0f; // For cross-fade

  if (local_is_fading) {
    double elapsed_time =
        synth_getCurrentTimeInSeconds() - g_synth_data_fade_start_time;
    if (elapsed_time >= G_SYNTH_DATA_FADE_DURATION_SECONDS) {
      pthread_mutex_lock(&g_synth_data_freeze_mutex);
      g_is_synth_data_fading_out = 0;
      g_is_synth_data_frozen = 0;
      pthread_mutex_unlock(&g_synth_data_freeze_mutex);
      memcpy(processed_grayScale, g_grayScale_live,
             sizeof(g_grayScale_live)); // Use live data
    } else {
      alpha_blend =
          (float)(elapsed_time /
                  G_SYNTH_DATA_FADE_DURATION_SECONDS); // Alpha from 0 (frozen)
                                                       // to 1 (live)
      alpha_blend = (alpha_blend < 0.0f)
                        ? 0.0f
                        : ((alpha_blend > 1.0f) ? 1.0f : alpha_blend);
      for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
        processed_grayScale[i] =
            (int32_t)(g_frozen_grayscale_buffer[i] * (1.0f - alpha_blend) +
                      g_grayScale_live[i] * alpha_blend);
      }
    }
  } else if (local_is_frozen) {
    memcpy(processed_grayScale, g_frozen_grayscale_buffer,
           sizeof(g_frozen_grayscale_buffer)); // Use frozen data
  } else {
    memcpy(processed_grayScale, g_grayScale_live,
           sizeof(g_grayScale_live)); // Use live data
  }
  // --- End Synth Data Freeze/Fade Logic ---

  // Lancer la synthèse avec les données potentiellement gelées/fondues
  synth_IfftMode(processed_grayScale,
                 buffers_R[index].data); // Process synthesis

  // Mettre à jour les buffers d'affichage globaux avec les données traitées
  // (processed_grayScale) Convertir processed_grayScale (int32_t, 0-65535) en
  // R,G,B (uint8_t, 0-255)
  pthread_mutex_lock(&g_displayable_synth_mutex);
  for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
    // Normaliser la valeur grayscale (0-65535) vers 0-255 pour uint8_t
    // La valeur max de gray[i] dans greyScale est 65535.
    uint8_t val_8bit = (uint8_t)(processed_grayScale[i] /
                                 256); // Simple division pour normaliser
    if (processed_grayScale[i] > 65535)
      val_8bit = 255; // Clamp
    if (processed_grayScale[i] < 0)
      val_8bit = 0; // Clamp

    g_displayable_synth_R[i] = val_8bit;
    g_displayable_synth_G[i] = val_8bit;
    g_displayable_synth_B[i] = val_8bit;
  }
  pthread_mutex_unlock(&g_displayable_synth_mutex);
  // Synthèse IFFT terminée
#endif

#if 0
  // Génération d'une onde sinusoïdale simple pour test audio
  printf("Test audio: génération d'une onde sinusoïdale de 440Hz dans "
         "buffer[%d]\n",
         index);
  for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
    buffers_R[index].data[i] = 0.5f * sinf(phase); // Amplitude de 0.5 (50%)
    phase += (TWO_PI * 440) / SAMPLING_FREQUENCY;

    // Éviter que phase devienne trop grand
    if (phase >= TWO_PI) {
      phase -= TWO_PI;
    }
  }

  // Vérifier quelques valeurs de sortie audio
  printf("Valeurs audio de test: %.6f, %.6f, %.6f\n", buffers_R[index].data[0],
         buffers_R[index].data[1], buffers_R[index].data[2]);
#endif

  // Marquer le buffer comme prêt
  pthread_mutex_lock(&buffers_R[index].mutex);
  buffers_R[index].ready = 1;
  pthread_mutex_unlock(&buffers_R[index].mutex);

  // Changer l'indice pour que le callback lise le buffer rempli et que le
  // prochain écriture se fasse sur l'autre buffer
  __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}
