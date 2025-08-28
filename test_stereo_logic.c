/* test_stereo_logic.c - Test pour comprendre la logique stéréo */

#include "config.h"
#include <stdint.h>
#include <stdio.h>

// Simuler les fonctions d'extraction
uint32_t extractRedChannel_test(uint8_t red_value) {
  if (IS_WHITE_BACKGROUND()) {
    // White background mode: invert colors (dark pixels = more sound)
    return (uint32_t)(((255UL - red_value) * 65535UL) / 255UL);
  } else {
    // Black background mode: direct conversion (light pixels = more sound)
    return (uint32_t)((red_value * 65535UL) / 255UL);
  }
}

uint32_t extractBlueChannel_test(uint8_t blue_value) {
  if (IS_WHITE_BACKGROUND()) {
    // White background mode: invert colors (dark pixels = more sound)
    return (uint32_t)(((255UL - blue_value) * 65535UL) / 255UL);
  } else {
    // Black background mode: direct conversion (light pixels = more sound)
    return (uint32_t)((blue_value * 65535UL) / 255UL);
  }
}

uint32_t greyScale_test(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t weighted = (r * 299 + g * 587 + b * 114);
  // Normalisation en 16 bits (0 - 65535)
  return (uint32_t)((weighted * 65535UL) / 255000UL);
}

int main() {
  printf("=== Test de la logique Stéréo vs Mono ===\n\n");

  // Test avec différents scénarios
  printf("Configuration actuelle: SYNTH_MODE = %d\n", SYNTH_MODE);
  printf("IS_STEREO_MODE(): %s\n", IS_STEREO_MODE() ? "true" : "false");
  printf("IS_WHITE_BACKGROUND(): %s\n",
         IS_WHITE_BACKGROUND() ? "true" : "false");
  printf("\n");

  // Scénario 1: Pixels rouges sur fond blanc
  printf("=== Scénario 1: Pixels rouges (255,0,0) sur fond blanc ===\n");
  uint8_t red_pixel_r = 255, red_pixel_g = 0, red_pixel_b = 0;
  uint32_t mono_result = greyScale_test(red_pixel_r, red_pixel_g, red_pixel_b);
  uint32_t stereo_left = extractRedChannel_test(red_pixel_r);
  uint32_t stereo_right = extractBlueChannel_test(red_pixel_b);

  printf("Mono (greyScale): %u\n", mono_result);
  printf("Stéréo Left (rouge): %u\n", stereo_left);
  printf("Stéréo Right (bleu): %u\n", stereo_right);
  printf("\n");

  // Scénario 2: Pixels noirs sur fond blanc
  printf("=== Scénario 2: Pixels noirs (0,0,0) sur fond blanc ===\n");
  uint8_t black_pixel_r = 0, black_pixel_g = 0, black_pixel_b = 0;
  mono_result = greyScale_test(black_pixel_r, black_pixel_g, black_pixel_b);
  stereo_left = extractRedChannel_test(black_pixel_r);
  stereo_right = extractBlueChannel_test(black_pixel_b);

  printf("Mono (greyScale): %u\n", mono_result);
  printf("Stéréo Left (rouge): %u\n", stereo_left);
  printf("Stéréo Right (bleu): %u\n", stereo_right);
  printf("\n");

  // Scénario 3: Fond blanc
  printf("=== Scénario 3: Fond blanc (255,255,255) ===\n");
  uint8_t white_r = 255, white_g = 255, white_b = 255;
  mono_result = greyScale_test(white_r, white_g, white_b);
  stereo_left = extractRedChannel_test(white_r);
  stereo_right = extractBlueChannel_test(white_b);

  printf("Mono (greyScale): %u\n", mono_result);
  printf("Stéréo Left (rouge): %u\n", stereo_left);
  printf("Stéréo Right (bleu): %u\n", stereo_right);
  printf("\n");

  printf("=== Analyse ===\n");
  printf("PROBLÈME IDENTIFIÉ:\n");
  printf("- En mode mono: pixels noirs (0,0,0) donnent maximum de son\n");
  printf("- En mode stéréo: pixels rouges (255,0,0) ne donnent du son QUE dans "
         "le canal gauche\n");
  printf("- Pour avoir du son stéréo équivalent au mono, il faudrait des "
         "pixels violets (255,0,255)\n");
  printf("  ou traiter différemment l'extraction des canaux.\n");

  return 0;
}
