#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif
#ifdef __linux__
#define _GNU_SOURCE
#include <linux/serial.h>
// termios2 headers for custom baud rates on Linux
#ifdef __has_include
  #if __has_include(<asm/ioctls.h>) && __has_include(<asm/termbits.h>)
    #include <asm/ioctls.h>
    #include <asm/termbits.h>
    #define HAVE_TERMIOS2 1
  #else
    #define HAVE_TERMIOS2 0
  #endif
#else
  // Fallback for older compilers without __has_include
  #include <asm/ioctls.h>
  #include <asm/termbits.h>
  #define HAVE_TERMIOS2 1
#endif
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

#include "config.h"
#include "dmx.h"
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

// DMX addresses for each spot (3 channels per spot)
// Configuration pour Stairville Show Bar Tri LED 18x3W RGB
// Adresse de départ: 1, mode 54 canaux pour contrôle individuel des 18 LEDs
// Canaux 1-3 : RGB pour LED 1, canaux 4-6 : RGB pour LED 2, etc.
const uint8_t spotChannels[DMX_NUM_SPOTS] = {
    1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 52};

volatile sig_atomic_t keepRunning = 1;
int fd;

// Cette fonction est maintenant déclarée externe pour éviter la duplication
// avec le gestionnaire principal dans main.c
extern void signalHandler(int signal);

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
                                DMXSpot spots[DMX_NUM_SPOTS]) {
  size_t zoneSize = numPixels / DMX_NUM_SPOTS;
  double overlap = DMX_ZONE_OVERLAP; // Facteur de chevauchement entre zones
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
  for (size_t i = 0; i < DMX_NUM_SPOTS; i++) {
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

    // La zone est-elle sombre ET uniforme? (surface noire sans variations
    // significatives)
    bool isDarkUniform = (intensity > DMX_LOW_INTENSITY_THRESHOLD &&
                          avgStdDev < DMX_UNIFORM_THRESHOLD);

    // Si c'est une zone noire uniforme, réduire très fortement l'intensité
    if (isDarkUniform) {
      // Surface noire uniforme - atténuer fortement l'intensité pour éviter les
      // clignotements
      response_factor = 0.0; // Éteindre complètement
    }
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
  for (size_t i = 0; i < DMX_NUM_SPOTS; i++) {
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
    for (size_t j = 0; j < DMX_NUM_SPOTS; j++) {
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

    // Conversion en uint8_t pour les spots
    spots[i].red = (uint8_t)smoothR[i];
    spots[i].green = (uint8_t)smoothG[i];
    spots[i].blue = (uint8_t)smoothB[i];
    spots[i].white = (uint8_t)smoothW[i];

    // Application de la correction du profil couleur sur RGB
    applyColorProfile(&spots[i].red, &spots[i].green, &spots[i].blue, redFactor,
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
// Function to set custom baud rate using termios2 API
int set_custom_baudrate_termios2(int fd, int baud, int silent) {
#if HAVE_TERMIOS2
    struct termios2 tio2;
    
    // Get current termios2 settings
    if (ioctl(fd, TCGETS2, &tio2) < 0) {
        if (!silent)
            printf("⚠️  TCGETS2 failed, termios2 not available: %s\n", strerror(errno));
        return -1;
    }
    
    // Configure for custom baud rate
    tio2.c_cflag &= ~CBAUD;     // Clear baud rate mask
    tio2.c_cflag |= BOTHER;     // Enable custom baud rate
    tio2.c_ispeed = baud;       // Set input speed
    tio2.c_ospeed = baud;       // Set output speed
    
    // Apply the new settings
    if (ioctl(fd, TCSETS2, &tio2) < 0) {
        if (!silent)
            printf("⚠️  TCSETS2 failed: %s\n", strerror(errno));
        return -1;
    }
    
    // Verify the baud rate was set correctly
    if (ioctl(fd, TCGETS2, &tio2) == 0) {
        if (!silent) {
            printf("✅ termios2 baud rate configured successfully\n");
            printf("✅ Input speed: %u, Output speed: %u\n", tio2.c_ispeed, tio2.c_ospeed);
        }
        
        // Check if the speeds match what we requested
        if (tio2.c_ispeed == (unsigned int)baud && tio2.c_ospeed == (unsigned int)baud) {
            if (!silent)
                printf("🎉 Exact DMX baud rate achieved: %d bps\n", baud);
            return 0;
        } else {
            if (!silent)
                printf("⚠️  Baud rate mismatch - requested: %d, got: %u/%u\n", 
                       baud, tio2.c_ispeed, tio2.c_ospeed);
            return -1;
        }
    }
    
    return -1;
#else
    (void)fd; (void)baud; (void)silent; // Suppress unused warnings
    return -1; // termios2 headers not available
#endif
}

// Function to set custom baud rate using FTDI-specific ioctl
int set_custom_baudrate_ftdi(int fd, int baud, int silent) {
    struct serial_struct ser;
    
    // Get current serial settings
    if (ioctl(fd, TIOCGSERIAL, &ser) < 0) {
        if (!silent)
            printf("⚠️  TIOCGSERIAL failed: %s\n", strerror(errno));
        return -1;
    }
    
    // Calculate the divisor for the desired baud rate
    // FTDI chips typically use a base clock of 3MHz or 48MHz
    // For 250000 bps, we need divisor = base_clock / (16 * baud_rate)
    // Try common FTDI base frequencies
    int base_clocks[] = {3000000, 48000000, 12000000};
    int num_clocks = sizeof(base_clocks) / sizeof(base_clocks[0]);
    
    for (int i = 0; i < num_clocks; i++) {
        int divisor = base_clocks[i] / (16 * baud);
        if (divisor > 0 && divisor <= 65535) { // Valid divisor range
            ser.custom_divisor = divisor;
            ser.baud_base = base_clocks[i] / 16;
            ser.flags &= ~ASYNC_SPD_MASK;
            ser.flags |= ASYNC_SPD_CUST;
            
            if (ioctl(fd, TIOCSSERIAL, &ser) == 0) {
                // Verify by reading back
                if (ioctl(fd, TIOCGSERIAL, &ser) == 0) {
                    int actual_baud = ser.baud_base / ser.custom_divisor;
                    if (!silent) {
                        printf("✅ FTDI custom baud rate configured\n");
                        printf("✅ Base clock: %d Hz, Divisor: %d, Actual baud: %d\n",
                               base_clocks[i], divisor, actual_baud);
                    }
                    
                    // Check if we're close enough (within 1% tolerance)
                    if (abs(actual_baud - baud) <= (baud / 100)) {
                        if (!silent)
                            printf("🎉 FTDI baud rate within tolerance: %d bps (target: %d)\n", 
                                   actual_baud, baud);
                        return 0;
                    }
                }
            }
        }
    }
    
    if (!silent)
        printf("⚠️  FTDI custom baud rate configuration failed\n");
    return -1;
}
#endif

int send_dmx_frame(int fd, unsigned char *frame, size_t len) {
  // Set break condition (100 µs) then clear and wait for 12 µs (Mark After
  // Break)
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

int init_Dmx(const char *port, int silent) {
  int fd;
  struct termios tty;

  // Ne pas installer de gestionnaire de signal ici
  // Le gestionnaire principal dans main.c s'en occupe déjà

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

  if (!silent) {
#ifdef __APPLE__
    printf("Baud rate: %lu\n", cfgetispeed(&tty));
    printf("c_cflag: 0x%lx\n", tty.c_cflag);
    printf("c_iflag: 0x%lx\n", tty.c_iflag);
    printf("c_oflag: 0x%lx\n", tty.c_oflag);
    printf("c_lflag: 0x%lx\n", tty.c_lflag);
#else
    printf("Baud rate: %u\n", cfgetispeed(&tty));
    printf("c_cflag: 0x%x\n", tty.c_cflag);
    printf("c_iflag: 0x%x\n", tty.c_iflag);
    printf("c_oflag: 0x%x\n", tty.c_oflag);
    printf("c_lflag: 0x%x\n", tty.c_lflag);
#endif
  }

#ifdef __APPLE__
  speed_t speed = 9600; // Variable utilisée pour macOS seulement
  // MacOS utilise IOSSIOSPEED pour définir des baudrates personnalisés
  if (ioctl(fd, IOSSIOSPEED, &speed) < 0) {
    if (!silent)
      perror("Error setting custom baud rate");
  } else {
    if (!silent)
      printf("Custom baud rate set successfully!\n");
  }
#else
  // Sur Linux, on utilise les constantes B* standard
  cfsetispeed(&tty, B9600);
  cfsetospeed(&tty, B9600);
  if (!silent)
    printf("Standard baud rate set\n");
#endif

  if (!silent) {
#ifdef __APPLE__
    printf("Baud rate after setting: %lu\n", cfgetispeed(&tty));
#else
    printf("Baud rate after setting: %u\n", cfgetispeed(&tty));
#endif
  }

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

  // Configuration du baud rate DMX - approche multi-niveaux robuste
#ifdef __APPLE__
  speed = DMX_BAUD;
  if (ioctl(fd, IOSSIOSPEED, &speed) < 0) {
    if (!silent)
      perror("Error setting custom baud rate");
    close(fd);
    return -1;
  }
  if (!silent)
    printf("DMX baud rate set to %d using IOSSIOSPEED\n", DMX_BAUD);
#else
  // Linux multi-level baud rate configuration
  int baud_configured = 0;
  
  if (!silent)
    printf("🔧 Configuring DMX baud rate (%d bps) using multi-level approach...\n", DMX_BAUD);
  
  // Level 1: Try termios2 API for exact baud rate
  if (!baud_configured) {
    if (!silent)
      printf("📋 Attempting termios2 configuration...\n");
    
    if (set_custom_baudrate_termios2(fd, DMX_BAUD, silent) == 0) {
      baud_configured = 1;
      if (!silent)
        printf("✅ DMX baud rate configured via termios2\n");
    }
  }
  
  // Level 2: Try FTDI-specific ioctl
  if (!baud_configured) {
    if (!silent)
      printf("📋 Attempting FTDI-specific configuration...\n");
    
    if (set_custom_baudrate_ftdi(fd, DMX_BAUD, silent) == 0) {
      baud_configured = 1;
      if (!silent)
        printf("✅ DMX baud rate configured via FTDI ioctl\n");
    }
  }
  
  // Level 3: Fallback to standard baud rates
  if (!baud_configured) {
    if (!silent)
      printf("📋 Falling back to standard baud rates...\n");
    
    // Try standard rates in order of preference (closest to DMX_BAUD)
    speed_t fallback_rates[] = {B230400, B115200, B57600, B38400, B19200};
    const char* fallback_names[] = {"230400", "115200", "57600", "38400", "19200"};
    int num_fallbacks = sizeof(fallback_rates) / sizeof(fallback_rates[0]);
    
    for (int i = 0; i < num_fallbacks; i++) {
      // Get current settings
      struct termios fallback_tty;
      if (tcgetattr(fd, &fallback_tty) == 0) {
        // Set the fallback baud rate
        cfsetispeed(&fallback_tty, fallback_rates[i]);
        cfsetospeed(&fallback_tty, fallback_rates[i]);
        
        if (tcsetattr(fd, TCSANOW, &fallback_tty) == 0) {
          // Verify it worked
          if (tcgetattr(fd, &fallback_tty) == 0) {
            speed_t actual_speed = cfgetispeed(&fallback_tty);
            if (actual_speed == fallback_rates[i]) {
              baud_configured = 1;
              if (!silent) {
                printf("✅ DMX fallback baud rate configured: %s bps\n", fallback_names[i]);
                printf("⚠️  Note: Using %s instead of target %d bps\n", fallback_names[i], DMX_BAUD);
              }
              break;
            }
          }
        }
      }
    }
  }
  
  // Final verification and reporting
  struct termios verify_tty;
  if (tcgetattr(fd, &verify_tty) == 0) {
    speed_t final_speed = cfgetispeed(&verify_tty);
    if (!silent) {
      printf("✅ Final verification - baud rate value: %u\n", final_speed);
      printf("✅ Final c_cflag: 0x%x\n", verify_tty.c_cflag);
      
      if (baud_configured) {
        printf("🎉 DMX baud rate successfully configured!\n");
      } else {
        printf("❌ Failed to configure any DMX baud rate\n");
        close(fd);
        return -1;
      }
    }
  }
#endif

  if (!silent)
    printf("Serial port opened and configured successfully.\n");
  return fd;
}
