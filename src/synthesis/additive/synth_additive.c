/*
 * synth.c
 *
 *  Created on: 24 avr. 2019
 *      Author: zhonx
 */

// CPU affinity support - must be defined before any includes
#ifdef __linux__
#define _GNU_SOURCE
#endif

/* Includes ------------------------------------------------------------------*/
#include "config.h"

#include "stdio.h"
#include "stdlib.h"
#include <string.h> // For memset, memcpy
/* Commenté pour éviter l'erreur avec cblas.h */
/* #include <Accelerate/Accelerate.h> */
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_c_api.h"
#include "error.h"
#include "shared.h"
#include "synth_additive.h"
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

// Forward declarations for thread pool functions
typedef struct synth_thread_worker_s synth_thread_worker_t;
static int synth_init_thread_pool(void);
static int synth_start_worker_threads(void);
void synth_shutdown_thread_pool(void); // Non-static pour atexit()
static void synth_process_worker_range(synth_thread_worker_t *worker);
static void synth_precompute_wave_data(int32_t *imageData);
void *synth_persistent_worker_thread(void *arg);

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

  // Register cleanup function for thread pool
  atexit(synth_shutdown_thread_pool);

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

  // 🔍 DIAGNOSTIC: Log mode configuration once per call
  static int first_call = 1;
  if (first_call) {
    printf("🔍 MONO_GRAYSCALE: Standard weights R=0.299, G=0.587, B=0.114\n");
    first_call = 0;
  }

  for (i = 0; i < size; i++) {
    uint32_t r = (uint32_t)buffer_R[i];
    uint32_t g = (uint32_t)buffer_G[i];
    uint32_t b = (uint32_t)buffer_B[i];

    uint32_t weighted = (r * 299 + g * 587 + b * 114);
    // Normalisation en 16 bits (0 - 65535)
    gray[i] = (int32_t)((weighted * 65535UL) / 255000UL);

    // 🔍 DIAGNOSTIC: Log some sample values for comparison with perceptual
    // algorithm
    static int sample_counter = 0;
    if (sample_counter % 10000 == 0 && i < 5) {
      printf("🔍 MONO[%d]: RGB(%d,%d,%d) → gray=%d (standard weights)\n", i,
             buffer_R[i], buffer_G[i], buffer_B[i], gray[i]);
    }
    sample_counter++;
  }

  return 0;
}

/**
 * Extract warm channel (left stereo) using perceptual color science
 * Implements opponent color theory with warm colors (red/orange/yellow) → left
 * channel
 * @param buffer_R Red channel input buffer (8-bit)
 * @param buffer_G Green channel input buffer (8-bit)
 * @param buffer_B Blue channel input buffer (8-bit)
 * @param warm_output Output buffer for warm channel (16-bit)
 * @param size Number of pixels to process
 * @return 0 on success
 */
uint32_t extractWarmChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *warm_output,
                            uint32_t size) {
  uint32_t i = 0;

  // 🔍 DIAGNOSTIC: Log mode configuration once per call
  static int first_call = 1;
  if (first_call) {
    printf("🔍 WARM_EXTRACT: SYNTH_MODE=%d, IS_WHITE_BG=%d\n", SYNTH_MODE,
           IS_WHITE_BACKGROUND());
    printf("🔍 PERCEPTUAL_WEIGHTS: R=%.2f, G=%.2f, B=%.2f\n",
           PERCEPTUAL_WEIGHT_R, PERCEPTUAL_WEIGHT_G, PERCEPTUAL_WEIGHT_B);
    printf("🔍 OPPONENT_WEIGHTS: α=%.2f, β=%.2f\n", OPPONENT_ALPHA,
           OPPONENT_BETA);
    first_call = 0;
  }

  for (i = 0; i < size; i++) {
    // Step 1: Convert RGB to normalized [0..1] values
    float r_norm = (float)buffer_R[i] / 255.0f;
    float g_norm = (float)buffer_G[i] / 255.0f;
    float b_norm = (float)buffer_B[i] / 255.0f;

    // Step 2: Calculate perceptual luminance Y
    float luminance_Y = PERCEPTUAL_WEIGHT_R * r_norm +
                        PERCEPTUAL_WEIGHT_G * g_norm +
                        PERCEPTUAL_WEIGHT_B * b_norm;

    // Step 3: Calculate opponent axes
    float O_rb =
        b_norm -
        r_norm; // Blue-Red opponent axis (corrected for intuitive behavior)
    float O_gm =
        (2.0f * g_norm - r_norm - b_norm) / 2.0f; // Green-Magenta opponent axis

    // Step 4: Calculate warm/cold scores
    float S_warm = fmaxf(0.0f, OPPONENT_ALPHA * O_rb + OPPONENT_BETA * O_gm);
    float S_cold =
        fmaxf(0.0f, OPPONENT_ALPHA * (-O_rb) + OPPONENT_BETA * (-O_gm));

    // Step 5: Determine if color is chromatic or achromatic
    float total_chroma = S_warm + S_cold;
    float warm_proportion;

    if (total_chroma > CHROMATIC_THRESHOLD) {
      // Chromatic color: calculate proportion based on warm/cold scores
      warm_proportion = S_warm / total_chroma;
    } else {
      // Achromatic color (gray/white/black): use 50/50 split
      warm_proportion = ACHROMATIC_SPLIT;
    }

    // Step 6: Weight by luminosity and convert to 16-bit
    float warm_energy;
    if (total_chroma > CHROMATIC_THRESHOLD) {
      // Chromatic color: use proportional energy
      warm_energy = luminance_Y * warm_proportion;
    } else {
      // Achromatic color: use full luminance energy (like mono mode)
      warm_energy = luminance_Y;
    }
    int32_t final_value = (int32_t)(warm_energy * VOLUME_AMP_RESOLUTION);

    // Apply color inversion based on SYNTH_MODE (unified system)
    if (IS_WHITE_BACKGROUND()) {
      // White background mode: dark pixels = more energy
      final_value = VOLUME_AMP_RESOLUTION - final_value;
      if (final_value < 0)
        final_value = 0;
      if (final_value > VOLUME_AMP_RESOLUTION)
        final_value = VOLUME_AMP_RESOLUTION;
    }
    // Black background mode: bright pixels = more energy (no inversion needed)

    // 🔍 DIAGNOSTIC: Log some sample values to verify algorithm
    static int sample_counter = 0;
    if (sample_counter % 10000 == 0 && i < 5) {
      printf("🔍 WARM[%d]: RGB(%d,%d,%d) Y=%.3f O_rb=%.3f O_gm=%.3f "
             "S_warm=%.3f prop=%.3f final=%d\n",
             i, buffer_R[i], buffer_G[i], buffer_B[i], luminance_Y, O_rb, O_gm,
             S_warm, warm_proportion, final_value);
    }
    sample_counter++;

    warm_output[i] = final_value;
  }

  return 0;
}

/**
 * Extract cold channel (right stereo) using perceptual color science
 * Implements opponent color theory with cold colors (blue/cyan) → right channel
 * @param buffer_R Red channel input buffer (8-bit)
 * @param buffer_G Green channel input buffer (8-bit)
 * @param buffer_B Blue channel input buffer (8-bit)
 * @param cold_output Output buffer for cold channel (16-bit)
 * @param size Number of pixels to process
 * @return 0 on success
 */
uint32_t extractColdChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *cold_output,
                            uint32_t size) {
  uint32_t i = 0;

  for (i = 0; i < size; i++) {
    // Step 1: Convert RGB to normalized [0..1] values
    float r_norm = (float)buffer_R[i] / 255.0f;
    float g_norm = (float)buffer_G[i] / 255.0f;
    float b_norm = (float)buffer_B[i] / 255.0f;

    // Step 2: Calculate perceptual luminance Y
    float luminance_Y = PERCEPTUAL_WEIGHT_R * r_norm +
                        PERCEPTUAL_WEIGHT_G * g_norm +
                        PERCEPTUAL_WEIGHT_B * b_norm;

    // Step 3: Calculate opponent axes
    float O_rb =
        b_norm -
        r_norm; // Blue-Red opponent axis (corrected for intuitive behavior)
    float O_gm =
        (2.0f * g_norm - r_norm - b_norm) / 2.0f; // Green-Magenta opponent axis

    // Step 4: Calculate warm/cold scores
    float S_warm = fmaxf(0.0f, OPPONENT_ALPHA * O_rb + OPPONENT_BETA * O_gm);
    float S_cold =
        fmaxf(0.0f, OPPONENT_ALPHA * (-O_rb) + OPPONENT_BETA * (-O_gm));

    // Step 5: Determine if color is chromatic or achromatic
    float total_chroma = S_warm + S_cold;
    float cold_proportion;

    if (total_chroma > CHROMATIC_THRESHOLD) {
      // Chromatic color: calculate proportion based on warm/cold scores
      cold_proportion = S_cold / total_chroma;
    } else {
      // Achromatic color (gray/white/black): use 50/50 split
      cold_proportion = 1.0f - ACHROMATIC_SPLIT; // Complement of warm split
    }

    // Step 6: Weight by luminosity and convert to 16-bit
    float cold_energy;
    if (total_chroma > CHROMATIC_THRESHOLD) {
      // Chromatic color: use proportional energy
      cold_energy = luminance_Y * cold_proportion;
    } else {
      // Achromatic color: use full luminance energy (like mono mode)
      cold_energy = luminance_Y;
    }
    int32_t final_value = (int32_t)(cold_energy * VOLUME_AMP_RESOLUTION);

    // Apply color inversion based on SYNTH_MODE (unified system)
    if (IS_WHITE_BACKGROUND()) {
      // White background mode: dark pixels = more energy
      final_value = VOLUME_AMP_RESOLUTION - final_value;
      if (final_value < 0)
        final_value = 0;
      if (final_value > VOLUME_AMP_RESOLUTION)
        final_value = VOLUME_AMP_RESOLUTION;
    }
    // Black background mode: bright pixels = more energy (no inversion needed)

    // 🔍 DIAGNOSTIC: Log some sample values to verify algorithm
    static int sample_counter = 0;
    if (sample_counter % 10000 == 0 && i < 5) {
      printf("🔍 COLD[%d]: RGB(%d,%d,%d) Y=%.3f O_rb=%.3f O_gm=%.3f "
             "S_cold=%.3f prop=%.3f final=%d\n",
             i, buffer_R[i], buffer_G[i], buffer_B[i], luminance_Y, O_rb, O_gm,
             S_cold, cold_proportion, final_value);
    }
    sample_counter++;

    cold_output[i] = final_value;
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
 * @brief  Structure pour le pool de threads persistants optimisé
 */
typedef struct synth_thread_worker_s {
  int thread_id;      // ID du thread (0, 1, 2)
  int start_note;     // Note de départ pour ce thread
  int end_note;       // Note de fin pour ce thread
  int32_t *imageData; // Données d'image d'entrée (partagé)

  // Buffers de sortie locaux au thread
  float thread_ifftBuffer[AUDIO_BUFFER_SIZE];
  float thread_sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  float thread_maxVolumeBuffer[AUDIO_BUFFER_SIZE];

  // Buffers de travail locaux (évite VLA sur pile)
  int32_t imageBuffer_q31[NUMBER_OF_NOTES / 3 + 100]; // +100 pour sécurité
  float imageBuffer_f32[NUMBER_OF_NOTES / 3 + 100];
  float waveBuffer[AUDIO_BUFFER_SIZE];
  float volumeBuffer[AUDIO_BUFFER_SIZE];

  // Données waves[] pré-calculées (lecture seule)
  int32_t precomputed_new_idx[NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];
  float precomputed_wave_data[NUMBER_OF_NOTES / 3 + 100][AUDIO_BUFFER_SIZE];
  float precomputed_volume[NUMBER_OF_NOTES / 3 + 100];
  float precomputed_volume_increment[NUMBER_OF_NOTES / 3 + 100];
  float precomputed_volume_decrement[NUMBER_OF_NOTES / 3 + 100];

  // Synchronisation
  pthread_mutex_t work_mutex;
  pthread_cond_t work_cond;
  volatile int work_ready;
  volatile int work_done;

} synth_thread_worker_t;

// Pool de threads persistants
static synth_thread_worker_t thread_pool[3];
static pthread_t worker_threads[3];
static volatile int synth_pool_initialized = 0;
static volatile int synth_pool_shutdown = 0;

// Mutex global pour protéger l'accès aux données waves[] pendant le pré-calcul
static pthread_mutex_t waves_global_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief  Initialise le pool de threads persistants
 * @retval 0 en cas de succès, -1 en cas d'erreur
 */
static int synth_init_thread_pool(void) {
  if (synth_pool_initialized)
    return 0;

  int notes_per_thread = NUMBER_OF_NOTES / 3;

  for (int i = 0; i < 3; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    // Configuration du worker
    worker->thread_id = i;
    worker->start_note = i * notes_per_thread;
    worker->end_note = (i == 2) ? NUMBER_OF_NOTES : (i + 1) * notes_per_thread;
    worker->work_ready = 0;
    worker->work_done = 0;

    // Initialisation de la synchronisation
    if (pthread_mutex_init(&worker->work_mutex, NULL) != 0) {
      printf("Erreur lors de l'initialisation du mutex pour le thread %d\n", i);
      return -1;
    }
    if (pthread_cond_init(&worker->work_cond, NULL) != 0) {
      printf(
          "Erreur lors de l'initialisation de la condition pour le thread %d\n",
          i);
      return -1;
    }
  }

  synth_pool_initialized = 1;
  return 0;
}

/**
 * @brief  Fonction principale des threads workers persistants
 * @param  arg Pointeur vers la structure synth_thread_worker_t
 * @retval Pointeur NULL
 */
void *synth_persistent_worker_thread(void *arg) {
  synth_thread_worker_t *worker = (synth_thread_worker_t *)arg;

  while (!synth_pool_shutdown) {
    // Attendre du travail
    pthread_mutex_lock(&worker->work_mutex);
    while (!worker->work_ready && !synth_pool_shutdown) {
      pthread_cond_wait(&worker->work_cond, &worker->work_mutex);
    }
    pthread_mutex_unlock(&worker->work_mutex);

    if (synth_pool_shutdown)
      break;

    // Effectuer le travail
    synth_process_worker_range(worker);

    // Signaler que le travail est terminé
    pthread_mutex_lock(&worker->work_mutex);
    worker->work_done = 1;
    worker->work_ready = 0;
    pthread_mutex_unlock(&worker->work_mutex);
  }

  return NULL;
}

/**
 * @brief  Traite une plage de notes pour un worker donné
 * @param  worker Pointeur vers la structure du worker
 * @retval None
 */
static void synth_process_worker_range(synth_thread_worker_t *worker) {
  int32_t idx, acc, buff_idx, note, local_note_idx;

  // 🔍 DIAGNOSTIC: Check if this function is called (should be for stereo red
  // channel)
  static int worker_call_counter = 0;
  if (worker_call_counter % 1000 == 0) {
    printf("🔍 WORKER_RANGE: Called (thread %d), IS_WHITE_BG=%d\n",
           worker->thread_id, IS_WHITE_BACKGROUND());
  }
  worker_call_counter++;

  // Initialiser les buffers de sortie à zéro
  fill_float(0, worker->thread_ifftBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_maxVolumeBuffer, AUDIO_BUFFER_SIZE);

  // Prétraitement: calcul des moyennes et transformation en imageBuffer_q31
  for (idx = worker->start_note; idx < worker->end_note; idx++) {
    local_note_idx = idx - worker->start_note;
    worker->imageBuffer_q31[local_note_idx] = 0;

    for (acc = 0; acc < PIXELS_PER_NOTE; acc++) {
      worker->imageBuffer_q31[local_note_idx] +=
          (worker->imageData[idx * PIXELS_PER_NOTE + acc]);
    }

    worker->imageBuffer_q31[local_note_idx] /= PIXELS_PER_NOTE;

    // Apply color inversion for mono mode based on SYNTH_MODE
    if (!IS_STEREO_MODE()) {
      // Mono mode: apply inversion based on background color
      if (IS_WHITE_BACKGROUND()) {
        // White background mode: dark pixels = more energy
        worker->imageBuffer_q31[local_note_idx] =
            VOLUME_AMP_RESOLUTION - worker->imageBuffer_q31[local_note_idx];
        if (worker->imageBuffer_q31[local_note_idx] < 0)
          worker->imageBuffer_q31[local_note_idx] = 0;
        if (worker->imageBuffer_q31[local_note_idx] > VOLUME_AMP_RESOLUTION)
          worker->imageBuffer_q31[local_note_idx] = VOLUME_AMP_RESOLUTION;
      }
      // Black background mode: bright pixels = more energy (no inversion
      // needed)
    }
    // Stereo mode: inversion already done in
    // extractWarmChannel()/extractColdChannel()
  }

  // Correction bug - seulement pour le thread qui traite la note 0
  if (worker->start_note == 0) {
    worker->imageBuffer_q31[0] = 0;
  }

#ifdef RELATIVE_MODE
  // Traitement spécial pour RELATIVE_MODE
  if (worker->start_note < worker->end_note - 1) {
    sub_int32((int32_t *)&worker->imageBuffer_q31[0],
              (int32_t *)&worker->imageBuffer_q31[1],
              (int32_t *)&worker->imageBuffer_q31[0],
              worker->end_note - worker->start_note - 1);

    clip_int32((int32_t *)worker->imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION,
               worker->end_note - worker->start_note);
  }

  if (worker->end_note == NUMBER_OF_NOTES) {
    worker->imageBuffer_q31[worker->end_note - worker->start_note - 1] = 0;
  }
#endif

  // Traitement principal des notes
  for (note = worker->start_note; note < worker->end_note; note++) {
    local_note_idx = note - worker->start_note;
    worker->imageBuffer_f32[local_note_idx] =
        (float)worker->imageBuffer_q31[local_note_idx];

#if ENABLE_NON_LINEAR_MAPPING
    {
      float normalizedIntensity = worker->imageBuffer_f32[local_note_idx] /
                                  (float)VOLUME_AMP_RESOLUTION;
      float gamma = GAMMA_VALUE;
      normalizedIntensity = powf(normalizedIntensity, gamma);
      worker->imageBuffer_f32[local_note_idx] =
          normalizedIntensity * VOLUME_AMP_RESOLUTION;
    }
#endif

    // Utiliser les données pré-calculées pour éviter les accès concurrents à
    // waves[]
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      worker->waveBuffer[buff_idx] =
          worker->precomputed_wave_data[local_note_idx][buff_idx];
    }

#ifdef GAP_LIMITER
    // ✅ CORRECTION: Gap limiter avec accès direct à waves[] (thread-safe car
    // notes distinctes)
    float target_volume = worker->imageBuffer_f32[local_note_idx];

    // Calculer dynamiquement le volume avec gap limiter (accès direct à
    // waves[])
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE - 1; buff_idx++) {
      if (waves[note].current_volume < target_volume) {
        waves[note].current_volume += waves[note].volume_increment;
        if (waves[note].current_volume > target_volume) {
          waves[note].current_volume = target_volume;
          break;
        }
      } else {
        waves[note].current_volume -= waves[note].volume_decrement;
        if (waves[note].current_volume < target_volume) {
          waves[note].current_volume = target_volume;
          break;
        }
      }
      worker->volumeBuffer[buff_idx] = waves[note].current_volume;
    }

    // Fill remaining buffer with final volume value
    if (buff_idx < AUDIO_BUFFER_SIZE) {
      fill_float(waves[note].current_volume, &worker->volumeBuffer[buff_idx],
                 AUDIO_BUFFER_SIZE - buff_idx);
    }
#else
    fill_float(worker->imageBuffer_f32[local_note_idx], worker->volumeBuffer,
               AUDIO_BUFFER_SIZE);
#endif

    // Apply volume scaling to the current note waveform
    mult_float(worker->waveBuffer, worker->volumeBuffer, worker->waveBuffer,
               AUDIO_BUFFER_SIZE);

    for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;) {
      if (worker->volumeBuffer[buff_idx] >
          worker->thread_maxVolumeBuffer[buff_idx]) {
        worker->thread_maxVolumeBuffer[buff_idx] =
            worker->volumeBuffer[buff_idx];
      }
    }

    // IFFT summation (local au thread)
    add_float(worker->waveBuffer, worker->thread_ifftBuffer,
              worker->thread_ifftBuffer, AUDIO_BUFFER_SIZE);
    // Volume summation (local au thread)
    add_float(worker->volumeBuffer, worker->thread_sumVolumeBuffer,
              worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  }
}

/**
 * @brief  Pré-calcule les données waves[] en parallèle pour éviter la
 * contention
 * @param  imageData Données d'image d'entrée
 * @retval None
 */
static void synth_precompute_wave_data(int32_t *imageData) {
  // ✅ OPTIMISATION: Pré-calcul parallélisé pour équilibrer la charge CPU

  // Phase 1: Assignation des données d'image (thread-safe, lecture seule)
  for (int i = 0; i < 3; i++) {
    thread_pool[i].imageData = imageData;
  }

  // Phase 2: Pré-calcul parallèle des données waves[] par plages
  pthread_mutex_lock(&waves_global_mutex);

  // Utiliser les workers pour pré-calculer en parallèle
  for (int i = 0; i < 3; i++) {
    synth_thread_worker_t *worker = &thread_pool[i];

    for (int note = worker->start_note; note < worker->end_note; note++) {
      int local_note_idx = note - worker->start_note;

      // Pré-calculer les données de forme d'onde
      for (int buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        int32_t new_idx = (waves[note].current_idx + waves[note].octave_coeff);
        if ((uint32_t)new_idx >= waves[note].area_size) {
          new_idx -= waves[note].area_size;
        }

        worker->precomputed_new_idx[local_note_idx][buff_idx] = new_idx;
        worker->precomputed_wave_data[local_note_idx][buff_idx] =
            (*(waves[note].start_ptr + new_idx));
        waves[note].current_idx = new_idx;
      }

#ifdef GAP_LIMITER
      // ✅ GAP_LIMITER: Ne pas pré-calculer le volume - les threads l'accèdent
      // directement Les paramètres increment/decrement sont thread-safe en
      // lecture seule
      worker->precomputed_volume_increment[local_note_idx] =
          waves[note].volume_increment;
      worker->precomputed_volume_decrement[local_note_idx] =
          waves[note].volume_decrement;
#endif
    }
  }

  pthread_mutex_unlock(&waves_global_mutex);
}

/**
 * @brief  Démarre les threads workers persistants avec affinité CPU
 * @retval 0 en cas de succès, -1 en cas d'erreur
 */
static int synth_start_worker_threads(void) {
  for (int i = 0; i < 3; i++) {
    if (pthread_create(&worker_threads[i], NULL, synth_persistent_worker_thread,
                       &thread_pool[i]) != 0) {
      printf("Erreur lors de la création du thread worker %d\n", i);
      return -1;
    }

    // ✅ OPTIMISATION: Affinité CPU pour équilibrer la charge sur Pi5
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Distribuer les threads sur les CPUs 1, 2, 3 (laisser CPU 0 pour le
    // système)
    CPU_SET(i + 1, &cpuset);

    int result =
        pthread_setaffinity_np(worker_threads[i], sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
      printf("Thread worker %d assigné au CPU %d\n", i, i + 1);
    } else {
      printf("Impossible d'assigner le thread %d au CPU %d (erreur: %d)\n", i,
             i + 1, result);
    }
#endif
  }
  return 0;
}

/**
 * @brief  Arrête le pool de threads persistants
 * @retval None
 */
void synth_shutdown_thread_pool(void) {
  if (!synth_pool_initialized)
    return;

  synth_pool_shutdown = 1;

  // Réveiller tous les threads
  for (int i = 0; i < 3; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    pthread_cond_signal(&thread_pool[i].work_cond);
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }

  // Attendre que tous les threads se terminent
  for (int i = 0; i < 3; i++) {
    pthread_join(worker_threads[i], NULL);
    pthread_mutex_destroy(&thread_pool[i].work_mutex);
    pthread_cond_destroy(&thread_pool[i].work_cond);
  }

  synth_pool_initialized = 0;
}

/**
 * @brief  Version optimisée de la synthèse IFFT avec pool de threads
 * persistants
 * @param  imageData Données d'entrée en niveaux de gris
 * @param  audioData Buffer de sortie audio
 * @retval None
 */
void synth_IfftMode(
    int32_t *imageData,
    float *audioData) { // imageData is now potentially frozen/faded g_grayScale

  // Mode IFFT (logs limités)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== IFFT Mode appelé (optimisé) =====\n");
  }

  static int32_t signal_R;
  static int buff_idx;
  static int first_call = 1;

  // Initialiser le pool de threads si première fois
  if (first_call) {
    if (synth_init_thread_pool() == 0) {
      if (synth_start_worker_threads() == 0) {
        printf("Pool de threads optimisé initialisé avec succès\n");
      } else {
        printf(
            "Erreur lors du démarrage des threads, mode séquentiel activé\n");
        synth_pool_initialized = 0;
      }
    } else {
      printf(
          "Erreur lors de l'initialisation du pool, mode séquentiel activé\n");
      synth_pool_initialized = 0;
    }
    first_call = 0;
  }

  // Buffers finaux pour les résultats combinés
  static float ifftBuffer[AUDIO_BUFFER_SIZE];
  static float sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  static float maxVolumeBuffer[AUDIO_BUFFER_SIZE];

  // Réinitialiser les buffers finaux
  fill_float(0, ifftBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, maxVolumeBuffer, AUDIO_BUFFER_SIZE);

  float tmp_audioData[AUDIO_BUFFER_SIZE];

  if (synth_pool_initialized && !synth_pool_shutdown) {
    // === VERSION OPTIMISÉE AVEC POOL DE THREADS ===

    // Phase 1: Pré-calcul des données en single-thread (évite la contention)
    synth_precompute_wave_data(imageData);

    // Phase 2: Démarrer les workers en parallèle
    for (int i = 0; i < 3; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      thread_pool[i].work_ready = 1;
      thread_pool[i].work_done = 0;
      pthread_cond_signal(&thread_pool[i].work_cond);
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    // Phase 3: Attendre que tous les workers terminent (optimisé pour Pi5)
    for (int i = 0; i < 3; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      while (!thread_pool[i].work_done) {
        // ✅ OPTIMISATION Pi5: Attente passive pour réduire la charge CPU
        struct timespec sleep_time = {0, 100000}; // 100 microseconds
        pthread_mutex_unlock(&thread_pool[i].work_mutex);
        nanosleep(&sleep_time, NULL); // Sleep au lieu de busy wait
        pthread_mutex_lock(&thread_pool[i].work_mutex);
      }
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    /*// 🔍 DIAGNOSTIC: Analyser les buffers de chaque thread avant accumulation
    if (log_counter % LOG_FREQUENCY == 0) {
      for (int i = 0; i < 3; i++) {
        float thread_min = thread_pool[i].thread_ifftBuffer[0];
        float thread_max = thread_pool[i].thread_ifftBuffer[0];
        float thread_sum = 0.0f;

        for (int j = 0; j < AUDIO_BUFFER_SIZE; j++) {
          float val = thread_pool[i].thread_ifftBuffer[j];
          if (val < thread_min)
            thread_min = val;
          if (val > thread_max)
            thread_max = val;
          thread_sum += val * val; // Pour RMS
        }

        float thread_rms = sqrtf(thread_sum / AUDIO_BUFFER_SIZE);
        printf("🔍 THREAD %d: min=%.6f, max=%.6f, rms=%.6f\n", i, thread_min,
               thread_max, thread_rms);
      }
    }*/

    // Phase 4: Combiner les résultats des threads avec normalisation
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_ifftBuffer, ifftBuffer, ifftBuffer,
                AUDIO_BUFFER_SIZE);
      add_float(thread_pool[i].thread_sumVolumeBuffer, sumVolumeBuffer,
                sumVolumeBuffer, AUDIO_BUFFER_SIZE);

      // Pour maxVolumeBuffer, prendre le maximum
      for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (thread_pool[i].thread_maxVolumeBuffer[buff_idx] >
            maxVolumeBuffer[buff_idx]) {
          maxVolumeBuffer[buff_idx] =
              thread_pool[i].thread_maxVolumeBuffer[buff_idx];
        }
      }
    }

    // 🔍 DIAGNOSTIC: Analyser le signal AVANT normalisation (pour comparaison
    // Mac/Pi)
    if (log_counter % LOG_FREQUENCY == 0) {
      float raw_min = ifftBuffer[0];
      float raw_max = ifftBuffer[0];
      float raw_sum = 0.0f;

      for (int j = 0; j < AUDIO_BUFFER_SIZE; j++) {
        float val = ifftBuffer[j];
        if (val < raw_min)
          raw_min = val;
        if (val > raw_max)
          raw_max = val;
        raw_sum += val * val;
      }

      float raw_rms = sqrtf(raw_sum / AUDIO_BUFFER_SIZE);
      printf("🔍 AVANT NORMALISATION: min=%.6f, max=%.6f, rms=%.6f\n", raw_min,
             raw_max, raw_rms);
    }

    // 🔧 CORRECTION: Normalisation conditionnelle par plateforme
#ifdef __linux__
    // Pi/Linux : Diviser par 3 (BossDAC/ALSA amplifie naturellement)
    scale_float(ifftBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(sumVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(maxVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
#else
    // Mac : Pas de division (CoreAudio ne compense pas automatiquement)
    // Signal gardé à pleine amplitude pour volume normal
#endif

    /*// 🔍 DIAGNOSTIC: Analyser le signal APRÈS normalisation (pour comparaison
    // Mac/Pi)
    if (log_counter % LOG_FREQUENCY == 0) {
      float norm_min = ifftBuffer[0];
      float norm_max = ifftBuffer[0];
      float norm_sum = 0.0f;

      for (int j = 0; j < AUDIO_BUFFER_SIZE; j++) {
        float val = ifftBuffer[j];
        if (val < norm_min)
          norm_min = val;
        if (val > norm_max)
          norm_max = val;
        norm_sum += val * val;
      }

      float norm_rms = sqrtf(norm_sum / AUDIO_BUFFER_SIZE);
      printf("🔍 APRÈS NORMALISATION: min=%.6f, max=%.6f, rms=%.6f\n", norm_min,
             norm_max, norm_rms);
    }*/

    /*// 🔍 DIAGNOSTIC: Analyser le signal après accumulation des threads
    if (log_counter % LOG_FREQUENCY == 0) {
      float accum_min = ifftBuffer[0];
      float accum_max = ifftBuffer[0];
      float accum_sum = 0.0f;

      for (int j = 0; j < AUDIO_BUFFER_SIZE; j++) {
        float val = ifftBuffer[j];
        if (val < accum_min)
          accum_min = val;
        if (val > accum_max)
          accum_max = val;
        accum_sum += val * val;
      }

      float accum_rms = sqrtf(accum_sum / AUDIO_BUFFER_SIZE);
      printf("🎯 ACCUMULATION: min=%.6f, max=%.6f, rms=%.6f\n", accum_min,
             accum_max, accum_rms);
    }*/

    // ✅ Phase 5 supprimée : Les threads accèdent directement à waves[] donc
    // les volumes sont déjà synchronisés

  } else {
    // === FALLBACK MODE SÉQUENTIEL (pour compatibilité/debug) ===
    static int32_t imageBuffer_q31[NUMBER_OF_NOTES];
    static float imageBuffer_f32[NUMBER_OF_NOTES];
    static float waveBuffer[AUDIO_BUFFER_SIZE];
    static float volumeBuffer[AUDIO_BUFFER_SIZE];

    // Version séquentielle simplifiée de l'algorithme original
    int32_t idx, acc, new_idx, note;

    // Prétraitement: calcul des moyennes
    for (idx = 0; idx < NUMBER_OF_NOTES; idx++) {
      imageBuffer_q31[idx] = 0;
      for (acc = 0; acc < PIXELS_PER_NOTE; acc++) {
        imageBuffer_q31[idx] += (imageData[idx * PIXELS_PER_NOTE + acc]);
      }
      imageBuffer_q31[idx] /= PIXELS_PER_NOTE;

      // Apply color inversion based on SYNTH_MODE (unified system)
      if (IS_WHITE_BACKGROUND()) {
        // White background mode: dark pixels = more energy
        imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[idx];
        if (imageBuffer_q31[idx] < 0)
          imageBuffer_q31[idx] = 0;
        if (imageBuffer_q31[idx] > VOLUME_AMP_RESOLUTION)
          imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION;
      }
      // Black background mode: bright pixels = more energy (no inversion
      // needed)
    }
    imageBuffer_q31[0] = 0; // Correction bug

#ifdef RELATIVE_MODE
    sub_int32((int32_t *)imageBuffer_q31, (int32_t *)&imageBuffer_q31[1],
              (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES - 1);
    clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION,
               NUMBER_OF_NOTES);
    imageBuffer_q31[NUMBER_OF_NOTES - 1] = 0;
#endif

    // Traitement principal des notes
    for (note = 0; note < NUMBER_OF_NOTES; note++) {
      imageBuffer_f32[note] = (float)imageBuffer_q31[note];

#if ENABLE_NON_LINEAR_MAPPING
      {
        float normalizedIntensity =
            imageBuffer_f32[note] / (float)VOLUME_AMP_RESOLUTION;
        float gamma = GAMMA_VALUE;
        normalizedIntensity = powf(normalizedIntensity, gamma);
        imageBuffer_f32[note] = normalizedIntensity * VOLUME_AMP_RESOLUTION;
      }
#endif

      // Génération des formes d'onde
      for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        new_idx = (waves[note].current_idx + waves[note].octave_coeff);
        if ((uint32_t)new_idx >= waves[note].area_size) {
          new_idx -= waves[note].area_size;
        }
        waveBuffer[buff_idx] = (*(waves[note].start_ptr + new_idx));
        waves[note].current_idx = new_idx;
      }

#ifdef GAP_LIMITER
      // Gap limiter
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
      if (buff_idx < AUDIO_BUFFER_SIZE) {
        fill_float(waves[note].current_volume, &volumeBuffer[buff_idx],
                   AUDIO_BUFFER_SIZE - buff_idx);
      }
#else
      fill_float(imageBuffer_f32[note], volumeBuffer, AUDIO_BUFFER_SIZE);
#endif

      // Apply volume scaling
      mult_float(waveBuffer, volumeBuffer, waveBuffer, AUDIO_BUFFER_SIZE);

      for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;) {
        if (volumeBuffer[buff_idx] > maxVolumeBuffer[buff_idx]) {
          maxVolumeBuffer[buff_idx] = volumeBuffer[buff_idx];
        }
      }

      // Accumulation
      add_float(waveBuffer, ifftBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
      add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer,
                AUDIO_BUFFER_SIZE);
    }
  }

  // === PHASE FINALE (commune aux deux modes) ===
  mult_float(ifftBuffer, maxVolumeBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
  scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);

  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    if (sumVolumeBuffer[buff_idx] != 0) {
      signal_R = (int32_t)(ifftBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
    } else {
      signal_R = 0;
    }
    tmp_audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;
  }

  // Calculer le facteur de contraste basé sur l'image
  float contrast_factor = calculate_contrast(imageData, CIS_MAX_PIXELS_NB);

  // Apply contrast modulation
  float min_level = 0.0f, max_level = 0.0f;
  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    audioData[buff_idx] = tmp_audioData[buff_idx] * contrast_factor;

    // Track min/max for debug
    if (buff_idx == 0 || audioData[buff_idx] < min_level)
      min_level = audioData[buff_idx];
    if (buff_idx == 0 || audioData[buff_idx] > max_level)
      max_level = audioData[buff_idx];
  }

  // 🔍 DIAGNOSTIC LOGS for mono mode comparison
  if (log_counter % LOG_FREQUENCY == 0) {
    float final_rms = 0.0f;
    int clipped_samples = 0;

    for (int j = 0; j < AUDIO_BUFFER_SIZE; j++) {
      final_rms += audioData[j] * audioData[j];
      if (audioData[j] >= 0.95f || audioData[j] <= -0.95f) {
        clipped_samples++;
      }
    }
    final_rms = sqrtf(final_rms / AUDIO_BUFFER_SIZE);

    printf("🎯 MONO_MODE: min=%.6f, max=%.6f, rms=%.6f, clipped=%d/%d, "
           "contrast=%.3f\n",
           min_level, max_level, final_rms, clipped_samples, AUDIO_BUFFER_SIZE,
           contrast_factor);
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

  // Mettre à jour les buffers d'affichage globaux avec les données couleur
  // originales
  pthread_mutex_lock(&g_displayable_synth_mutex);
  memcpy(g_displayable_synth_R, buffer_R, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_G, buffer_G, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_B, buffer_B, CIS_MAX_PIXELS_NB);
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

/**
 * Stereo synthesis function - processes red and blue channels independently
 * Red channel -> Left audio channel, Blue channel -> Right audio channel
 * @param buffer_R Red channel input buffer (8-bit)
 * @param buffer_G Green channel input buffer (8-bit) - ignored in stereo mode
 * @param buffer_B Blue channel input buffer (8-bit)
 */
void synth_AudioProcessStereo(uint8_t *buffer_R, uint8_t *buffer_G,
                              uint8_t *buffer_B) {
  // Traitement audio stéréo (logs limités)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Stereo Audio Process appelé =====\n");
  }

  // Vérifier que les buffers d'entrée ne sont pas NULL
  if (!buffer_R || !buffer_G || !buffer_B) {
    printf("ERREUR: Un des buffers d'entrée est NULL!\n");
    return;
  }

  int index = __atomic_load_n(&current_buffer_index, __ATOMIC_RELAXED);

  // Buffers pour les canaux chaud et froid séparés (perceptual color science)
  static int32_t g_warmChannel_live[CIS_MAX_PIXELS_NB];
  static int32_t g_coldChannel_live[CIS_MAX_PIXELS_NB];

  // Buffers pour les données traitées (freeze/fade)
  int32_t processed_warmChannel[CIS_MAX_PIXELS_NB];
  int32_t processed_coldChannel[CIS_MAX_PIXELS_NB];

  // Attendre que le buffer destinataire soit libre
  pthread_mutex_lock(&buffers_R[index].mutex);
  while (buffers_R[index].ready != 0) {
    pthread_cond_wait(&buffers_R[index].cond, &buffers_R[index].mutex);
  }
  pthread_mutex_unlock(&buffers_R[index].mutex);

  // Attendre que le buffer gauche soit libre aussi (pour le stéréo)
  pthread_mutex_lock(&buffers_L[index].mutex);
  while (buffers_L[index].ready != 0) {
    pthread_cond_wait(&buffers_L[index].cond, &buffers_L[index].mutex);
  }
  pthread_mutex_unlock(&buffers_L[index].mutex);

  // Extraire les canaux chaud et froid séparément (perceptual color science)
  // L'inversion des couleurs est maintenant gérée directement dans les
  // fonctions d'extraction
  extractWarmChannel(buffer_R, buffer_G, buffer_B, g_warmChannel_live,
                     CIS_MAX_PIXELS_NB);
  extractColdChannel(buffer_R, buffer_G, buffer_B, g_coldChannel_live,
                     CIS_MAX_PIXELS_NB);

  // --- Synth Data Freeze/Fade Logic (pour les deux canaux) ---
  pthread_mutex_lock(&g_synth_data_freeze_mutex);
  int local_is_frozen = g_is_synth_data_frozen;
  int local_is_fading = g_is_synth_data_fading_out;

  static int prev_frozen_state_stereo = 0;
  static int32_t g_frozen_warmChannel[CIS_MAX_PIXELS_NB];
  static int32_t g_frozen_coldChannel[CIS_MAX_PIXELS_NB];

  if (local_is_frozen && !prev_frozen_state_stereo && !local_is_fading) {
    memcpy(g_frozen_warmChannel, g_warmChannel_live,
           sizeof(g_warmChannel_live));
    memcpy(g_frozen_coldChannel, g_coldChannel_live,
           sizeof(g_coldChannel_live));
  }
  prev_frozen_state_stereo = local_is_frozen;

  static int prev_fading_state_stereo = 0;
  if (local_is_fading && !prev_fading_state_stereo) {
    g_synth_data_fade_start_time = synth_getCurrentTimeInSeconds();
  }
  prev_fading_state_stereo = local_is_fading;
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
      memcpy(processed_warmChannel, g_warmChannel_live,
             sizeof(g_warmChannel_live));
      memcpy(processed_coldChannel, g_coldChannel_live,
             sizeof(g_coldChannel_live));
    } else {
      alpha_blend = (float)(elapsed_time / G_SYNTH_DATA_FADE_DURATION_SECONDS);
      alpha_blend = (alpha_blend < 0.0f)
                        ? 0.0f
                        : ((alpha_blend > 1.0f) ? 1.0f : alpha_blend);
      for (int i = 0; i < CIS_MAX_PIXELS_NB; ++i) {
        processed_warmChannel[i] =
            (int32_t)(g_frozen_warmChannel[i] * (1.0f - alpha_blend) +
                      g_warmChannel_live[i] * alpha_blend);
        processed_coldChannel[i] =
            (int32_t)(g_frozen_coldChannel[i] * (1.0f - alpha_blend) +
                      g_coldChannel_live[i] * alpha_blend);
      }
    }
  } else if (local_is_frozen) {
    memcpy(processed_warmChannel, g_frozen_warmChannel,
           sizeof(g_frozen_warmChannel));
    memcpy(processed_coldChannel, g_frozen_coldChannel,
           sizeof(g_frozen_coldChannel));
  } else {
    memcpy(processed_warmChannel, g_warmChannel_live,
           sizeof(g_warmChannel_live));
    memcpy(processed_coldChannel, g_coldChannel_live,
           sizeof(g_coldChannel_live));
  }
  // --- End Synth Data Freeze/Fade Logic ---

  // 🔧 PERCEPTUAL STEREO: Warm colors → left channel, Cold colors → right
  // channel 🎯 TRUE STEREO MODE: Both channels active to test crackling issue

  // Left channel: warm colors (red/orange/yellow) using optimized thread pool
  synth_IfftMode(processed_warmChannel, buffers_L[index].data);

  // Right channel: cold colors (blue/cyan) using stateless version to avoid
  // interference
  synth_IfftMode_Stateless(processed_coldChannel, buffers_R[index].data);

  // Mettre à jour les buffers d'affichage globaux avec les données couleur
  // originales
  pthread_mutex_lock(&g_displayable_synth_mutex);
  memcpy(g_displayable_synth_R, buffer_R, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_G, buffer_G, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_B, buffer_B, CIS_MAX_PIXELS_NB);
  pthread_mutex_unlock(&g_displayable_synth_mutex);

  // Marquer les buffers comme prêts
  pthread_mutex_lock(&buffers_L[index].mutex);
  buffers_L[index].ready = 1;
  pthread_mutex_unlock(&buffers_L[index].mutex);

  pthread_mutex_lock(&buffers_R[index].mutex);
  buffers_R[index].ready = 1;
  pthread_mutex_unlock(&buffers_R[index].mutex);

  // Changer l'indice pour que le callback lise les buffers remplis
  __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}

/**
 * @brief Thread-safe version of synth_IfftMode for stereo synthesis
 * This function doesn't modify the global waves[] state, preventing
 * interference between left and right channel synthesis calls
 * @param imageData Input grayscale data
 * @param audioData Output audio buffer
 * @retval None
 */
void synth_IfftMode_Stateless(int32_t *imageData, float *audioData) {
  // Create local copies of oscillator states to avoid interference
  static __thread uint32_t local_current_idx[NUMBER_OF_NOTES];
  static __thread float local_current_volume[NUMBER_OF_NOTES];
  static __thread int thread_initialized = 0;

  // Initialize thread-local state on first call
  if (!thread_initialized) {
    for (int i = 0; i < NUMBER_OF_NOTES; i++) {
      local_current_idx[i] = waves[i].current_idx;
      local_current_volume[i] = waves[i].current_volume;
    }
    thread_initialized = 1;
  }

  // Use the same algorithm as synth_IfftMode but with local state
  static __thread float ifftBuffer[AUDIO_BUFFER_SIZE];
  static __thread float sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  static __thread float maxVolumeBuffer[AUDIO_BUFFER_SIZE];
  static __thread int32_t imageBuffer_q31[NUMBER_OF_NOTES];
  static __thread float imageBuffer_f32[NUMBER_OF_NOTES];
  static __thread float waveBuffer[AUDIO_BUFFER_SIZE];
  static __thread float volumeBuffer[AUDIO_BUFFER_SIZE];

  // Initialize buffers
  fill_float(0, ifftBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, maxVolumeBuffer, AUDIO_BUFFER_SIZE);

  int32_t idx, acc, new_idx, note, buff_idx;
  int32_t signal_R;

  // Preprocessing: calculate averages
  for (idx = 0; idx < NUMBER_OF_NOTES; idx++) {
    imageBuffer_q31[idx] = 0;
    for (acc = 0; acc < PIXELS_PER_NOTE; acc++) {
      imageBuffer_q31[idx] += (imageData[idx * PIXELS_PER_NOTE + acc]);
    }
    imageBuffer_q31[idx] /= PIXELS_PER_NOTE;

    // 🔧 STEREO FIX: No color inversion here for stereo mode
    // Color inversion is already handled in
    // extractWarmChannel()/extractColdChannel() Only apply inversion for mono
    // mode
    if (!IS_STEREO_MODE()) {
      // Mono mode: apply inversion based on background color
      if (IS_WHITE_BACKGROUND()) {
        // White background mode: dark pixels = more energy
        imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[idx];
        if (imageBuffer_q31[idx] < 0)
          imageBuffer_q31[idx] = 0;
        if (imageBuffer_q31[idx] > VOLUME_AMP_RESOLUTION)
          imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION;
      }
      // Black background mode: bright pixels = more energy (no inversion
      // needed)
    }
    // Stereo mode: inversion already done in
    // extractWarmChannel()/extractColdChannel()
  }
  imageBuffer_q31[0] = 0; // Bug correction

#ifdef RELATIVE_MODE
  sub_int32((int32_t *)imageBuffer_q31, (int32_t *)&imageBuffer_q31[1],
            (int32_t *)imageBuffer_q31, NUMBER_OF_NOTES - 1);
  clip_int32((int32_t *)imageBuffer_q31, 0, VOLUME_AMP_RESOLUTION,
             NUMBER_OF_NOTES);
  imageBuffer_q31[NUMBER_OF_NOTES - 1] = 0;
#endif

  // Main note processing loop
  for (note = 0; note < NUMBER_OF_NOTES; note++) {
    imageBuffer_f32[note] = (float)imageBuffer_q31[note];

#if ENABLE_NON_LINEAR_MAPPING
    {
      float normalizedIntensity =
          imageBuffer_f32[note] / (float)VOLUME_AMP_RESOLUTION;
      float gamma = GAMMA_VALUE;
      normalizedIntensity = powf(normalizedIntensity, gamma);
      imageBuffer_f32[note] = normalizedIntensity * VOLUME_AMP_RESOLUTION;
    }
#endif

    // Generate waveforms using local state
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      new_idx = (local_current_idx[note] + waves[note].octave_coeff);
      if ((uint32_t)new_idx >= waves[note].area_size) {
        new_idx -= waves[note].area_size;
      }
      waveBuffer[buff_idx] = (*(waves[note].start_ptr + new_idx));
      local_current_idx[note] = new_idx; // Update local state
    }

#ifdef GAP_LIMITER
    // Gap limiter with local volume state
    float target_volume = imageBuffer_f32[note];
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      if (local_current_volume[note] < target_volume) {
        local_current_volume[note] += waves[note].volume_increment;
        if (local_current_volume[note] > target_volume) {
          local_current_volume[note] = target_volume;
        }
      } else {
        local_current_volume[note] -= waves[note].volume_decrement;
        if (local_current_volume[note] < target_volume) {
          local_current_volume[note] = target_volume;
        }
      }
      volumeBuffer[buff_idx] = local_current_volume[note];
    }
#else
    fill_float(imageBuffer_f32[note], volumeBuffer, AUDIO_BUFFER_SIZE);
#endif

    // Apply volume scaling
    mult_float(waveBuffer, volumeBuffer, waveBuffer, AUDIO_BUFFER_SIZE);

    for (buff_idx = AUDIO_BUFFER_SIZE; --buff_idx >= 0;) {
      if (volumeBuffer[buff_idx] > maxVolumeBuffer[buff_idx]) {
        maxVolumeBuffer[buff_idx] = volumeBuffer[buff_idx];
      }
    }

    // Accumulation
    add_float(waveBuffer, ifftBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
    add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer,
              AUDIO_BUFFER_SIZE);
  }

  // Final processing
  mult_float(ifftBuffer, maxVolumeBuffer, ifftBuffer, AUDIO_BUFFER_SIZE);
  scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);

  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    if (sumVolumeBuffer[buff_idx] != 0) {
      signal_R = (int32_t)(ifftBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
    } else {
      signal_R = 0;
    }
    audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;
  }

  // Apply contrast modulation
  float contrast_factor = calculate_contrast(imageData, CIS_MAX_PIXELS_NB);
  for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    audioData[buff_idx] *= contrast_factor;
  }

  // 🔍 DIAGNOSTIC LOGS for stereo mode
  static int stereo_log_counter = 0;
  if (stereo_log_counter % 120 ==
      0) { // Log every ~1 second at 48kHz/400 buffer
    float min_val = audioData[0], max_val = audioData[0], rms_sum = 0.0f;
    int clipped_count = 0;

    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
      float val = audioData[i];
      if (val < min_val)
        min_val = val;
      if (val > max_val)
        max_val = val;
      rms_sum += val * val;
      if (val >= 0.95f || val <= -0.95f)
        clipped_count++;
    }

    float rms = sqrtf(rms_sum / AUDIO_BUFFER_SIZE);
    printf("🎵 STEREO_STATELESS: min=%.6f, max=%.6f, rms=%.6f, clipped=%d/%d, "
           "contrast=%.3f\n",
           min_val, max_val, rms, clipped_count, AUDIO_BUFFER_SIZE,
           contrast_factor);
  }
  stereo_log_counter++;
}
