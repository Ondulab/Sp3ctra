/* config.h */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/**************************************************************************************
 * Debug Definitions
 **************************************************************************************/
// #define PRINT_IFFT_FREQUENCY
// #define PRINT_IFFT_FREQUENCY_FULL
// #define DEBUG_MIDI
// #define DEBUG_UDP // Uncomment to enable verbose UDP logging
// #define DEBUG_BUFFERS // Uncomment to enable verbose buffer swap logging
// #define DEBUG_AUTO_VOLUME // Enable auto-volume debug logging
// #define DEBUG_IMU_PACKETS     // Enable IMU packet reception logging
// #define DEBUG_AUDIO_INTERFACE // Enable audio interface debug logging

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
 * DAC Definitions - Optimized for Raspberry Pi Module 5
 **************************************************************************************/
#define SAMPLING_FREQUENCY (48000)
#define AUDIO_CHANNEL (2)

// Buffer size optimized for Pi Module 5 with real-time synthesis
// Larger buffer reduces audio dropouts during intensive FFT processing
// 48kHz: 150 frames = 3.125ms latency (optimal for real-time)
// 96kHz: 600 frames = 6.25ms latency (double latency for synthesis headroom)
#if SAMPLING_FREQUENCY >= 96000
#define AUDIO_BUFFER_SIZE (250)
#elif SAMPLING_FREQUENCY >= 48000
#define AUDIO_BUFFER_SIZE (400) // 150
#else
#define AUDIO_BUFFER_SIZE (128)
#endif

// Automatic cache sizing for smooth volume transitions in audio callback
// Target: ~2% of buffer size for imperceptible volume steps
// This ensures smooth volume changes regardless of buffer size
#define AUDIO_CACHE_UPDATE_FREQUENCY                                           \
  ((AUDIO_BUFFER_SIZE * 2) / 100) // 2% of buffer size

// Ensure minimum of 4 and maximum of 32 for performance and stability
#define AUDIO_CACHE_UPDATE_FREQUENCY_CLAMPED                                   \
  ((AUDIO_CACHE_UPDATE_FREQUENCY < 4)    ? 4                                   \
   : (AUDIO_CACHE_UPDATE_FREQUENCY > 32) ? 32                                  \
                                         : AUDIO_CACHE_UPDATE_FREQUENCY)

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

/* Auto-volume (IMU X) configuration - tune these values on site */
#define IMU_ACTIVE_THRESHOLD_X                                                 \
  (0.02f) /* Threshold on accel X to consider active (sensor units) */
#define IMU_FILTER_ALPHA_X                                                     \
  (0.25f) /* Exponential smoothing alpha for acc X (0..1) */
#define IMU_INACTIVITY_TIMEOUT_S                                               \
  (5) /* Seconds of no activity before dimming                                 \
       */
#define AUTO_VOLUME_INACTIVE_LEVEL                                             \
  (0.09f) /* Target volume when inactive (0.0..1.0) */
#define AUTO_VOLUME_ACTIVE_LEVEL                                               \
  (1.0f)                          /* Target volume when active (0.0..1.0) */
#define AUTO_VOLUME_FADE_MS (600) /* Fade duration in milliseconds */
#define AUTO_VOLUME_POLL_MS (10)  /* How often auto-volume updates (ms) */
#define AUTO_VOLUME_DISABLE_WITH_MIDI                                          \
  1 /* If 1, disable auto-dim when MIDI controller connected */

#define ENABLE_NON_LINEAR_MAPPING                                              \
  1 // Set to 1 to enable non-linear mapping, or 0 to disable

/**************************************************************************************
 * Synthesis Mode Configuration - Resource Optimization
 **************************************************************************************/
// Manual control flags (highest priority)
#define FORCE_DISABLE_FFT 0  // Set to 1 to force disable FFT synthesis
#define FORCE_DISABLE_IFFT 0 // Set to 1 to force disable IFFT synthesis

// Automatic optimization flags
#define AUTO_DISABLE_FFT_WITHOUT_MIDI 1 // Auto-disable FFT if no MIDI detected

// MIDI polling optimization
#define ENABLE_MIDI_POLLING 1 // Set to 0 to disable MIDI polling entirely

/**************************************************************************************
 * Synth and Image Processing Configuration
 **************************************************************************************/

// Image Processing and Contrast Modulation
#define CONTRAST_MIN 0.00f   // Minimum volume for blurred images (0.0 to 1.0)
#define CONTRAST_STRIDE 4.0f // Pixel sampling stride for optimization
#define CONTRAST_ADJUSTMENT_POWER                                              \
  1.5f // Exponent for adjusting the contrast curve

// Non-Linear Intensity Mapping
#define GAMMA_VALUE 4.8f // Gamma value for non-linear intensity correction

// Logging Parameters
#define LOG_FREQUENCY                                                          \
  (SAMPLING_FREQUENCY /                                                        \
   AUDIO_BUFFER_SIZE) // Approximate logging frequency in Hz

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
 * Audio Effects Definitions
 **************************************************************************************/
// Reverb Configuration
#define ENABLE_REVERB 0 // Set to 1 to enable reverb, 0 to disable
#define DEFAULT_REVERB_MIX                                                     \
  0.0f // Default dry/wet mix (0.0 - 1.0) - 0 = no reverb
#define DEFAULT_REVERB_ROOM_SIZE 0.7f // Default room size (0.0 - 1.0)
#define DEFAULT_REVERB_DAMPING 0.5f   // Default damping (0.0 - 1.0)
#define DEFAULT_REVERB_WIDTH 1.0f     // Default stereo width (0.0 - 1.0)
#define DEFAULT_REVERB_PREDELAY                                                \
  0.02f // Default pre-delay in seconds (0.0 - 0.1)

// Advanced reverb parameters for Zita-Rev1 algorithm
#define DEFAULT_REVERB_RT_LOW 3.0f   // Low frequency reverb time (seconds)
#define DEFAULT_REVERB_RT_MID 2.0f   // Mid frequency reverb time (seconds)
#define DEFAULT_REVERB_FDAMP 3000.0f // High frequency damping frequency (Hz)
#define DEFAULT_REVERB_XOVER 200.0f  // Crossover frequency (Hz)
#define DEFAULT_REVERB_OPMIX 0.0f    // Output mix parameter (0.0 - 1.0)
#define DEFAULT_REVERB_RGXYZ 0.0f    // Ambisonic parameter (-1.0 - 1.0)

/**************************************************************************************
 * Display Definitions
 **************************************************************************************/
#define WINDOWS_WIDTH (CIS_MAX_PIXELS_NB)
#define WINDOWS_HEIGHT (1160)

#endif // __CONFIG_H__
