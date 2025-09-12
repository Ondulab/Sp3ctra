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

// Paramètres pour la courbe de réponse progressive
#define DMX_BLACK_THRESHOLD          (0.3)                  // Seuil en dessous duquel les LEDs restent éteintes (0-1)
#define DMX_RESPONSE_CURVE           (2.5)                  // Contrôle l'inflexion de la courbe log/exp

// Paramètres pour la détection des éléments significatifs et le chevauchement
// des zones
#define DMX_ZONE_OVERLAP             (0.15)                 // Facteur de chevauchement entre zones (0-1)
#define DMX_WHITE_THRESHOLD          (220)                  // Valeur en dessous de laquelle un pixel n'est pas considéré comme blanc
#define DMX_SIGNIFICANT_WEIGHT       (5.0)                  // Poids des pixels non-blancs par rapport aux pixels blancs

// Paramètres pour la détection des blobs
#define DMX_MIN_BLOB_SIZE            (2)                    // Taille minimale d'un blob pour être considéré (en pixels)
#define DMX_COLOR_SIMILARITY_THRESHOLD (1000)               // Seuil de similarité de couleur (distance euclidienne au carré)
#define DMX_MAX_BLOBS_PER_ZONE       (15)                   // Nombre maximum de blobs par zone
#define DMX_MAX_ZONE_SIZE            (500)                  // Taille maximum d'une zone avec chevauchement

// Paramètres pour la stabilisation des zones sombres/noires
#define DMX_LOW_INTENSITY_THRESHOLD  (0.15)                 // Seuil d'intensité considéré comme "faible"
#define DMX_DARK_SMOOTHING_FACTOR    (0.98)                 // Lissage plus fort pour les zones sombres (0-1)
#define DMX_UNIFORM_THRESHOLD        (8.0)                  // Seuil pour considérer une zone comme uniforme
#define DMX_MIN_STD_DEV              (0.03)                 // Écart-type minimal pour considérer des variations significatives

#define DMX_NUM_SPOTS                (27)                   // Nombre de spots DMX à gérer

#endif // __CONFIG_DMX_H__
