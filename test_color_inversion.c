/*
 * Test program to verify red/blue color inversion fix
 * This program tests the extractWarmChannel and extractColdChannel functions
 * to ensure that red pixels activate the warm channel and blue pixels activate
 * the cold channel
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Include necessary definitions from config.h
#define SYNTH_MODE_STEREO_WHITE_BG 2
#define SYNTH_MODE SYNTH_MODE_STEREO_WHITE_BG
#define IS_WHITE_BACKGROUND() ((SYNTH_MODE == 0) || (SYNTH_MODE == 2))
#define VOLUME_AMP_RESOLUTION (65535)
#define PERCEPTUAL_WEIGHT_R (0.21f)
#define PERCEPTUAL_WEIGHT_G (0.72f)
#define PERCEPTUAL_WEIGHT_B (0.07f)
#define OPPONENT_ALPHA (1.0f)
#define OPPONENT_BETA (0.5f)
#define CHROMATIC_THRESHOLD (0.1f)
#define ACHROMATIC_SPLIT (0.5f)

// Function prototypes (copied from synth_additive.c)
uint32_t extractWarmChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *warm_output,
                            uint32_t size);
uint32_t extractColdChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *cold_output,
                            uint32_t size);

// Simplified implementations for testing
uint32_t extractWarmChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *warm_output,
                            uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    // Step 1: Convert RGB to normalized [0..1] values
    float r_norm = (float)buffer_R[i] / 255.0f;
    float g_norm = (float)buffer_G[i] / 255.0f;
    float b_norm = (float)buffer_B[i] / 255.0f;

    // Step 2: Calculate perceptual luminance Y
    float luminance_Y = PERCEPTUAL_WEIGHT_R * r_norm +
                        PERCEPTUAL_WEIGHT_G * g_norm +
                        PERCEPTUAL_WEIGHT_B * b_norm;

    // Step 3: Calculate opponent axes (CORRECTED)
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

    warm_output[i] = final_value;
  }
  return 0;
}

uint32_t extractColdChannel(uint8_t *buffer_R, uint8_t *buffer_G,
                            uint8_t *buffer_B, int32_t *cold_output,
                            uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    // Step 1: Convert RGB to normalized [0..1] values
    float r_norm = (float)buffer_R[i] / 255.0f;
    float g_norm = (float)buffer_G[i] / 255.0f;
    float b_norm = (float)buffer_B[i] / 255.0f;

    // Step 2: Calculate perceptual luminance Y
    float luminance_Y = PERCEPTUAL_WEIGHT_R * r_norm +
                        PERCEPTUAL_WEIGHT_G * g_norm +
                        PERCEPTUAL_WEIGHT_B * b_norm;

    // Step 3: Calculate opponent axes (CORRECTED)
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

    cold_output[i] = final_value;
  }
  return 0;
}

int main() {
  printf("=== Test de correction de l'inversion rouge-bleu ===\n\n");

  // Test cases: pure colors on white background
  uint8_t test_colors[][3] = {
      {255, 0, 0},     // Pure red
      {0, 0, 255},     // Pure blue
      {0, 255, 0},     // Pure green
      {255, 255, 255}, // White
      {0, 0, 0},       // Black
      {128, 128, 128}, // Gray
      {255, 128, 0},   // Orange (warm)
      {0, 128, 255}    // Cyan (cold)
  };

  const char *color_names[] = {"Rouge pur", "Bleu pur", "Vert pur", "Blanc",
                               "Noir",      "Gris",     "Orange",   "Cyan"};

  int num_tests = sizeof(test_colors) / sizeof(test_colors[0]);

  for (int i = 0; i < num_tests; i++) {
    uint8_t r = test_colors[i][0];
    uint8_t g = test_colors[i][1];
    uint8_t b = test_colors[i][2];

    int32_t warm_output, cold_output;

    extractWarmChannel(&r, &g, &b, &warm_output, 1);
    extractColdChannel(&r, &g, &b, &cold_output, 1);

    printf("%-12s RGB(%3d,%3d,%3d) -> Warm: %5d, Cold: %5d", color_names[i], r,
           g, b, warm_output, cold_output);

    // Analyze results
    if (i == 0) { // Pure red
      if (warm_output > cold_output) {
        printf(" ✅ Rouge active plus le canal warm");
      } else {
        printf(" ❌ Rouge devrait activer plus le canal warm");
      }
    } else if (i == 1) { // Pure blue
      if (cold_output > warm_output) {
        printf(" ✅ Bleu active plus le canal cold");
      } else {
        printf(" ❌ Bleu devrait activer plus le canal cold");
      }
    }

    printf("\n");
  }

  printf("\n=== Résumé ===\n");
  printf("Mode: SYNTH_MODE_STEREO_WHITE_BG\n");
  printf("Fond blanc: les pixels sombres génèrent plus d'énergie\n");
  printf("Axe opponent corrigé: O_rb = b_norm - r_norm\n");
  printf("Canal warm (gauche): devrait réagir plus au rouge\n");
  printf("Canal cold (droit): devrait réagir plus au bleu\n");

  return 0;
}
