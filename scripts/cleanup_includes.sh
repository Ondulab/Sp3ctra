#!/bin/bash
# Script to remove includes of deleted files
# Branch: feature/vst-plugin
# Date: 15/01/2026

set -e

echo "🧹 Cleaning up includes of deleted files..."

# Files to process
FILES=(
  "src/audio/rtaudio/audio_rtaudio.cpp"
  "src/audio/rtaudio/audio_rtaudio.h"
  "src/communication/midi/midi_callbacks.cpp"
  "src/communication/midi/midi_callbacks.h"
  "src/communication/midi/midi_controller.cpp"
  "src/config/config_loader.h"
  "src/config/config_parser_table.h"
  "src/core/main.c"
  "src/processing/image_preprocessor.c"
  "src/processing/image_preprocessor.h"
  "src/synthesis/luxstral/synth_luxstral.c"
  "src/synthesis/luxstral/synth_luxstral_algorithms.c"
  "src/synthesis/luxstral/synth_luxstral_threading.c"
  "src/threading/multithreading.c"
)

# Patterns to remove (includes of deleted files)
PATTERNS=(
  'config_dmx\.h'
  'config_display\.h'
  'auto_volume\.h'
  'image_debug\.h'
  'dmx\.h'
  'display\.h'
  'ZitaRev1\.h'
  'three_band_eq\.h'
  'pareq\.h'
  'config_display_loader\.h'
)

COUNT=0

for file in "${FILES[@]}"; do
  if [ -f "$file" ]; then
    echo "Processing: $file"
    for pattern in "${PATTERNS[@]}"; do
      # Remove lines containing #include "..."pattern"
      sed -i '' "/^#include.*${pattern}/d" "$file"
      # Also remove lines with ../ paths
      sed -i '' "/^#include.*\.\.\/.*${pattern}/d" "$file"
    done
    COUNT=$((COUNT + 1))
  else
    echo "⚠️  File not found: $file"
  fi
done

echo "✅ Cleaned $COUNT files"
echo "🔍 Checking for remaining problematic includes..."

# Check if any problematic includes remain
REMAINING=$(grep -r --include="*.h" --include="*.c" --include="*.cpp" -l "config_dmx.h\|config_display.h\|auto_volume.h\|image_debug.h\|dmx.h\|display.h\|ZitaRev1.h\|three_band_eq.h\|pareq.h" src/ 2>/dev/null | wc -l)

if [ "$REMAINING" -eq 0 ]; then
  echo "✅ All problematic includes removed!"
else
  echo "⚠️  $REMAINING files still have problematic includes:"
  grep -r --include="*.h" --include="*.c" --include="*.cpp" -l "config_dmx.h\|config_display.h\|auto_volume.h\|image_debug.h\|dmx.h\|display.h\|ZitaRev1.h\|three_band_eq.h\|pareq.h" src/ 2>/dev/null
fi

echo "✅ Include cleanup complete"
