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
#include "image_debug.h"
#include "lock_free_pan.h"
#include "../../config/config_debug.h"
#include "../../config/config_synth_additive.h"

// External declaration for debug configuration
#ifdef DEBUG_OSC
extern debug_additive_osc_config_t g_debug_osc_config;
#endif

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
// Mutex to ensure thread-safe synthesis processing for stereo channels
static pthread_mutex_t g_synth_process_mutex;

// Variables pour la limitation des logs (affichage périodique)
static uint32_t log_counter = 0;
#define LOG_FREQUENCY                                                          \
  (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE) // Environ 1 seconde

// static volatile int32_t *half_audio_ptr; // Unused variable
// static volatile int32_t *full_audio_ptr; // Unused variable
static int32_t imageRef[NUMBER_OF_NOTES] = {0};

// Global filter instance for mono mode only (stereo uses per-oscillator panning now)
// static ImageTemporalFilter mono_filter = {0}; // Commented out - not used in current implementation

/* Variable used to get converted value */
// ToChange__IO uint16_t uhADCxConvertedValue = 0;

/* Private function prototypes -----------------------------------------------*/
static uint32_t greyScale(uint8_t *buffer_R, uint8_t *buffer_G,
                          uint8_t *buffer_B, int32_t *gray, uint32_t size);
void synth_IfftMode(int32_t *imageData, float *audioDataLeft, float *audioDataRight, float contrast_factor);

static float calculate_contrast(int32_t *imageData, size_t size);
static float calculate_color_temperature(uint8_t r, uint8_t g, uint8_t b);
static void calculate_pan_gains(float pan_position, float *left_gain, float *right_gain);

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
  wavesGeneratorParams.startFrequency = (uint32_t)START_FREQUENCY; // Cast to uint32_t
  wavesGeneratorParams.harmonization = MAJOR;
  wavesGeneratorParams.harmonizationLevel = 100;
  wavesGeneratorParams.waveformOrder = 1;

  buffer_len = init_waves(unitary_waveform, waves, &wavesGeneratorParams); // 24002070 24000C30

  int32_t value = VOLUME_INCREMENT;

  if (value < 1)
    value = 1;
  if (value > 100000)
    value = 100000;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_increment =
        1.00 / (float)value * waves[note].max_volume_increment;
  }

  value = VOLUME_DECREMENT;

  if (value < 1)
    value = 1;
  if (value > 100000)
    value = 100000;
  for (int32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    waves[note].volume_decrement = 1.00 / (float)value * waves[note].max_volume_decrement;
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

  // Initialize image debug system
  image_debug_init();

  // Initialize the global synthesis mutex
  if (pthread_mutex_init(&g_synth_process_mutex, NULL) != 0) {
      perror("Failed to initialize synth process mutex");
      die("synth init failed");
      return -1;
  }

#ifdef STEREO_MODE
  // Initialize lock-free pan gains system
  lock_free_pan_init();
  printf("🔧 LOCK_FREE_PAN: System initialized for stereo mode\n");
#endif

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

  // Échantillonnage - ne traite pas tous les pixels pour optimiser
  // performance
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
 * @brief Calculate color temperature from RGB values (AGGRESSIVE VERSION)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @retval Pan position from -1.0 (warm/left) to +1.0 (cold/right)
 */
static float calculate_color_temperature(uint8_t r, uint8_t g, uint8_t b) {
  // Convert RGB to normalized values
  float r_norm = r / 255.0f;
  float g_norm = g / 255.0f;
  float b_norm = b / 255.0f;
  
  // AGGRESSIVE ALGORITHM: Direct blue-red comparison for maximum stereo effect
  // Blue/Cyan = cold (right), Red/Yellow = warm (left)
  
  // Primary cold/warm axis: Blue vs Red (most important) - INVERTED
  float blue_red_diff = b_norm - r_norm;
  
  // Secondary axis: Cyan (G+B) vs Yellow (R+G) - INVERTED
  float cyan_strength = (g_norm + b_norm) * 0.5f;
  float yellow_strength = (r_norm + g_norm) * 0.5f;
  float cyan_yellow_diff = cyan_strength - yellow_strength;
  
  // Combine with heavy weight on blue-red axis
  float temperature = blue_red_diff * 0.8f + cyan_yellow_diff * 0.2f;
  
  // AGGRESSIVE AMPLIFICATION: Make the effect much more pronounced
  temperature *= 2.5f;  // Amplify the base signal
  
  // Apply aggressive non-linear curve to push values toward extremes
  if (temperature > 0) {
    temperature = powf(temperature, 0.6f);  // Compress less, keep more dynamic range
  } else {
    temperature = -powf(-temperature, 0.6f);
  }
  
  // Hard clamp to [-1, 1] range
  if (temperature > 1.0f) temperature = 1.0f;
  if (temperature < -1.0f) temperature = -1.0f;
  
  return temperature;
}

/**
 * @brief Calculate stereo pan gains using constant power law
 * @param pan_position Pan position from -1.0 (left) to +1.0 (right)
 * @param left_gain Output left channel gain (0.0 to 1.0)
 * @param right_gain Output right channel gain (0.0 to 1.0)
 * @retval None
 */
static void calculate_pan_gains(float pan_position, float *left_gain, float *right_gain) {
  // Ensure pan position is in valid range
  if (pan_position < -1.0f) pan_position = -1.0f;
  if (pan_position > 1.0f) pan_position = 1.0f;
  
#if STEREO_PAN_LAW_CONSTANT_POWER
  // Constant power panning law (sin/cos curves)
  // Convert pan position to angle (0 to PI/2)
  float angle = (pan_position + 1.0f) * 0.25f * M_PI;
  
  // Calculate gains using trigonometric functions
  *left_gain = cosf(angle);
  *right_gain = sinf(angle);
  
  // Apply center compensation to maintain perceived loudness
  // At center (pan=0), both gains would be 0.707, boost slightly
  if (fabsf(pan_position) < 0.1f) {
    float center_boost = 1.02f;
    *left_gain *= center_boost;
    *right_gain *= center_boost;
  }
#else
  // Linear panning law (simpler but less perceptually uniform)
  *left_gain = (1.0f - pan_position) * 0.5f;
  *right_gain = (1.0f + pan_position) * 0.5f;
#endif
  
  // Ensure gains are in valid range
  if (*left_gain > 1.0f) *left_gain = 1.0f;
  if (*left_gain < 0.0f) *left_gain = 0.0f;
  if (*right_gain > 1.0f) *right_gain = 1.0f;
  if (*right_gain < 0.0f) *right_gain = 0.0f;
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
  float thread_additiveBuffer[AUDIO_BUFFER_SIZE];
  float thread_sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  float thread_maxVolumeBuffer[AUDIO_BUFFER_SIZE];
  
  // Buffers stéréo pour accumulation directe L/R (toujours présents)
  // En mode mono : L = R = signal dupliqué
  // En mode stéréo : L et R avec panoramisation par oscillateur
  float thread_additiveBuffer_L[AUDIO_BUFFER_SIZE];
  float thread_additiveBuffer_R[AUDIO_BUFFER_SIZE];

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
  
  // Pan positions et gains pré-calculés pour chaque note
  float precomputed_pan_position[NUMBER_OF_NOTES / 3 + 100];
  float precomputed_left_gain[NUMBER_OF_NOTES / 3 + 100];
  float precomputed_right_gain[NUMBER_OF_NOTES / 3 + 100];

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

// Mutex global pour protéger l'accès aux données waves[] pendant le
// pré-calcul
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

    // CRITICAL FIX: Initialize all buffers to zero to prevent garbage values
    memset(worker->thread_additiveBuffer, 0, sizeof(worker->thread_additiveBuffer));
    memset(worker->thread_sumVolumeBuffer, 0, sizeof(worker->thread_sumVolumeBuffer));
    memset(worker->thread_maxVolumeBuffer, 0, sizeof(worker->thread_maxVolumeBuffer));
    
    // CRITICAL FIX: Initialize stereo buffers to zero (always present)
    memset(worker->thread_additiveBuffer_L, 0, sizeof(worker->thread_additiveBuffer_L));
    memset(worker->thread_additiveBuffer_R, 0, sizeof(worker->thread_additiveBuffer_R));

    // Initialize work buffers
    memset(worker->imageBuffer_q31, 0, sizeof(worker->imageBuffer_q31));
    memset(worker->imageBuffer_f32, 0, sizeof(worker->imageBuffer_f32));
    memset(worker->waveBuffer, 0, sizeof(worker->waveBuffer));
    memset(worker->volumeBuffer, 0, sizeof(worker->volumeBuffer));
    
    // Initialize precomputed data arrays
    memset(worker->precomputed_new_idx, 0, sizeof(worker->precomputed_new_idx));
    memset(worker->precomputed_wave_data, 0, sizeof(worker->precomputed_wave_data));
    memset(worker->precomputed_volume, 0, sizeof(worker->precomputed_volume));
    memset(worker->precomputed_volume_increment, 0, sizeof(worker->precomputed_volume_increment));
    memset(worker->precomputed_volume_decrement, 0, sizeof(worker->precomputed_volume_decrement));
    
    memset(worker->precomputed_pan_position, 0, sizeof(worker->precomputed_pan_position));
    memset(worker->precomputed_left_gain, 0, sizeof(worker->precomputed_left_gain));
    memset(worker->precomputed_right_gain, 0, sizeof(worker->precomputed_right_gain));

    // Initialisation de la synchronisation
    if (pthread_mutex_init(&worker->work_mutex, NULL) != 0) {
      printf("Erreur lors de l'initialisation du mutex pour le thread %d\n", i);
      return -1;
    }
    if (pthread_cond_init(&worker->work_cond, NULL) != 0) {
      printf("Erreur lors de l'initialisation de la condition pour le thread "
             "%d\n",
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

  // Initialiser les buffers de sortie à zéro
  fill_float(0, worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_sumVolumeBuffer, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_maxVolumeBuffer, AUDIO_BUFFER_SIZE);
  
  // Initialize stereo buffers - CRITICAL FIX: must zero these buffers! (always present)
  fill_float(0, worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
  fill_float(0, worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);

  // Prétraitement: calcul des moyennes et transformation en imageBuffer_q31
  for (idx = worker->start_note; idx < worker->end_note; idx++) {
    local_note_idx = idx - worker->start_note;
    worker->imageBuffer_q31[local_note_idx] = 0;

    for (acc = 0; acc < PIXELS_PER_NOTE; acc++) {
      worker->imageBuffer_q31[local_note_idx] +=
          (worker->imageData[idx * PIXELS_PER_NOTE + acc]);
    }

    worker->imageBuffer_q31[local_note_idx] /= PIXELS_PER_NOTE;

    // Apply color inversion (dark pixels = more energy)
    worker->imageBuffer_q31[local_note_idx] =
        VOLUME_AMP_RESOLUTION - worker->imageBuffer_q31[local_note_idx];
    if (worker->imageBuffer_q31[local_note_idx] < 0)
      worker->imageBuffer_q31[local_note_idx] = 0;
    if (worker->imageBuffer_q31[local_note_idx] > VOLUME_AMP_RESOLUTION)
      worker->imageBuffer_q31[local_note_idx] = VOLUME_AMP_RESOLUTION;
    // The comment below is a leftover from a previous implementation and is no longer true.
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
    // Set the target volume for the oscillator
    waves[note].target_volume = worker->imageBuffer_f32[local_note_idx];


    // Apply volume ramp per sample
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (waves[note].current_volume < waves[note].target_volume) {
            waves[note].current_volume += waves[note].volume_increment;
            if (waves[note].current_volume > waves[note].target_volume) {
                waves[note].current_volume = waves[note].target_volume;
            }
        } else if (waves[note].current_volume > waves[note].target_volume) {
            waves[note].current_volume -= waves[note].volume_decrement;
            if (waves[note].current_volume < waves[note].target_volume) {
                waves[note].current_volume = waves[note].target_volume;
            }
        }
        worker->volumeBuffer[buff_idx] = waves[note].current_volume;

#ifdef DEBUG_OSC
        // Debug: Print oscillator values for specified range/single oscillator
        if (g_debug_osc_config.enabled) {
          int should_debug = 0;
          
          if (g_debug_osc_config.single_osc >= 0) {
            // Single oscillator mode
            should_debug = (note == g_debug_osc_config.single_osc);
          } else {
            // Range mode
            should_debug = (note >= g_debug_osc_config.start_osc && note <= g_debug_osc_config.end_osc);
          }
          
          if (should_debug) {
            printf("[DEBUG_OSC_%d] sample=%d target=%.1f current=%.1f inc=%.3f dec=%.3f max_inc=%.3f max_dec=%.3f freq=%.1f\n",
                   note,
                   buff_idx,
                   waves[note].target_volume,
                   waves[note].current_volume,
                   waves[note].volume_increment,
                   waves[note].volume_decrement,
                   waves[note].max_volume_increment,
                   waves[note].max_volume_decrement,
                   waves[note].frequency);
            fflush(stdout);
          }
        }
#endif
    }

#else
    fill_float(worker->imageBuffer_f32[local_note_idx], worker->volumeBuffer,
               AUDIO_BUFFER_SIZE);
#endif // GAP_LIMITER

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

    // Always fill stereo buffers (unified approach)
#ifdef STEREO_MODE
    // Stereo mode: Apply per-oscillator panning and accumulate to L/R buffers
    float left_gain = worker->precomputed_left_gain[local_note_idx];
    float right_gain = worker->precomputed_right_gain[local_note_idx];
    
    // Create temporary buffers for L/R channels
    float waveBuffer_L[AUDIO_BUFFER_SIZE];
    float waveBuffer_R[AUDIO_BUFFER_SIZE];
    
    // Apply panning gains
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      waveBuffer_L[buff_idx] = worker->waveBuffer[buff_idx] * left_gain;
      waveBuffer_R[buff_idx] = worker->waveBuffer[buff_idx] * right_gain;
    }
    
    // Accumulate to stereo buffers
    add_float(waveBuffer_L, worker->thread_additiveBuffer_L,
              worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
    add_float(waveBuffer_R, worker->thread_additiveBuffer_R,
              worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);
#else
    // Mono mode: Duplicate mono signal to both L/R channels (center panning)
    // This creates a unified architecture where stereo buffers are always filled
    add_float(worker->waveBuffer, worker->thread_additiveBuffer_L,
              worker->thread_additiveBuffer_L, AUDIO_BUFFER_SIZE);
    add_float(worker->waveBuffer, worker->thread_additiveBuffer_R,
              worker->thread_additiveBuffer_R, AUDIO_BUFFER_SIZE);
#endif

    // Additive summation for mono or combined processing
    add_float(worker->waveBuffer, worker->thread_additiveBuffer,
              worker->thread_additiveBuffer, AUDIO_BUFFER_SIZE);
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
      // ✅ GAP_LIMITER: Ne pas pré-calculer le volume - les threads
      // l'accèdent directement Les paramètres increment/decrement sont
      // thread-safe en lecture seule
      worker->precomputed_volume_increment[local_note_idx] =
          waves[note].volume_increment;
      worker->precomputed_volume_decrement[local_note_idx] =
          waves[note].volume_decrement;
#endif

#ifdef STEREO_MODE
      // Use lock-free pan system to get current gains
      float left_gain, right_gain, pan_position;
      lock_free_pan_read(note, &left_gain, &right_gain, &pan_position);
      
      worker->precomputed_pan_position[local_note_idx] = pan_position;
      worker->precomputed_left_gain[local_note_idx] = left_gain;
      worker->precomputed_right_gain[local_note_idx] = right_gain;
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

#ifdef STEREO_MODE
  // Cleanup lock-free pan gains system
  lock_free_pan_cleanup();
  printf("🔧 LOCK_FREE_PAN: System cleaned up\n");
#endif

  synth_pool_initialized = 0;
}

/**
 * @brief  Optimized version of the Additive synthesis with a persistent
 * thread pool
 * @param  imageData Grayscale input data
 * @param  audioDataLeft Buffer de sortie audio canal gauche (stereo mode)
 * @param  audioDataRight Buffer de sortie audio canal droit (stereo mode)
 * @param  audioData Buffer de sortie audio mono (mono mode)
 * @retval None
 */
void synth_IfftMode(int32_t *imageData, float *audioDataLeft, float *audioDataRight, float contrast_factor) {

  // Additive mode (limited logs)
  if (log_counter % LOG_FREQUENCY == 0) {
    // printf("===== Additive Mode called (optimized) =====\n");
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
      printf("Erreur lors de l'initialisation du pool, mode séquentiel "
             "activé\n");
      synth_pool_initialized = 0;
    }
    first_call = 0;
  }

  // Buffers finaux pour les résultats combinés
  static float additiveBuffer[AUDIO_BUFFER_SIZE];
  static float sumVolumeBuffer[AUDIO_BUFFER_SIZE];
  static float maxVolumeBuffer[AUDIO_BUFFER_SIZE];

  // Réinitialiser les buffers finaux
  fill_float(0, additiveBuffer, AUDIO_BUFFER_SIZE);
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

    // Phase 4: Combiner les résultats des threads avec normalisation
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_additiveBuffer, additiveBuffer,
                additiveBuffer, AUDIO_BUFFER_SIZE);
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


    //  CORRECTION: Normalisation conditionnelle par plateforme
#ifdef __linux__
    // Pi/Linux : Diviser par 3 (BossDAC/ALSA amplifie naturellement)
    scale_float(additiveBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(sumVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
    scale_float(maxVolumeBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
#else
    // Mac : Pas de division (CoreAudio ne compense pas automatiquement)
    // Signal gardé à pleine amplitude pour volume normal
#endif

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

      // Apply color inversion (white background is implicit)
      // White background mode: dark pixels = more energy
      imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION - imageBuffer_q31[idx];
      if (imageBuffer_q31[idx] < 0)
        imageBuffer_q31[idx] = 0;
      if (imageBuffer_q31[idx] > VOLUME_AMP_RESOLUTION)
        imageBuffer_q31[idx] = VOLUME_AMP_RESOLUTION;
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
      // Set the target volume for the oscillator
      waves[note].target_volume = imageBuffer_f32[note];


      // Apply volume ramp per sample
      for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (waves[note].current_volume < waves[note].target_volume) {
          waves[note].current_volume += waves[note].volume_increment;
          if (waves[note].current_volume > waves[note].target_volume) {
            waves[note].current_volume = waves[note].target_volume;
          }
        } else if (waves[note].current_volume > waves[note].target_volume) {
          waves[note].current_volume -= waves[note].volume_decrement;
          if (waves[note].current_volume < waves[note].target_volume) {
            waves[note].current_volume = waves[note].target_volume;
          }
        }
        volumeBuffer[buff_idx] = waves[note].current_volume;

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
      add_float(waveBuffer, additiveBuffer, additiveBuffer, AUDIO_BUFFER_SIZE);
      add_float(volumeBuffer, sumVolumeBuffer, sumVolumeBuffer,
                AUDIO_BUFFER_SIZE);
    }
  }

  // === PHASE FINALE (commune aux deux modes) ===
  mult_float(additiveBuffer, maxVolumeBuffer, additiveBuffer,
             AUDIO_BUFFER_SIZE);
  scale_float(sumVolumeBuffer, VOLUME_AMP_RESOLUTION / 2, AUDIO_BUFFER_SIZE);

    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (sumVolumeBuffer[buff_idx] != 0) {
            signal_R =
                (int32_t)(additiveBuffer[buff_idx] / (sumVolumeBuffer[buff_idx]));
        } else {
            signal_R = 0;
        }
        tmp_audioData[buff_idx] = signal_R / (float)WAVE_AMP_RESOLUTION;

#ifdef DEBUG_OSCILLATOR_VOLUMES
        // Capture oscillator volumes for each audio sample (48kHz resolution)
        image_debug_capture_oscillator_sample();
#endif
    }

  // Le facteur de contraste est maintenant passé en paramètre depuis synth_AudioProcess

  // Apply contrast modulation and unified stereo output
  if (synth_pool_initialized && !synth_pool_shutdown) {
#ifdef STEREO_MODE
    // STEREO MODE: Use actual stereo buffers from threads
    // Combine stereo buffers from all threads
    static float stereoBuffer_L[AUDIO_BUFFER_SIZE];
    static float stereoBuffer_R[AUDIO_BUFFER_SIZE];
    
    // Initialize stereo buffers
    fill_float(0, stereoBuffer_L, AUDIO_BUFFER_SIZE);
    fill_float(0, stereoBuffer_R, AUDIO_BUFFER_SIZE);
    
    // Accumulate stereo buffers from all threads
    for (int i = 0; i < 3; i++) {
      add_float(thread_pool[i].thread_additiveBuffer_L, stereoBuffer_L,
                stereoBuffer_L, AUDIO_BUFFER_SIZE);
      add_float(thread_pool[i].thread_additiveBuffer_R, stereoBuffer_R,
                stereoBuffer_R, AUDIO_BUFFER_SIZE);
    }
    
    // Apply same normalization as mono signal
    mult_float(stereoBuffer_L, maxVolumeBuffer, stereoBuffer_L, AUDIO_BUFFER_SIZE);
    mult_float(stereoBuffer_R, maxVolumeBuffer, stereoBuffer_R, AUDIO_BUFFER_SIZE);
    
    // Apply final processing and contrast
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float left_signal, right_signal;
      
      if (sumVolumeBuffer[buff_idx] != 0) {
        left_signal = (stereoBuffer_L[buff_idx] / sumVolumeBuffer[buff_idx]) / (float)WAVE_AMP_RESOLUTION;
        right_signal = (stereoBuffer_R[buff_idx] / sumVolumeBuffer[buff_idx]) / (float)WAVE_AMP_RESOLUTION;
      } else {
        left_signal = 0;
        right_signal = 0;
      }
      
      // Apply contrast factor
      audioDataLeft[buff_idx] = left_signal * contrast_factor;
      audioDataRight[buff_idx] = right_signal * contrast_factor;

      // Apply final hard limiting
      if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
      if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
      if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
      if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
    }

#else
    // MONO MODE: Use original simple processing and duplicate output
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float mono_sample = tmp_audioData[buff_idx] * contrast_factor;
      
      // Duplicate mono sample to both channels
      audioDataLeft[buff_idx] = mono_sample;
      audioDataRight[buff_idx] = mono_sample;

      // Apply final hard limiting
      if (audioDataLeft[buff_idx] > 1.0f) audioDataLeft[buff_idx] = 1.0f;
      if (audioDataLeft[buff_idx] < -1.0f) audioDataLeft[buff_idx] = -1.0f;
      if (audioDataRight[buff_idx] > 1.0f) audioDataRight[buff_idx] = 1.0f;
      if (audioDataRight[buff_idx] < -1.0f) audioDataRight[buff_idx] = -1.0f;
    }
#endif
  } else {
    // Fallback mode
    // Fallback: simple processing from mono data
#ifdef STEREO_MODE
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float mono_sample = tmp_audioData[buff_idx] * contrast_factor;
      audioDataLeft[buff_idx] = mono_sample * 0.707f;  // Center pan
      audioDataRight[buff_idx] = mono_sample * 0.707f; // Center pan
    }
#else
    for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
      float mono_sample = tmp_audioData[buff_idx] * contrast_factor;
      audioDataLeft[buff_idx] = mono_sample;
      audioDataRight[buff_idx] = mono_sample;
    }
#endif
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

#ifdef DEBUG_OSC
  // Debug: Print "NEW IMAGE LINE" when a new image is processed
  if (g_debug_osc_config.enabled) {
    printf("\n=== NEW IMAGE LINE ===\n");
    fflush(stdout);
  }
#endif

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

#ifdef STEREO_MODE
  // Calculate color temperature and pan positions for each oscillator
  // This is done once per image reception for efficiency
  for (uint32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    // Calculate average color for this note's pixels
    uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
    uint32_t pixel_count = 0;
    
    for (uint32_t pix = 0; pix < PIXELS_PER_NOTE; pix++) {
      uint32_t pixel_idx = note * PIXELS_PER_NOTE + pix;
      if (pixel_idx < CIS_MAX_PIXELS_NB) {
        r_sum += buffer_R[pixel_idx];
        g_sum += buffer_G[pixel_idx];
        b_sum += buffer_B[pixel_idx];
        pixel_count++;
      }
    }
    
    if (pixel_count > 0) {
      // Calculate average RGB values
      uint8_t r_avg = r_sum / pixel_count;
      uint8_t g_avg = g_sum / pixel_count;
      uint8_t b_avg = b_sum / pixel_count;
      
      // Calculate color temperature and pan position
      float temperature = calculate_color_temperature(r_avg, g_avg, b_avg);
      waves[note].pan_position = temperature;
      
      // Use temporary variables to avoid volatile qualifier warnings
      float temp_left_gain, temp_right_gain;
      calculate_pan_gains(temperature, &temp_left_gain, &temp_right_gain);
      waves[note].left_gain = temp_left_gain;
      waves[note].right_gain = temp_right_gain;
      
      // Debug output for first few notes (limited frequency)
      if (log_counter % (LOG_FREQUENCY * 10) == 0 && note < 5) {
#ifdef DEBUG_RGB_TEMPERATURE
        printf("Note %d: RGB(%d,%d,%d) -> Temp=%.2f L=%.2f R=%.2f\n",
               note, r_avg, g_avg, b_avg, temperature,
               waves[note].left_gain, waves[note].right_gain);
#endif
      }
    } else {
      // Default to center if no pixels
      waves[note].pan_position = 0.0f;
      waves[note].left_gain = 0.707f;
      waves[note].right_gain = 0.707f;
    }
  }
  
  // Update lock-free pan gains system with calculated values
  // Prepare arrays for batch update
  static float left_gains[NUMBER_OF_NOTES];
  static float right_gains[NUMBER_OF_NOTES];
  static float pan_positions[NUMBER_OF_NOTES];
  
  for (uint32_t note = 0; note < NUMBER_OF_NOTES; note++) {
    left_gains[note] = waves[note].left_gain;
    right_gains[note] = waves[note].right_gain;
    pan_positions[note] = waves[note].pan_position;
  }
  
  // Atomic update of all pan gains
  lock_free_pan_update(left_gains, right_gains, pan_positions, NUMBER_OF_NOTES);
#endif // STEREO_MODE

#ifdef ENABLE_IMAGE_DEBUG
  // Capture mono pipeline for debug visualization
  static int debug_frame_counter_mono = 0;
  image_debug_capture_mono_pipeline(buffer_R, buffer_G, buffer_B,
                                   g_grayScale_live, processed_grayScale,
                                   debug_frame_counter_mono++);
#endif

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
                  G_SYNTH_DATA_FADE_DURATION_SECONDS); // Alpha from 0
                                                       // (frozen) to 1 (live)
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

  // Calculate contrast factor based on the processed grayscale image
  // This optimization moves the contrast calculation from synth_IfftMode to here
  // for better performance (calculated once per image instead of per audio buffer)
  float contrast_factor = calculate_contrast(processed_grayScale, CIS_MAX_PIXELS_NB);

  // Lancer la synthèse avec les données potentiellement gelées/fondues
  // Unified mode: always pass both left and right buffers
  synth_IfftMode(processed_grayScale,
                 buffers_L[index].data,
                 buffers_R[index].data,
                 contrast_factor);

  // Mettre à jour les buffers d'affichage globaux avec les données couleur
  // originales
  pthread_mutex_lock(&g_displayable_synth_mutex);
  memcpy(g_displayable_synth_R, buffer_R, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_G, buffer_G, CIS_MAX_PIXELS_NB);
  memcpy(g_displayable_synth_B, buffer_B, CIS_MAX_PIXELS_NB);
  pthread_mutex_unlock(&g_displayable_synth_mutex);
  // Additive synthesis finished
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

  // Marquer les buffers comme prêts
  pthread_mutex_lock(&buffers_L[index].mutex);
  buffers_L[index].ready = 1;
  pthread_cond_signal(&buffers_L[index].cond);
  pthread_mutex_unlock(&buffers_L[index].mutex);

  pthread_mutex_lock(&buffers_R[index].mutex);
  buffers_R[index].ready = 1;
  pthread_cond_signal(&buffers_R[index].cond);
  pthread_mutex_unlock(&buffers_R[index].mutex);

  // Changer l'indice pour que le callback lise le buffer rempli et que le
  // prochain écriture se fasse sur l'autre buffer
  __atomic_store_n(&current_buffer_index, 1 - index, __ATOMIC_RELEASE);
}
