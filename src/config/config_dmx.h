/* config_dmx.h */

#ifndef __CONFIG_DMX_H__
#define __CONFIG_DMX_H__

/**************************************************************************************
 * DMX Definitions
 **************************************************************************************/
#define USE_DMX

#ifdef __APPLE__
#define DMX_PORT                     "/dev/tty.usbserial-AD0JUL0N"
#else
#define DMX_PORT                     "/dev/sp3ctra-dmx"
#endif
#define DMX_BAUD                     (250000)
#define DMX_CHANNELS                 (512)
#define DMX_FRAME_SIZE               (DMX_CHANNELS + 1)

#define DMX_RED_FACTOR               (1)
#define DMX_GREEN_FACTOR             (1.5)
#define DMX_BLUE_FACTOR              (1)

#define DMX_SATURATION_FACTOR        (2.9)                  // 1.8 Color saturation factor

#define DMX_SMOOTHING_FACTOR         (0.80)

#define DMX_GAMMA                    (1.2)

// Parameters for progressive response curve
#define DMX_BLACK_THRESHOLD          (0.3)                  // Threshold below which LEDs remain off (0-1)
#define DMX_RESPONSE_CURVE           (2.5)                  // Controls log/exp curve inflection

// Parameters for significant element detection and overlap
// des zones
#define DMX_ZONE_OVERLAP             (0.15)                 // Facteur de chevauchement entre zones (0-1)
#define DMX_WHITE_THRESHOLD          (220)                  // Value below which a pixel is not considered white
#define DMX_SIGNIFICANT_WEIGHT       (5.0)                  // Poids des pixels non-blancs par rapport aux pixels blancs

// Parameters for blob detection
#define DMX_MIN_BLOB_SIZE            (2)                    // Minimum blob size to be considered (in pixels)
#define DMX_COLOR_SIMILARITY_THRESHOLD (1000)               // Color similarity threshold (squared Euclidean distance)
#define DMX_MAX_BLOBS_PER_ZONE       (15)                   // Nombre maximum de blobs par zone
#define DMX_MAX_ZONE_SIZE            (500)                  // Taille maximum d'une zone avec chevauchement

// Parameters for dark/black area stabilization
#define DMX_LOW_INTENSITY_THRESHOLD  (0.15)                 // Intensity threshold considered as "low"
#define DMX_DARK_SMOOTHING_FACTOR    (0.98)                 // Lissage plus fort pour les zones sombres (0-1)
#define DMX_UNIFORM_THRESHOLD        (8.0)                  // Threshold to consider an area as uniform
#define DMX_MIN_STD_DEV              (0.03)                 // Minimum standard deviation to consider significant variations

/**************************************************************************************
 * DMX Flexible Configuration
 **************************************************************************************/
// Supported projector types
typedef enum {
    DMX_SPOT_RGB = 3,    // RGB (3 canaux)
    DMX_SPOT_RGBW = 4,   // RGB (4 canaux)
    // DMX_SPOT_RGBWA = 5 // RGB (5 canaux)
} DMXSpotType;

// Configuration flexible - changer ces valeurs selon les besoins
#define DMX_SPOT_TYPE           DMX_SPOT_RGBW //DMX_SPOT_RGB
#define DMX_CHANNELS_PER_SPOT   ((int)DMX_SPOT_TYPE)
#define DMX_NUM_SPOTS           (90)                        // Test avec 18 spots
#define DMX_START_CHANNEL       (1)                         // Start channel

#endif // __CONFIG_DMX_H__
