#!/bin/bash

echo "🎵 Script de test 96kHz avec buffer optimisé"
echo "============================================="

# Sauvegarder la config originale
cp src/core/config.h src/core/config.h.bak

# Créer une config temporaire pour 96kHz
cat > src/core/config_temp.h << 'EOF'
/* config.h - Configuration temporaire 96kHz */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/**************************************************************************************
 * Debug Definitions
 **************************************************************************************/
#define DEBUG_MIDI

/**************************************************************************************
 * Mode Definitions
 **************************************************************************************/
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

#if (CIS_MAX_PIXELS_NB % UDP_MAX_NB_PACKET_PER_LINE) != 0
#error "CIS_MAX_PIXELS_NB must be divisible by UDP_MAX_NB_PACKET_PER_LINE."
#endif

#define UDP_LINE_FRAGMENT_SIZE (CIS_MAX_PIXELS_NB / UDP_MAX_NB_PACKET_PER_LINE)
#define PORT (55151)
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
#define DMX_SATURATION_FACTOR (2.9)
#define DMX_SMOOTHING_FACTOR (0.80)
#define DMX_GAMMA (1.2)
#define DMX_BLACK_THRESHOLD (0.3)
#define DMX_RESPONSE_CURVE (2.5)
#define DMX_ZONE_OVERLAP (0.15)
#define DMX_WHITE_THRESHOLD (220)
#define DMX_SIGNIFICANT_WEIGHT (5.0)
#define DMX_MIN_BLOB_SIZE (2)
#define DMX_COLOR_SIMILARITY_THRESHOLD (1000)
#define DMX_MAX_BLOBS_PER_ZONE (15)
#define DMX_MAX_ZONE_SIZE (500)
#define DMX_LOW_INTENSITY_THRESHOLD (0.15)
#define DMX_DARK_SMOOTHING_FACTOR (0.98)
#define DMX_UNIFORM_THRESHOLD (8.0)
#define DMX_MIN_STD_DEV (0.03)
#define DMX_NUM_SPOTS (18)

/**************************************************************************************
 * DAC Definitions - 96kHz OPTIMIZED
 **************************************************************************************/
#define SAMPLING_FREQUENCY (96000)
#define AUDIO_CHANNEL (2)
#define AUDIO_BUFFER_SIZE (300)  // 300 frames = 3.125ms at 96kHz

/**************************************************************************************
 * Synth Definitions
 **************************************************************************************/
#define SIN
#define GAP_LIMITER
#define COLOR_INVERTED
#define ENABLE_NON_LINEAR_MAPPING 1

/**************************************************************************************
 * Synth and Image Processing Configuration
 **************************************************************************************/
#define CONTRAST_MIN 0.00f
#define CONTRAST_STRIDE 4.0f
#define CONTRAST_ADJUSTMENT_POWER 1.5f
#define GAMMA_VALUE 1.8f
#define LOG_FREQUENCY (SAMPLING_FREQUENCY / AUDIO_BUFFER_SIZE)

/**************************************************************************************
 * Wave Generation Definitions
 **************************************************************************************/
#define WAVE_AMP_RESOLUTION (16777215)
#define VOLUME_AMP_RESOLUTION (65535)
#define START_FREQUENCY (65.41)
#define MAX_OCTAVE_NUMBER (8)
#define SEMITONE_PER_OCTAVE (12)
#define COMMA_PER_SEMITONE (36)
#define VOLUME_INCREMENT (1)
#define VOLUME_DECREMENT (1)
#define PIXELS_PER_NOTE (1)
#define NUMBER_OF_NOTES (CIS_MAX_PIXELS_NB / PIXELS_PER_NOTE)

/**************************************************************************************
 * Audio Effects Definitions
 **************************************************************************************/
#define ENABLE_REVERB 0
#define DEFAULT_REVERB_MIX 0.3f
#define DEFAULT_REVERB_ROOM_SIZE 0.7f
#define DEFAULT_REVERB_DAMPING 0.4f
#define DEFAULT_REVERB_WIDTH 1.0f

/**************************************************************************************
 * Display Definitions
 **************************************************************************************/
#define WINDOWS_WIDTH (CIS_MAX_PIXELS_NB)
#define WINDOWS_HEIGHT (1160)

#endif // __CONFIG_H__
EOF

# Remplacer temporairement la config
mv src/core/config.h src/core/config.h.original
mv src/core/config_temp.h src/core/config.h

echo "🔧 Compilation avec config 96kHz (buffer 300 frames)..."
make clean -C build_nogui
make -C build_nogui

if [ $? -eq 0 ]; then
    echo "✅ Compilation réussie !"
    echo "🎵 Test 96kHz disponible : ./build_nogui/CISYNTH_noGUI --cli --no-dmx"
    echo ""
    echo "Paramètres optimisés pour 96kHz :"
    echo "- Fréquence : 96000 Hz"
    echo "- Buffer : 300 frames"
    echo "- Latence : 3.125ms"
    echo ""
    echo "Pour restaurer la config 48kHz : ./restore_48khz.sh"
else
    echo "❌ Erreur de compilation"
    # Restaurer la config originale en cas d'erreur
    mv src/core/config.h.original src/core/config.h
fi
