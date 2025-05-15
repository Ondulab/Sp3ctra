/* config.h */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/**************************************************************************************
 * Debug Definitions
 **************************************************************************************/
// #define PRINT_IFFT_FREQUENCY
// #define PRINT_IFFT_FREQUENCY_FULL

/**************************************************************************************
 * Mode Definitions
 **************************************************************************************/
/* CLI_MODE est défini soit ici, soit par le compilateur via -DCLI_MODE */
#ifndef CLI_MODE
#define CLI_MODE
#endif

/**************************************************************************************
 * CIS Definitions
 **************************************************************************************/
#define CIS_400DPI_PIXELS_NB (3456)
#define CIS_200DPI_PIXELS_NB (1728)

#define UDP_MAX_NB_PACKET_PER_LINE (12)
#define CIS_MAX_PIXELS_NB (CIS_400DPI_PIXELS_NB)

// Ensure UDP_LINE_FRAGMENT_SIZE is an integer
#if (CIS_MAX_PIXELS_NB % UDP_MAX_NB_PACKET_PER_LINE) != 0
#error "CIS_MAX_PIXELS_NB must be divisible by UDP_MAX_NB_PACKET_PER_LINE."
#endif

// Size of each UDP line fragment (number of pixels per packet)
#define UDP_LINE_FRAGMENT_SIZE (CIS_MAX_PIXELS_NB / UDP_MAX_NB_PACKET_PER_LINE)

#define PORT (55151) // Port for incoming data
#define DEFAULT_MULTI "192.168.0.1"
#define DEFAULT_PORT PORT

/**************************************************************************************
 * DMX Definitions
 **************************************************************************************/
#define USE_DMX

#define DMX_PORT "/dev/tty.usbserial-AD0JUL0N"
#define DMX_BAUD (250000)
#define DMX_CHANNELS (512)
#define DMX_FRAME_SIZE (DMX_CHANNELS + 1)

#define DMX_RED_FACTOR (1)
#define DMX_GREEN_FACTOR (1.5)
#define DMX_BLUE_FACTOR (1)

#define DMX_SATURATION_FACTOR (2.9) // 1.8 Color saturation factor

#define DMX_SMOOTHING_FACTOR (0.80)

#define DMX_GAMMA (1.2)

// Paramètres pour la courbe de réponse progressive
#define DMX_BLACK_THRESHOLD                                                    \
  (0.3) // Seuil en dessous duquel les LEDs restent éteintes (0-1)
#define DMX_RESPONSE_CURVE (2.5) // Contrôle l'inflexion de la courbe log/exp

// Paramètres pour la détection des éléments significatifs et le chevauchement
// des zones
#define DMX_ZONE_OVERLAP (0.15) // Facteur de chevauchement entre zones (0-1)
#define DMX_WHITE_THRESHOLD                                                    \
  (220) // Valeur en dessous de laquelle un pixel n'est pas considéré comme
        // blanc
#define DMX_SIGNIFICANT_WEIGHT                                                 \
  (5.0) // Poids des pixels non-blancs par rapport aux pixels blancs

// Paramètres pour la détection des blobs
#define DMX_MIN_BLOB_SIZE                                                      \
  (2) // Taille minimale d'un blob pour être considéré (en pixels)
#define DMX_COLOR_SIMILARITY_THRESHOLD                                         \
  (1000) // Seuil de similarité de couleur (distance euclidienne au carré)
#define DMX_MAX_BLOBS_PER_ZONE (15) // Nombre maximum de blobs par zone
#define DMX_MAX_ZONE_SIZE (500) // Taille maximum d'une zone avec chevauchement

// Paramètres pour la stabilisation des zones sombres/noires
#define DMX_LOW_INTENSITY_THRESHOLD                                            \
  (0.15) // Seuil d'intensité considéré comme "faible"
#define DMX_DARK_SMOOTHING_FACTOR                                              \
  (0.98) // Lissage plus fort pour les zones sombres (0-1)
#define DMX_UNIFORM_THRESHOLD                                                  \
  (8.0) // Seuil pour considérer une zone comme uniforme
#define DMX_MIN_STD_DEV                                                        \
  (0.03) // Écart-type minimal pour considérer des variations significatives

#define DMX_NUM_SPOTS (18) // Nombre de spots DMX à gérer

/**************************************************************************************
 * DAC Definitions
 **************************************************************************************/
#define SAMPLING_FREQUENCY (48000)
#define AUDIO_CHANNEL (2)
#define AUDIO_BUFFER_SIZE (256)

/**************************************************************************************
 * Image Definitions
 **************************************************************************************/
// Compilation-time switch for enabling image transformation
#define ENABLE_IMAGE_TRANSFORM 0

// Renamed constant for gamma correction
// #define IMAGE_GAMMA 2.2

/**************************************************************************************
 * Synth Definitions
 **************************************************************************************/
// Define waveform type (options: SIN, SAW, SQR)
#define SIN

#define GAP_LIMITER

#define COLOR_INVERTED

#define ENABLE_NON_LINEAR_MAPPING                                              \
  1 // Set to 1 to enable non-linear mapping, or 0 to disable

// Optional definitions (uncomment if needed)
// #define STEREO_1
// #define RELATIVE_MODE

#define PI (3.14159265358979323846)
#define TWO_PI (PI * 2)
/**************************************************************************************
 * Wave Generation Definitions
 **************************************************************************************/
#define WAVE_AMP_RESOLUTION (16777215) // Decimal value
#define VOLUME_AMP_RESOLUTION (65535)  // Decimal value
#define START_FREQUENCY (65.41)
#define MAX_OCTAVE_NUMBER (8) // >> le nb d'octaves n'a pas d'incidence ?
#define SEMITONE_PER_OCTAVE (12)
#define COMMA_PER_SEMITONE (36)

#define VOLUME_INCREMENT (1)
#define VOLUME_DECREMENT (1)

#define PIXELS_PER_NOTE (1)
#define NUMBER_OF_NOTES (CIS_MAX_PIXELS_NB / PIXELS_PER_NOTE)

/**************************************************************************************
 * Display Definitions
 **************************************************************************************/
#define WINDOWS_WIDTH (CIS_MAX_PIXELS_NB)
#define WINDOWS_HEIGHT (1160)

#endif // __CONFIG_H__
