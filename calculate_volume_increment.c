#include <math.h>
#include <stdint.h>
#include <stdio.h>

// Constantes du projet
#define WAVE_AMP_RESOLUTION (16777215)
#define VOLUME_AMP_RESOLUTION (65535)
#define START_FREQUENCY (65.41)
#define SAMPLING_FREQUENCY (48000)
#define SEMITONE_PER_OCTAVE (12)
#define COMMA_PER_SEMITONE (36)
#define PI (3.14159265358979323846)

// Structure simplifiée pour les calculs
struct wave_calc {
  float frequency;
  uint32_t area_size;
  uint32_t octave_coeff;
  float max_volume_increment;
  float amplitude_at_octave_pos;
};

// Fonction pour calculer la fréquence (copie de wave_generation.c)
static float calculate_frequency(uint32_t comma_cnt) {
  float frequency =
      START_FREQUENCY *
      pow(2, ((float)comma_cnt /
              (12.0 * ((SEMITONE_PER_OCTAVE * (float)COMMA_PER_SEMITONE) /
                       (12.00 / (log(2)) *
                        log((START_FREQUENCY * 2.00) / START_FREQUENCY))))));
  return frequency;
}

int main() {
  printf("=== CALCUL DE max_volume_increment POUR DIFFÉRENTES NOTES ===\n\n");

  // Facteur de normalisation
  float normalization_factor =
      (float)WAVE_AMP_RESOLUTION / (float)VOLUME_AMP_RESOLUTION;
  printf("Facteur de normalisation = %.2f\n\n", normalization_factor);

  // Exemples pour différentes notes et octaves
  printf("%-8s %-10s %-12s %-15s %-18s %-20s\n", "Note", "Fréquence", "Octave",
         "octave_coeff", "Amplitude@pos", "max_vol_incr");
  printf("%-8s %-10s %-12s %-15s %-18s %-20s\n", "----", "---------", "------",
         "-----------", "-------------", "-------------");

  // Calculer pour quelques notes représentatives
  uint32_t test_notes[] = {0, 36, 72, 108, 144}; // Do de différentes octaves

  for (int i = 0; i < 5; i++) {
    uint32_t comma_cnt = test_notes[i];

    // Calculer la fréquence de base (octave 0)
    float base_frequency = calculate_frequency(
        comma_cnt % (SEMITONE_PER_OCTAVE * COMMA_PER_SEMITONE));

    // Calculer l'octave
    uint32_t octave = comma_cnt / (SEMITONE_PER_OCTAVE * COMMA_PER_SEMITONE);

    // Calculer les paramètres de la note
    struct wave_calc note;
    note.frequency = base_frequency * pow(2, octave);
    note.area_size = (uint32_t)(SAMPLING_FREQUENCY / base_frequency);
    note.octave_coeff = pow(2, octave);

    // Calculer l'amplitude à la position octave_coeff dans la sinusoïde
    // Reproduit le calcul de calculate_waveform()
    note.amplitude_at_octave_pos =
        sin((note.octave_coeff * 2.0 * PI) / (float)note.area_size) *
        (WAVE_AMP_RESOLUTION / 2.0);

    // Calculer max_volume_increment
    note.max_volume_increment =
        note.amplitude_at_octave_pos / normalization_factor;

    printf("%-8d %-10.2f %-12d %-15d %-18.0f %-20.2f\n", comma_cnt,
           note.frequency, octave, note.octave_coeff,
           note.amplitude_at_octave_pos, note.max_volume_increment);
  }

  printf("\n=== ANALYSE DÉTAILLÉE POUR QUELQUES CAS ===\n\n");

  // Analyse détaillée pour 3 cas spécifiques
  uint32_t detailed_cases[] = {0, 432, 864}; // Do grave, Do médium, Do aigu

  for (int i = 0; i < 3; i++) {
    uint32_t comma_cnt = detailed_cases[i];
    uint32_t octave = comma_cnt / (SEMITONE_PER_OCTAVE * COMMA_PER_SEMITONE);

    float base_frequency = calculate_frequency(
        comma_cnt % (SEMITONE_PER_OCTAVE * COMMA_PER_SEMITONE));
    float frequency = base_frequency * pow(2, octave);
    uint32_t area_size = (uint32_t)(SAMPLING_FREQUENCY / base_frequency);
    uint32_t octave_coeff = pow(2, octave);

    printf("--- Note %d (Octave %d) ---\n", comma_cnt, octave);
    printf("Fréquence de base: %.2f Hz\n", base_frequency);
    printf("Fréquence finale: %.2f Hz\n", frequency);
    printf("area_size: %d échantillons\n", area_size);
    printf("octave_coeff: %d\n", octave_coeff);

    // Position dans la sinusoïde
    float angle_rad = (octave_coeff * 2.0 * PI) / (float)area_size;
    float amplitude = sin(angle_rad) * (WAVE_AMP_RESOLUTION / 2.0);
    float max_vol_incr = amplitude / normalization_factor;

    printf("Angle dans sinusoïde: %.4f radians (%.1f degrés)\n", angle_rad,
           angle_rad * 180.0 / PI);
    printf("Amplitude échantillonnée: %.0f\n", amplitude);
    printf("max_volume_increment: %.2f\n", max_vol_incr);
    printf("\n");
  }

  return 0;
}
