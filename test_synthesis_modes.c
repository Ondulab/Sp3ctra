/* test_synthesis_modes.c - Test program to verify synthesis mode configuration
 */

#include "config.h"
#include <stdio.h>

int main() {
  printf("=== Sp3ctra Synthesis Mode Configuration Test ===\n\n");

  // Display current configuration
  printf("Current SYNTH_MODE: %d\n", SYNTH_MODE);

  // Test mode detection macros
  printf("IS_STEREO_MODE(): %s\n", IS_STEREO_MODE() ? "true" : "false");
  printf("IS_WHITE_BACKGROUND(): %s\n",
         IS_WHITE_BACKGROUND() ? "true" : "false");
  printf("COLOR_INVERTED (legacy): %s\n", COLOR_INVERTED ? "true" : "false");

  // Display mode interpretation
  printf("\nMode interpretation:\n");
  switch (SYNTH_MODE) {
  case SYNTH_MODE_MONO_WHITE_BG:
    printf("- Mono synthesis with white background\n");
    printf("- Dark pixels = more audio energy\n");
    break;
  case SYNTH_MODE_MONO_BLACK_BG:
    printf("- Mono synthesis with black background\n");
    printf("- Light pixels = more audio energy\n");
    break;
  case SYNTH_MODE_STEREO_WHITE_BG:
    printf("- Stereo synthesis with white background\n");
    printf("- Red channel -> Left audio, Blue channel -> Right audio\n");
    printf("- Dark pixels = more audio energy\n");
    break;
  case SYNTH_MODE_STEREO_BLACK_BG:
    printf("- Stereo synthesis with black background\n");
    printf("- Red channel -> Left audio, Blue channel -> Right audio\n");
    printf("- Light pixels = more audio energy\n");
    break;
  default:
    printf("- Unknown mode!\n");
    break;
  }

  printf("\nAudio processing function selection:\n");
  if (IS_STEREO_MODE()) {
    printf("- Using synth_AudioProcessStereo()\n");
    printf("- Independent processing for left and right channels\n");
  } else {
    printf("- Using synth_AudioProcess() (mono)\n");
    printf("- Single channel processing\n");
  }

  printf("\nColor inversion logic:\n");
  if (IS_WHITE_BACKGROUND()) {
    printf("- White background mode: 255 - pixel_value\n");
    printf("- Dark areas produce more sound\n");
  } else {
    printf("- Black background mode: pixel_value (no inversion)\n");
    printf("- Light areas produce more sound\n");
  }

  printf("\n=== Test completed successfully! ===\n");
  return 0;
}
