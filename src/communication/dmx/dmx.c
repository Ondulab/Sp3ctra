#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h> // Pour uint8_t, uint32_t et autres types entiers de taille fixe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/serial.h>

// Manual termios2 definitions to avoid header conflicts
#ifdef __has_include
  #if __has_include(<asm/ioctls.h>)
    #include <asm/ioctls.h>
    #define HAVE_TERMIOS2_IOCTLS 1
  #else
    #define HAVE_TERMIOS2_IOCTLS 0
  #endif
#else
  #define HAVE_TERMIOS2_IOCTLS 1
  #include <asm/ioctls.h>
#endif

// Manual definitions for termios2 to avoid struct conflicts
#if HAVE_TERMIOS2_IOCTLS
  #ifndef TCGETS2
    #define TCGETS2 _IOR('T', 0x2A, struct termios2)
    #define TCSETS2 _IOW('T', 0x2B, struct termios2)
    #define TCSETSW2 _IOW('T', 0x2C, struct termios2)
    #define TCSETSF2 _IOW('T', 0x2D, struct termios2)
  #endif
  
  #ifndef BOTHER
    #define BOTHER 0010000
  #endif
  
  #ifndef CBAUD
    #define CBAUD 0010017
  #endif
  
  // Manual struct termios2 definition to avoid conflicts
  struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag; 
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[32];
    speed_t c_ispeed;
    speed_t c_ospeed;
  };
  
  #define HAVE_TERMIOS2 1
#else
  #define HAVE_TERMIOS2 0
#endif
#endif

#include "config.h"
#include "dmx.h"
#include "context.h"
#include <math.h>

// Fonction pour déterminer si un pixel est significatif
bool isSignificant(uint8_t r, uint8_t g, uint8_t b) {
  // Calculer saturation (0-1, 0=gris, 1=couleur pure)
  uint8_t maxVal = r;
  if (g > maxVal)
    maxVal = g;
  if (b > maxVal)
    maxVal = b;

  uint8_t minVal = r;
  if (g < minVal)
    minVal = g;
  if (b < minVal)
    minVal = b;

  double saturation = (maxVal > 0) ? (double)(maxVal - minVal) / maxVal : 0;

  // Calculer la "non-blancheur" (0=blanc, 1=noir/sombre)
  double brightness = (r + g + b) / 3.0;
  double nonWhiteness = 1.0 - brightness / 255.0;

  // Combinaison pour obtenir la "significativité" du pixel
  double significance = saturation * 0.7 + nonWhiteness * 0.3;

  // Retourner vrai si suffisamment significatif
  return significance > 0.1;
}

// Fonction pour déterminer si deux couleurs sont similaires
bool isColorSimilar(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2,
                    uint8_t b2) {
  // Distance euclidienne dans l'espace RGB
  int dr = r1 - r2;
  int dg = g1 - g2;
  int db = b1 - b2;
  int distanceSquared = dr * dr + dg * dg + db * db;

  // Vérifier si la distance est inférieure au seuil
  return distanceSquared < DMX_COLOR_SIMILARITY_THRESHOLD;
}

// Fonction pour faire croître un blob en ajoutant des pixels adjacents
// similaires
void growBlob(const uint8_t *buffer_R, const uint8_t *buffer_G,
              const uint8_t *buffer_B, bool *processed, Blob *blob,
              size_t start, size_t end) {

  // File d'attente pour les pixels à examiner
  uint16_t queue[DMX_MAX_ZONE_SIZE];
  int queueFront = 0, queueBack = 0;

  // Ajouter le pixel initial à la file
  queue[queueBack++] = blob->startIdx;

  // Tant qu'il reste des pixels à examiner dans la file
  while (queueFront < queueBack) {
    uint16_t currentIdx = queue[queueFront++];
    size_t j = start + currentIdx;

    // Examiner les voisins (gauche et droite pour une ligne 1D)
    // Dans une image 1D (ligne), seuls les pixels adjacents sont considérés
    // comme voisins
    const int neighbors[] = {-1, 1}; // Indices relatifs des voisins

    for (int n = 0; n < 2; n++) {
      uint16_t neighborIdx = currentIdx + neighbors[n];
      size_t neighborJ = start + neighborIdx;

      // Vérifier si le voisin est dans les limites et n'a pas été traité
      if (neighborJ >= start && neighborJ < end && !processed[neighborIdx]) {
        // Vérifier si le voisin est significatif
        if (isSignificant(buffer_R[neighborJ], buffer_G[neighborJ],
                          buffer_B[neighborJ])) {
          // Vérifier si le voisin est similaire en couleur
          if (isColorSimilar(buffer_R[j], buffer_G[j], buffer_B[j],
                             buffer_R[neighborJ], buffer_G[neighborJ],
                             buffer_B[neighborJ])) {

            // Ajouter le voisin au blob et à la file
            processed[neighborIdx] = true;
            queue[queueBack++] = neighborIdx;

            // Mettre à jour la couleur moyenne du blob
            blob->avgR = (blob->avgR * blob->count + buffer_R[neighborJ]) /
                         (blob->count + 1);
            blob->avgG = (blob->avgG * blob->count + buffer_G[neighborJ]) /
                         (blob->count + 1);
            blob->avgB = (blob->avgB * blob->count + buffer_B[neighborJ]) /
                         (blob->count + 1);
            blob->count++;
          }
        }
      }
    }
  }
}

// Fonction pour détecter les blobs dans une zone
int detectBlobs(const uint8_t *buffer_R, const uint8_t *buffer_G,
                const uint8_t *buffer_B, size_t start, size_t end, Blob *blobs,
                double *pixelSignificance) {

  // Tableau temporaire pour marquer les pixels déjà traités
  bool pixelProcessed[DMX_MAX_ZONE_SIZE] = {false};
  int blobCount = 0;

  // Calculer d'abord la significativité de chaque pixel
  for (size_t j = start; j < end; j++) {
    uint16_t localIdx = j - start;

    // Calculer saturation (0-1, 0=gris, 1=couleur pure)
    uint8_t maxVal = buffer_R[j];
    if (buffer_G[j] > maxVal)
      maxVal = buffer_G[j];
    if (buffer_B[j] > maxVal)
      maxVal = buffer_B[j];

    uint8_t minVal = buffer_R[j];
    if (buffer_G[j] < minVal)
      minVal = buffer_G[j];
    if (buffer_B[j] < minVal)
      minVal = buffer_B[j];

    double saturation = (maxVal > 0) ? (double)(maxVal - minVal) / maxVal : 0;

    // Calculer la "non-blancheur" (0=blanc, 1=noir/sombre)
    double brightness = (buffer_R[j] + buffer_G[j] + buffer_B[j]) / 3.0;
    double nonWhiteness = 1.0 - brightness / 255.0;

    // Combinaison pour obtenir la "significativité" du pixel
    pixelSignificance[localIdx] = saturation * 0.7 + nonWhiteness * 0.3;

    // Marquer comme non traité si suffisamment significatif
    pixelProcessed[localIdx] = pixelSignificance[localIdx] <= 0.1;
  }

  // Pour chaque pixel significatif non encore traité, créer un nouveau blob
  for (size_t j = start; j < end; j++) {
    uint16_t localIdx = j - start;

    if (!pixelProcessed[localIdx]) {
      // Vérifier si on peut ajouter plus de blobs
      if (blobCount >= DMX_MAX_BLOBS_PER_ZONE) {
        break;
      }

      // Créer un nouveau blob commençant à l'indice j
      Blob newBlob = {.startIdx = localIdx,
                      .count = 1,
                      .avgR = buffer_R[j],
                      .avgG = buffer_G[j],
                      .avgB = buffer_B[j],
                      .significance = pixelSignificance[localIdx]};

      pixelProcessed[localIdx] = true;

      // Faire croître le blob en ajoutant les pixels adjacents similaires
      growBlob(buffer_R, buffer_G, buffer_B, pixelProcessed, &newBlob, start,
               end);

      // Ajouter ce blob à la liste s'il est suffisamment grand
      if (newBlob.count >= DMX_MIN_BLOB_SIZE) {
        blobs[blobCount++] = newBlob;
      }
    }
  }

  return blobCount;
}

// Fonction pour appliquer une courbe sigmoïde à la luminosité
double sigmoid_response(double x, double center, double steepness) {
  return 1.0 / (1.0 + exp(-(x - center) * steepness));
}

// Fonction pour appliquer un seuil avec rampe logarithmique
double threshold_response(double x, double threshold, double curve) {
  if (x < threshold) {
    return 0.0;
  } else {
    // Normaliser la valeur entre 0 et 1 après le seuil
    double normalized = (x - threshold) / (1.0 - threshold);
    // Appliquer une courbe exponentielle pour une progression plus douce
    return pow(normalized, curve);
  }
}

// Note: The old static spotChannels[] array has been removed
// Channel mapping is now generated dynamically by dmx_generate_channel_mapping()
// based on the flexible configuration (number of spots, channels per spot, start channel)

volatile sig_atomic_t keepRunning = 1;
int fd;

// DMX context global - manages either traditional fd or libftdi
DMXContext dmx_ctx = {0};

// Global DMX spots array - initialized dynamically
static DMXSpot *global_dmx_spots = NULL;
static int global_num_spots = 0;

// Cette fonction est maintenant déclarée externe pour éviter la duplication
// avec le gestionnaire principal dans main.c
extern void signalHandler(int signal);

// Initialize flexible DMX configuration
int dmx_init_configuration(int num_spots, DMXSpotType spot_type, int start_channel) {
    // Validate parameters
    if (num_spots <= 0 || num_spots > 512) {
        printf("❌ Invalid number of spots: %d (must be 1-512)\n", num_spots);
        return -1;
    }
    
    if (spot_type != DMX_SPOT_RGB) {
        printf("❌ Unsupported spot type: %d (only DMX_SPOT_RGB supported)\n", spot_type);
        return -1;
    }
    
    if (start_channel < 1 || start_channel > 512) {
        printf("❌ Invalid start channel: %d (must be 1-512)\n", start_channel);
        return -1;
    }
    
    // Check if total channels fit in DMX universe
    int total_channels = num_spots * (int)spot_type;
    if (start_channel + total_channels - 1 > 512) {
        printf("❌ Configuration exceeds DMX universe: start=%d, spots=%d, channels_per_spot=%d, total=%d\n",
               start_channel, num_spots, (int)spot_type, start_channel + total_channels - 1);
        return -1;
    }
    
    // Free previous allocation if any
    if (global_dmx_spots != NULL) {
        free(global_dmx_spots);
        global_dmx_spots = NULL;
    }
    
    // Allocate memory for spots
    global_dmx_spots = malloc(num_spots * sizeof(DMXSpot));
    if (global_dmx_spots == NULL) {
        printf("❌ Failed to allocate memory for %d DMX spots\n", num_spots);
        return -1;
    }
    
    // Store global configuration
    global_num_spots = num_spots;
    
    // Generate channel mapping
    dmx_generate_channel_mapping(global_dmx_spots, num_spots, spot_type, start_channel);
    
    printf("✅ DMX configuration initialized: %d spots, type=%d, start_channel=%d, total_channels=%d\n",
           num_spots, (int)spot_type, start_channel, total_channels);
    
    return 0;
}

// Generate channel mapping for spots
void dmx_generate_channel_mapping(DMXSpot spots[], int num_spots, DMXSpotType spot_type, int start_channel) {
    for (int i = 0; i < num_spots; i++) {
        spots[i].type = spot_type;
        spots[i].start_channel = start_channel + (i * (int)spot_type);
        
        // Initialize spot data based on type
        switch (spot_type) {
            case DMX_SPOT_RGB:
                spots[i].data.rgb.red = 0;
                spots[i].data.rgb.green = 0;
                spots[i].data.rgb.blue = 0;
                break;
            // Future extensions for other types
            default:
                printf("⚠️  Unsupported spot type in channel mapping: %d\n", (int)spot_type);
                break;
        }
    }
    
    printf("🔧 Generated channel mapping: spot[0] starts at channel %d, spot[%d] starts at channel %d\n",
           spots[0].start_channel, num_spots-1, spots[num_spots-1].start_channel);
}

void intHandler(int dummy) {
  (void)dummy;
  keepRunning = 0;
  // Appel au gestionnaire principal pour assurer une terminaison complète
  signalHandler(dummy);
}

// Structure pour stocker les couleurs d'un spot
typedef struct {
  double red;
  double green;
  double blue;
  double white;
  double intensity;
} SpotColor;

// Calcule l'écart-type d'un ensemble de valeurs
double calculateStandardDeviation(const uint8_t *values, size_t start,
                                  size_t end) {
  if (end <= start)
    return 0.0;

  // Calculer la moyenne
  double sum = 0.0;
  for (size_t i = start; i < end; i++) {
    sum += values[i];
  }
  double mean = sum / (end - start);

  // Calculer la somme des carrés des écarts
  double sumSquaredDiff = 0.0;
  for (size_t i = start; i < end; i++) {
    double diff = values[i] - mean;
    sumSquaredDiff += diff * diff;
  }

  // Calculer l'écart-type
  return sqrt(sumSquaredDiff / (end - start));
}

// Approche hybride : détection des blobs pour filtrer les poussières +
// transition douce + stabilisation des zones sombres
void computeAverageColorPerZone(const uint8_t *buffer_R,
                                const uint8_t *buffer_G,
                                const uint8_t *buffer_B, size_t numPixels,
                                DMXSpot spots[], int num_spots) {
  size_t zoneSize = numPixels / num_spots;
  double overlap = DMX_ZONE_OVERLAP; // Facteur de chevauchement entre zones
  
  // Static arrays sized for maximum spots for backward compatibility
  static int initialized[DMX_NUM_SPOTS] = {0};
  static double smoothR[DMX_NUM_SPOTS] = {0.0};
  static double smoothG[DMX_NUM_SPOTS] = {0.0};
  static double smoothB[DMX_NUM_SPOTS] = {0.0};
  static double smoothW[DMX_NUM_SPOTS] = {0.0};
  double alpha = DMX_SMOOTHING_FACTOR;

  // Correction factors for RGB
  double redFactor = DMX_RED_FACTOR;
  double greenFactor = DMX_GREEN_FACTOR;
  double blueFactor = DMX_BLUE_FACTOR;

  // Buffer pour stocker les couleurs calculées par zone (via détection de
  // blobs)
  SpotColor zoneColors[DMX_NUM_SPOTS] = {0};

  // Buffer pour stocker les couleurs après application des transitions
  SpotColor finalColors[DMX_NUM_SPOTS] = {0};

  // PHASE 1: Détection des blobs et calcul des couleurs par zone
  for (int i = 0; i < num_spots; i++) {
    // Calcul des limites de la zone avec chevauchement
    size_t zoneCenter = i * zoneSize + zoneSize / 2;
    size_t extendedZoneSize = (size_t)(zoneSize * (1.0 + overlap));

    // Limiter la taille pour éviter les débordements
    if (extendedZoneSize > DMX_MAX_ZONE_SIZE) {
      extendedZoneSize = DMX_MAX_ZONE_SIZE;
    }

    // Calculer start et end avec débordement contrôlé
    size_t start = (zoneCenter > extendedZoneSize / 2)
                       ? zoneCenter - extendedZoneSize / 2
                       : 0;
    size_t end = start + extendedZoneSize;
    if (end > numPixels) {
      end = numPixels;
    }

    // Variables pour le calcul normal
    unsigned long sumR = 0, sumG = 0, sumB = 0;
    size_t count = end - start;

    // Variables pour la détection des blobs
    Blob blobs[DMX_MAX_BLOBS_PER_ZONE];
    double pixelSignificance[DMX_MAX_ZONE_SIZE];

    // Somme des couleurs pour la moyenne standard
    for (size_t j = start; j < end; j++) {
      sumR += buffer_R[j];
      sumG += buffer_G[j];
      sumB += buffer_B[j];
    }

    // Détecter les blobs significatifs dans la zone
    int blobCount = detectBlobs(buffer_R, buffer_G, buffer_B, start, end, blobs,
                                pixelSignificance);

    // Calculer la couleur à utiliser
    double avgR, avgG, avgB;

    // Si aucun blob significatif trouvé, moyenne standard
    if (blobCount == 0) {
      avgR = (double)sumR / count;
      avgG = (double)sumG / count;
      avgB = (double)sumB / count;
    } else {
      // Contribution pondérée de chaque blob
      double totalWeight = 0.0;
      double weightedSumR = 0.0, weightedSumG = 0.0, weightedSumB = 0.0;

      for (int b = 0; b < blobCount; b++) {
        // Poids = taille du blob × significativité²
        double blobWeight =
            blobs[b].count * blobs[b].significance * blobs[b].significance;

        weightedSumR += blobs[b].avgR * blobWeight;
        weightedSumG += blobs[b].avgG * blobWeight;
        weightedSumB += blobs[b].avgB * blobWeight;
        totalWeight += blobWeight;
      }

      // Gros blob unique = plus d'importance
      if (blobCount == 1 && blobs[0].count > DMX_MIN_BLOB_SIZE * 3) {
        double blendFactor = 0.8; // 80% blob, 20% moyenne

        avgR = (weightedSumR / totalWeight) * blendFactor +
               ((double)sumR / count) * (1.0 - blendFactor);
        avgG = (weightedSumG / totalWeight) * blendFactor +
               ((double)sumG / count) * (1.0 - blendFactor);
        avgB = (weightedSumB / totalWeight) * blendFactor +
               ((double)sumB / count) * (1.0 - blendFactor);
      } else {
        // Mélange des blobs significatifs
        avgR = weightedSumR / totalWeight;
        avgG = weightedSumG / totalWeight;
        avgB = weightedSumB / totalWeight;
      }
    }

    // Traitement zones noires (inverser en blanc si nécessaire)
    double luminance = (0.299 * avgR + 0.587 * avgG + 0.114 * avgB);
    if (luminance < 10.0) {
      // Si activé: convertir en blanc
      // avgR = 255.0;
      // avgG = 255.0;
      // avgB = 255.0;
    }

    // Calculer l'écart-type pour déterminer si la zone est uniforme
    double stdDevR = calculateStandardDeviation(buffer_R, start, end);
    double stdDevG = calculateStandardDeviation(buffer_G, start, end);
    double stdDevB = calculateStandardDeviation(buffer_B, start, end);

    // Écart-type global RGB
    double avgStdDev = (stdDevR + stdDevG + stdDevB) / 3.0;

    // Calcul intensité (noir=1, blanc=0)
    double intensity = 1.0 - (luminance / 255.0);
    double response_factor = 0.0;

    /**
    // La zone est-elle sombre ET uniforme? (surface noire sans variations
    // significatives)
    bool isDarkUniform = (intensity > DMX_LOW_INTENSITY_THRESHOLD &&
                          avgStdDev < DMX_UNIFORM_THRESHOLD);
    // Si c'est une zone noire uniforme, réduire très fortement l'intensité
    if (isDarkUniform) {
      // Surface noire uniforme - atténuer fortement l'intensité pour éviter les
      // clignotements
      response_factor = 0.0; // Éteindre complètement
    };
    **/
    // Sinon, traitement normal
    else if (intensity > DMX_BLACK_THRESHOLD) {
      double normalized =
          (intensity - DMX_BLACK_THRESHOLD) / (1.0 - DMX_BLACK_THRESHOLD);
      response_factor = pow(normalized, DMX_RESPONSE_CURVE);
    }

    // Correction gamma
    double gamma = DMX_GAMMA;
    double I_spots_corr = pow(response_factor, gamma);

    // Appliquer l'intensité aux valeurs RGB
    zoneColors[i].red = avgR * I_spots_corr;
    zoneColors[i].green = avgG * I_spots_corr;
    zoneColors[i].blue = avgB * I_spots_corr;
    zoneColors[i].white =
        (zoneColors[i].red + zoneColors[i].green + zoneColors[i].blue) / 3.0;
    zoneColors[i].intensity = I_spots_corr;
  }

  // PHASE 2: Application des transitions douces entre zones
  for (int i = 0; i < num_spots; i++) {
    // Initialiser avec la couleur de la zone elle-même
    finalColors[i] = zoneColors[i];

    // Distance max d'influence entre zones
    double maxInfluenceDistance = zoneSize * (1.0 + overlap);

    // Réinitialiser ces valeurs pour pouvoir normaliser après
    finalColors[i].red = 0;
    finalColors[i].green = 0;
    finalColors[i].blue = 0;
    finalColors[i].intensity = 0;
    double totalWeight = 0;

    // Pour chaque zone, calculer l'influence de toutes les autres
    for (int j = 0; j < num_spots; j++) {
      // Distance entre les centres des zones i et j
      double distance = fabs((double)(i - j) * zoneSize);

      // Si la distance est inférieure à maxInfluenceDistance
      if (distance < maxInfluenceDistance) {
        // Facteur de transition = 1 au centre, décroit linéairement
        double transitionFactor = 1.0 - (distance / maxInfluenceDistance);

        // Améliorer la forme de la transition avec exposant
        transitionFactor = pow(transitionFactor, 1.5);

        // Seuil minimal d'influence
        if (transitionFactor > 0.05) {
          // Pondérer la couleur de la zone j selon son influence
          finalColors[i].red += zoneColors[j].red * transitionFactor;
          finalColors[i].green += zoneColors[j].green * transitionFactor;
          finalColors[i].blue += zoneColors[j].blue * transitionFactor;
          finalColors[i].intensity +=
              zoneColors[j].intensity * transitionFactor;
          totalWeight += transitionFactor;
        }
      }
    }

    // Normaliser les valeurs finales
    if (totalWeight > 0) {
      finalColors[i].red /= totalWeight;
      finalColors[i].green /= totalWeight;
      finalColors[i].blue /= totalWeight;
      finalColors[i].white =
          (finalColors[i].red + finalColors[i].green + finalColors[i].blue) /
          3.0;
      finalColors[i].intensity /= totalWeight;
    }

    // Lissage temporel (exponential moving average)
    if (!initialized[i]) {
      smoothR[i] = finalColors[i].red;
      smoothG[i] = finalColors[i].green;
      smoothB[i] = finalColors[i].blue;
      smoothW[i] = finalColors[i].white;
      initialized[i] = 1;
    } else {
      smoothR[i] = alpha * smoothR[i] + (1.0 - alpha) * finalColors[i].red;
      smoothG[i] = alpha * smoothG[i] + (1.0 - alpha) * finalColors[i].green;
      smoothB[i] = alpha * smoothB[i] + (1.0 - alpha) * finalColors[i].blue;
      smoothW[i] = alpha * smoothW[i] + (1.0 - alpha) * finalColors[i].white;
    }

    // Conversion en uint8_t pour les spots avec nouvelle structure union
    spots[i].data.rgb.red = (uint8_t)smoothR[i];
    spots[i].data.rgb.green = (uint8_t)smoothG[i];
    spots[i].data.rgb.blue = (uint8_t)smoothB[i];
    // Note: RGB spots don't have white channel, so we skip spots[i].white

    // Application de la correction du profil couleur sur RGB
    applyColorProfile(&spots[i].data.rgb.red, &spots[i].data.rgb.green, &spots[i].data.rgb.blue, redFactor,
                      greenFactor, blueFactor);
  }
}

void applyColorProfile(uint8_t *red, uint8_t *green, uint8_t *blue,
                       double redFactor, double greenFactor,
                       double blueFactor) {
  // Appliquer les facteurs de correction RGB
  double newRed = (*red) * redFactor;
  double newGreen = (*green) * greenFactor;
  double newBlue = (*blue) * blueFactor;

  // Clamp values to 255.
  if (newRed > 255.0) {
    newRed = 255.0;
  }
  if (newGreen > 255.0) {
    newGreen = 255.0;
  }
  if (newBlue > 255.0) {
    newBlue = 255.0;
  }

  // Amplification de la saturation
  double saturationFactor = DMX_SATURATION_FACTOR;
  double avg = (newRed + newGreen + newBlue) / 3.0;

  // Augmenter l'écart entre chaque composante et la moyenne (augmente la
  // saturation)
  newRed = avg + (newRed - avg) * saturationFactor;
  newGreen = avg + (newGreen - avg) * saturationFactor;
  newBlue = avg + (newBlue - avg) * saturationFactor;

  // Limiter à nouveau les valeurs entre 0 et 255
  if (newRed > 255.0)
    newRed = 255.0;
  if (newRed < 0.0)
    newRed = 0.0;
  if (newGreen > 255.0)
    newGreen = 255.0;
  if (newGreen < 0.0)
    newGreen = 0.0;
  if (newBlue > 255.0)
    newBlue = 255.0;
  if (newBlue < 0.0)
    newBlue = 0.0;

  *red = (uint8_t)newRed;
  *green = (uint8_t)newGreen;
  *blue = (uint8_t)newBlue;
}


#ifdef __linux__

// libftdi DMX break function - ported from dmx_libftdi_fixed.c
int send_dmx_break_libftdi(struct ftdi_context *ftdi) {
    int ret;
    
    // Method 1: Use proper FTDI break functions
    ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON);
    if (ret < 0) {
        // Fallback Method 2: Manual break via bitbang mode
        ret = ftdi_set_bitmode(ftdi, 0x01, BITMODE_BITBANG);
        if (ret < 0) return ret;
        
        ret = ftdi_write_data(ftdi, (unsigned char*)"\x00", 1); // Force line low
        if (ret < 0) return ret;
        
        usleep(176); // DMX break minimum 176µs
        
        ret = ftdi_write_data(ftdi, (unsigned char*)"\x01", 1); // Force line high  
        if (ret < 0) return ret;
        
        usleep(12); // Mark after break 12µs
        
        // Return to normal serial mode
        ret = ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET);
        return ret;
    }
    
    // Method 1 worked - complete the proper break sequence
    usleep(176); // DMX break minimum 176µs
    
    ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF);
    if (ret < 0) return ret;
    
    usleep(12); // Mark after break 12µs
    
    return 0;
}

// libftdi DMX frame sending function
int send_dmx_frame_libftdi(struct ftdi_context *ftdi, unsigned char *frame, size_t len) {
    int ret;
    
    // Send proper DMX break
    ret = send_dmx_break_libftdi(ftdi);
    if (ret < 0) {
        return ret;
    }
    
    // Send DMX data frame
    ret = ftdi_write_data(ftdi, frame, len);
    if (ret < 0) {
        return ret;
    }
    
    return 0;
}

// Initialize libftdi DMX context - Linux only
int init_dmx_linux_libftdi(int silent) {
    int ret;
    
    if (!silent)
        printf("🔧 Initializing DMX via libftdi (Linux)...\n");
    
    // Initialize libftdi context
    dmx_ctx.ftdi = ftdi_new();
    if (dmx_ctx.ftdi == NULL) {
        if (!silent)
            fprintf(stderr, "❌ ftdi_new failed\n");
        return -1;
    }
    
    // Open FTDI device - auto-detect first FTDI device
    ret = ftdi_usb_open(dmx_ctx.ftdi, 0x0403, 0x6001);  // Standard FTDI VID/PID
    if (ret < 0) {
        if (!silent)
            fprintf(stderr, "❌ Unable to open FTDI device: %s\n", ftdi_get_error_string(dmx_ctx.ftdi));
        ftdi_free(dmx_ctx.ftdi);
        dmx_ctx.ftdi = NULL;
        return -1;
    }
    
    if (!silent)
        printf("✅ FTDI device opened successfully\n");
    
    // Configure for DMX: 250000 bps, 8N2
    ret = ftdi_set_baudrate(dmx_ctx.ftdi, DMX_BAUD);
    if (ret < 0) {
        if (!silent)
            fprintf(stderr, "❌ Set baud rate failed: %s\n", ftdi_get_error_string(dmx_ctx.ftdi));
        ftdi_usb_close(dmx_ctx.ftdi);
        ftdi_free(dmx_ctx.ftdi);
        dmx_ctx.ftdi = NULL;
        return -1;
    } else {
        if (!silent)
            printf("✅ Baud rate set to %d\n", DMX_BAUD);
    }
    
    ret = ftdi_set_line_property(dmx_ctx.ftdi, BITS_8, STOP_BIT_2, NONE);
    if (ret < 0) {
        if (!silent)
            fprintf(stderr, "❌ Set line properties failed: %s\n", ftdi_get_error_string(dmx_ctx.ftdi));
        ftdi_usb_close(dmx_ctx.ftdi);
        ftdi_free(dmx_ctx.ftdi);
        dmx_ctx.ftdi = NULL;
        return -1;
    } else {
        if (!silent)
            printf("✅ Line properties set (8N2)\n");
    }
    
    // Reset any previous bitmode settings
    ret = ftdi_set_bitmode(dmx_ctx.ftdi, 0x00, BITMODE_RESET);
    if (ret < 0) {
        if (!silent)
            printf("⚠️  Bitmode reset warning: %s\n", ftdi_get_error_string(dmx_ctx.ftdi));
    }
    
    // Mark context as using libftdi
    dmx_ctx.use_libftdi = 1;
    dmx_ctx.fd = -1; // Not using traditional fd
    
    if (!silent)
        printf("🎉 libftdi DMX initialized successfully\n");
    
    return 0; // Success
}

// Cleanup libftdi resources
void cleanup_dmx_libftdi(void) {
    if (dmx_ctx.ftdi) {
        ftdi_usb_close(dmx_ctx.ftdi);
        ftdi_free(dmx_ctx.ftdi);
        dmx_ctx.ftdi = NULL;
    }
    dmx_ctx.use_libftdi = 0;
    dmx_ctx.fd = -1;
}

// Function to get detailed USB device information
void print_usb_device_info(int fd, int silent) {
    if (silent) return;
    
    struct serial_struct ser;
    if (ioctl(fd, TIOCGSERIAL, &ser) == 0) {
        printf("🔍 USB Serial Device Information:\n");
        printf("   Type: %d, Line: %d\n", ser.type, ser.line);
        printf("   Port: 0x%x, IRQ: %d\n", ser.port, ser.irq);
        printf("   Flags: 0x%x\n", ser.flags);
        printf("   Base baud: %d, Custom divisor: %d\n", ser.baud_base, ser.custom_divisor);
        
        // Try to identify device type
        if (ser.type == PORT_16550A) {
            printf("   Device type: 16550A UART\n");
        } else if (ser.type == PORT_UNKNOWN) {
            printf("   Device type: Unknown\n");
        } else {
            printf("   Device type: %d (see linux/serial.h)\n", ser.type);
        }
    }
}

// Enhanced function to set custom baud rate using termios2 API
int set_custom_baudrate_termios2(int fd, int baud, int silent) {
#if HAVE_TERMIOS2
    struct termios2 tio2;
    
    if (!silent)
        printf("🔧 Attempting termios2 configuration for %d bps...\n", baud);
    
    // Get current termios2 settings
    if (ioctl(fd, TCGETS2, &tio2) < 0) {
        if (!silent)
            printf("⚠️  TCGETS2 failed, termios2 not available: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }
    
    if (!silent)
        printf("✅ termios2 available, current speeds: %u/%u\n", tio2.c_ispeed, tio2.c_ospeed);
    
    // Store original values for comparison
    unsigned int orig_ispeed = tio2.c_ispeed;
    unsigned int orig_ospeed = tio2.c_ospeed;
    tcflag_t orig_cflag = tio2.c_cflag;
    
    // Configure for custom baud rate
    tio2.c_cflag &= ~CBAUD;     // Clear baud rate mask
    tio2.c_cflag |= BOTHER;     // Enable custom baud rate
    tio2.c_ispeed = baud;       // Set input speed
    tio2.c_ospeed = baud;       // Set output speed
    
    if (!silent) {
        printf("🔧 Configuring: cflag 0x%x -> 0x%x, speeds %u/%u -> %u/%u\n",
               orig_cflag, tio2.c_cflag, orig_ispeed, orig_ospeed, tio2.c_ispeed, tio2.c_ospeed);
    }
    
    // Apply the new settings
    if (ioctl(fd, TCSETS2, &tio2) < 0) {
        if (!silent)
            printf("⚠️  TCSETS2 failed: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }
    
    // Small delay to let settings take effect
    usleep(10000); // 10ms
    
    // Verify the baud rate was set correctly
    struct termios2 verify_tio2;
    if (ioctl(fd, TCGETS2, &verify_tio2) < 0) {
        if (!silent)
            printf("⚠️  Verification TCGETS2 failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (!silent) {
        printf("✅ termios2 configuration applied\n");
        printf("   Verified speeds: %u/%u (requested: %u)\n", 
               verify_tio2.c_ispeed, verify_tio2.c_ospeed, (unsigned int)baud);
        printf("   Verified cflag: 0x%x\n", verify_tio2.c_cflag);
    }
    
    // Check if the speeds match what we requested (allow small tolerance)
    unsigned int tolerance = baud / 100; // 1% tolerance
    if (abs((int)verify_tio2.c_ispeed - baud) <= (int)tolerance && 
        abs((int)verify_tio2.c_ospeed - baud) <= (int)tolerance) {
        if (!silent)
            printf("🎉 termios2 DMX baud rate successfully configured: %u bps\n", verify_tio2.c_ospeed);
        return 0;
    } else {
        if (!silent) {
            printf("⚠️  termios2 baud rate mismatch\n");
            printf("   Requested: %d, Got input: %u, output: %u\n", 
                   baud, verify_tio2.c_ispeed, verify_tio2.c_ospeed);
            printf("   Difference: input %d, output %d\n", 
                   (int)verify_tio2.c_ispeed - baud, (int)verify_tio2.c_ospeed - baud);
        }
        return -1;
    }
    
#else
    (void)fd; (void)baud; (void)silent; // Suppress unused warnings
    if (!silent)
        printf("⚠️  termios2 headers not available at compile time\n");
    return -1; // termios2 headers not available
#endif
}

// Enhanced function to set custom baud rate using FTDI-specific ioctl
int set_custom_baudrate_ftdi(int fd, int baud, int silent) {
    struct serial_struct ser;
    
    if (!silent)
        printf("🔧 Attempting FTDI configuration for %d bps...\n", baud);
    
    // Get current serial settings
    if (ioctl(fd, TIOCGSERIAL, &ser) < 0) {
        if (!silent)
            printf("⚠️  TIOCGSERIAL failed: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }
    
    if (!silent) {
        printf("✅ FTDI device detected\n");
        printf("   Current: type=%d, baud_base=%d, custom_divisor=%d, flags=0x%x\n",
               ser.type, ser.baud_base, ser.custom_divisor, ser.flags);
    }
    
    // Store original values
    int orig_baud_base = ser.baud_base;
    int orig_divisor = ser.custom_divisor;
    int orig_flags = ser.flags;
    
    // Calculate the divisor for the desired baud rate
    // FTDI chips use: actual_baud = base_clock / (16 * divisor)
    // So: divisor = base_clock / (16 * desired_baud)
    
    // Try different FTDI base clock frequencies
    int base_clocks[] = {3000000, 48000000, 12000000, 6000000, 24000000};
    const char* base_names[] = {"3MHz", "48MHz", "12MHz", "6MHz", "24MHz"};
    int num_clocks = sizeof(base_clocks) / sizeof(base_clocks[0]);
    
    for (int i = 0; i < num_clocks; i++) {
        // Calculate divisor: divisor = base_clock / (16 * baud)
        int divisor = base_clocks[i] / (16 * baud);
        
        if (divisor > 0 && divisor <= 65535) { // Valid divisor range
            if (!silent) {
                printf("🔧 Trying %s base clock: divisor=%d\n", base_names[i], divisor);
            }
            
            // Configure the serial settings
            ser.custom_divisor = divisor;
            ser.baud_base = base_clocks[i] / 16;
            ser.flags &= ~ASYNC_SPD_MASK;
            ser.flags |= ASYNC_SPD_CUST;
            
            // Apply settings
            if (ioctl(fd, TIOCSSERIAL, &ser) < 0) {
                if (!silent)
                    printf("⚠️  TIOCSSERIAL failed for %s: %s\n", base_names[i], strerror(errno));
                continue;
            }
            
            // Small delay
            usleep(10000); // 10ms
            
            // Verify by reading back the settings
            struct serial_struct verify_ser;
            if (ioctl(fd, TIOCGSERIAL, &verify_ser) < 0) {
                if (!silent)
                    printf("⚠️  Verification TIOCGSERIAL failed: %s\n", strerror(errno));
                continue;
            }
            
            // Calculate the actual baud rate: actual = base_clock / (16 * divisor)
            int actual_baud = base_clocks[i] / (16 * verify_ser.custom_divisor);
            
            if (!silent) {
                printf("✅ FTDI configured with %s base clock\n", base_names[i]);
                printf("   Settings: baud_base=%d, custom_divisor=%d, flags=0x%x\n",
                       verify_ser.baud_base, verify_ser.custom_divisor, verify_ser.flags);
                printf("   Calculated actual baud rate: %d bps\n", actual_baud);
            }
            
            // Check if we're close enough (within 2% tolerance for FTDI)
            int tolerance = baud / 50; // 2% tolerance
            if (abs(actual_baud - baud) <= tolerance) {
                if (!silent)
                    printf("🎉 FTDI DMX baud rate successfully configured: %d bps (target: %d)\n", 
                           actual_baud, baud);
                return 0;
            } else {
                if (!silent)
                    printf("⚠️  FTDI baud rate outside tolerance: %d bps (target: %d, diff: %d)\n", 
                           actual_baud, baud, abs(actual_baud - baud));
            }
        } else {
            if (!silent)
                printf("⚠️  Invalid divisor %d for %s base clock\n", divisor, base_names[i]);
        }
    }
    
    // Restore original settings if all attempts failed
    ser.baud_base = orig_baud_base;
    ser.custom_divisor = orig_divisor;
    ser.flags = orig_flags;
    ioctl(fd, TIOCSSERIAL, &ser);
    
    if (!silent)
        printf("⚠️  All FTDI baud rate attempts failed, settings restored\n");
    return -1;
}

// Function to try system command approach as last resort
int set_custom_baudrate_system(int fd, int baud, const char* port, int silent) {
    if (!silent)
        printf("🔧 Attempting system command approach for %d bps...\n", baud);
        
    // Close fd temporarily for external stty command
    close(fd);
    
    // Try different stty command variations
    char cmd[256];
    
    // Method 1: Direct baud rate setting
    snprintf(cmd, sizeof(cmd), "stty -F %s %d 2>/dev/null", port, baud);
    if (!silent)
        printf("🔧 Trying: %s\n", cmd);
    
    if (system(cmd) == 0) {
        // Reopen the port
        fd = open(port, O_RDWR | O_NOCTTY);
        if (fd >= 0) {
            if (!silent)
                printf("✅ System stty command succeeded\n");
            return fd; // Return new fd
        }
    }
    
    // Method 2: Try with different parameters
    snprintf(cmd, sizeof(cmd), "stty -F %s speed %d raw -echo 2>/dev/null", port, baud);
    if (!silent)
        printf("🔧 Trying: %s\n", cmd);
        
    if (system(cmd) == 0) {
        // Reopen the port
        fd = open(port, O_RDWR | O_NOCTTY);
        if (fd >= 0) {
            if (!silent)
                printf("✅ System stty speed command succeeded\n");
            return fd; // Return new fd
        }
    }
    
    // Reopen with original settings if system commands failed
    fd = open(port, O_RDWR | O_NOCTTY);
    if (!silent)
        printf("⚠️  System command approach failed, reopened port\n");
    return fd; // Return reopened fd (even if system commands failed)
}
#endif

int send_dmx_frame(int fd, unsigned char *frame, size_t len) {
#ifdef __linux__
  // Use libftdi if available and initialized
  if (dmx_ctx.use_libftdi && dmx_ctx.ftdi) {
    return send_dmx_frame_libftdi(dmx_ctx.ftdi, frame, len);
  }
#endif

  // Traditional fd-based DMX (Mac or Linux fallback)
  // Set break condition (100 µs) then clear and wait for 12 µs (Mark After Break)
  if (ioctl(fd, TIOCSBRK) < 0) {
    perror("Error setting break condition");
    return -1;
  }
  usleep(100); // 100 µs break

  if (ioctl(fd, TIOCCBRK) < 0) {
    perror("Error clearing break condition");
    return -1;
  }
  usleep(12); // 12 µs Mark After Break

  ssize_t written = write(fd, frame, len);
  if (written < 0) {
    perror("Error writing frame");
    return -1;
  }
  if (tcdrain(fd) < 0) {
    perror("Error draining output");
    return -1;
  }

  return 0;
}

// Platform-specific DMX initialization functions

#ifdef __APPLE__
int init_dmx_macos(const char *port, int silent) {
  int fd;
  struct termios tty;

  if (!port) {
    if (!silent)
      fprintf(stderr, "❌ DMX port required on macOS\n");
    return -1;
  }

  if (!silent)
    printf("🍎 Initializing DMX on macOS with port: %s\n", port);

  // Open serial port
  fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (!silent)
      perror("Error opening serial port");
    return -1;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    if (!silent)
      perror("Error getting flags");
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
    if (!silent)
      perror("Error setting flags");
    close(fd);
    return -1;
  }

  if (tcgetattr(fd, &tty) != 0) {
    if (!silent)
      perror("Error from tcgetattr");
    close(fd);
    return -1;
  }

  cfmakeraw(&tty);
  tty.c_cflag &= ~PARENB; // No parity
  tty.c_cflag &= ~CSTOPB; // 1 stop bit by default
  tty.c_cflag |= CSTOPB;  // Activate 2 stop bits
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8; // 8 data bits
  tty.c_cflag |= CLOCAL; // Ignore modem control lines
  tty.c_cflag |= CREAD;  // Enable receiver
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10; // Timeout 1 sec

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    if (!silent) {
      perror("Error from tcsetattr");
      printf("Errno: %d, %s\n", errno, strerror(errno));
    }
    close(fd);
    return -1;
  }

  int status;
  if (ioctl(fd, TIOCMGET, &status) < 0) {
    if (!silent)
      perror("Error getting modem status");
    close(fd);
    return -1;
  }
  status &= ~(TIOCM_DTR | TIOCM_RTS);
  if (ioctl(fd, TIOCMSET, &status) < 0) {
    if (!silent)
      perror("Error setting modem status");
    close(fd);
    return -1;
  }

  // Set DMX baud rate using macOS IOSSIOSPEED
  speed_t speed = DMX_BAUD;
  if (ioctl(fd, IOSSIOSPEED, &speed) < 0) {
    if (!silent)
      perror("Error setting custom baud rate");
    close(fd);
    return -1;
  }
  
  // Configure context for traditional fd
  dmx_ctx.use_libftdi = 0;
  dmx_ctx.fd = fd;
  
  if (!silent)
    printf("✅ macOS DMX initialized successfully: %d bps\n", DMX_BAUD);
  return fd;
}
#endif

#ifdef __linux__
int init_dmx_linux_standard(const char *port, int silent) {
  int fd;
  struct termios tty;

  if (!silent)
    printf("🐧 Initializing DMX on Linux (standard) with port: %s\n", port);

  // Open serial port
  fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (!silent)
      perror("Error opening serial port");
    return -1;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    if (!silent)
      perror("Error getting flags");
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
    if (!silent)
      perror("Error setting flags");
    close(fd);
    return -1;
  }

  if (tcgetattr(fd, &tty) != 0) {
    if (!silent)
      perror("Error from tcgetattr");
    close(fd);
    return -1;
  }

  cfmakeraw(&tty);
  tty.c_cflag &= ~PARENB; // No parity
  tty.c_cflag &= ~CSTOPB; // 1 stop bit by default
  tty.c_cflag |= CSTOPB;  // Activate 2 stop bits
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8; // 8 data bits
  tty.c_cflag |= CLOCAL; // Ignore modem control lines
  tty.c_cflag |= CREAD;  // Enable receiver
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10; // Timeout 1 sec

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    if (!silent) {
      perror("Error from tcsetattr");
      printf("Errno: %d, %s\n", errno, strerror(errno));
    }
    close(fd);
    return -1;
  }

  int status;
  if (ioctl(fd, TIOCMGET, &status) < 0) {
    if (!silent)
      perror("Error getting modem status");
    close(fd);
    return -1;
  }
  status &= ~(TIOCM_DTR | TIOCM_RTS);
  if (ioctl(fd, TIOCMSET, &status) < 0) {
    if (!silent)
      perror("Error setting modem status");
    close(fd);
    return -1;
  }

  // Linux multi-level baud rate configuration
  int baud_configured = 0;
  
  if (!silent) {
    printf("🔧 Configuring DMX baud rate (%d bps) using multi-level approach...\n", DMX_BAUD);
    print_usb_device_info(fd, silent);
  }
  
  // Try multiple approaches
  if (!baud_configured && set_custom_baudrate_termios2(fd, DMX_BAUD, silent) == 0) {
    baud_configured = 1;
  }
  if (!baud_configured && set_custom_baudrate_ftdi(fd, DMX_BAUD, silent) == 0) {
    baud_configured = 1;
  }
  if (!baud_configured) {
    int new_fd = set_custom_baudrate_system(fd, DMX_BAUD, port, silent);
    if (new_fd >= 0) {
      fd = new_fd;
      baud_configured = 1;
    }
  }
  
  if (!baud_configured) {
    if (!silent)
      printf("⚠️  All standard DMX baud rate methods failed\n");
    close(fd);
    return -1;
  }
  
  // Configure context for traditional fd
  dmx_ctx.use_libftdi = 0;
  dmx_ctx.fd = fd;
  
  if (!silent)
    printf("✅ Linux DMX (standard) initialized successfully\n");
  return fd;
}
#endif

// Unified DMX initialization with conditional architecture
int init_Dmx(const char *port, int silent) {
  // Clear any previous context
  memset(&dmx_ctx, 0, sizeof(dmx_ctx));
  dmx_ctx.fd = -1;

#ifdef __APPLE__
  // macOS: Use port parameter (required)
  return init_dmx_macos(port, silent);
#else
  // Linux: Try libftdi first (ignore port), then fallback to standard methods
  if (!silent)
    printf("🐧 Linux DMX initialization - trying libftdi first...\n");
  
  // Try libftdi first (auto-detect, ignore port parameter)
  if (init_dmx_linux_libftdi(silent) == 0) {
    if (!silent)
      printf("🎉 DMX initialized via libftdi (recommended for Linux)\n");
    return 0; // Success with libftdi
  }
  
  if (!silent) {
    printf("⚠️  libftdi initialization failed, falling back to standard methods...\n");
  }
  
  // Fallback to standard Linux methods (use port parameter)
  if (!port) {
    if (!silent)
      fprintf(stderr, "❌ DMX port required for Linux fallback methods\n");
    return -1;
  }
  
  return init_dmx_linux_standard(port, silent);
#endif
}
