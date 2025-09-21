/* config_debug.h */

#ifndef __CONFIG_DEBUG_H__
#define __CONFIG_DEBUG_H__

/**************************************************************************************
 * DEBUG SYSTÈME GÉNÉRAL
 * 
 * Fonctionnalités de debug générales qui ne dépendent pas des master switches
 **************************************************************************************/

// #define PRINT_FPS                    // Enable FPS counter printing
// #define DEBUG_BUFFERS                // Enable verbose buffer swap logging
// #define DEBUG_IMU_PACKETS            // Enable IMU packet reception logging

/**************************************************************************************
 * DEBUG COMMUNICATION
 * 
 * Dépendance: ENABLE_COMMUNICATION_DEBUG (optionnel pour compatibilité)
 **************************************************************************************/

// #define DEBUG_MIDI                   // Enable MIDI debug logging
// #define DEBUG_UDP                    // Enable verbose UDP logging

/**************************************************************************************
 * DEBUG AUDIO GÉNÉRAL
 * 
 * Dépendance: ENABLE_AUDIO_DEBUG (recommandé mais pas obligatoire pour compatibilité)
 **************************************************************************************/

// #define DEBUG_AUDIO_SIGNAL           // Enable signal debug logging (dry_L, dry_R, levels)
// #define DEBUG_AUDIO_REVERB           // Enable reverb debug logging (conditions, calls, output)
// #define DEBUG_AUDIO_INTERFACE        // Enable audio interface debug logging
// #define DEBUG_AUTO_VOLUME            // Enable auto-volume debug logging
// #define DEBUG_LOCK_FREE_PAN          // Enable lock-free pan system debug logging
// #define DEBUG_RGB_TEMPERATURE        // Enable RGB color temperature debug logging (Note X: RGB(...) -> Temp=...)

/**************************************************************************************
 * DEBUG SYNTHESIS ADDITIVE
 * 
 * Dépendances: 
 * - DEBUG_OSCILLATOR_VOLUME_SCAN requires ENABLE_IMAGE_DEBUG + DEBUG_OSCILLATOR_VOLUMES
 * - DEBUG_OSCILLATOR_VOLUMES peut fonctionner seul (mode basique)
 **************************************************************************************/

//#define DEBUG_OSCILLATOR_VOLUMES        // Enable oscillator volume capture (basic mode)
//#define DEBUG_OSCILLATOR_VOLUME_SCAN    // Enable temporal scan of oscillator volumes (advanced mode)

// Configuration des paramètres oscillateur debug
#define OSCILLATOR_VOLUME_SCAN_SAMPLES 48000                  // Oscillator volume scan configuration (1 second at 48kHz sampling rate)
#define OSCILLATOR_VOLUME_SCAN_AUTO_SAVE 48000                // Auto-save threshold for oscillator volume scans (auto-save every 1 second)

// Configuration des markers visuels
// #define DEBUG_OSCILLATOR_SCAN_MARKERS   // Enable visual markers in oscillator volume scans (comment out to disable yellow/blue markers)

// Configuration de capture d'images brutes du scanner
// NOTE: Raw scanner capture is now configured at runtime via --debug-image argument
// The old #define DEBUG_RAW_SCANNER_CAPTURE and RAW_SCANNER_CAPTURE_LINES are no longer used
// Use: ./Sp3ctra --debug-image        (default: 1000 lines)
// Use: ./Sp3ctra --debug-image=2000   (custom number of lines)

// #define PRINT_IFFT_FREQUENCY         // Enable IFFT frequency printing
// #define PRINT_IFFT_FREQUENCY_FULL    // Enable full IFFT frequency printing

/**************************************************************************************
 * DEBUG SYNTHESIS POLYPHONIC
 * 
 * Section préparée pour le mode polyphonique
 **************************************************************************************/

// #define DEBUG_POLYPHONIC_VOICES      // Enable polyphonic voice debug logging
// #define DEBUG_POLYPHONIC_FFT         // Enable FFT analysis debug for polyphonic mode

/**************************************************************************************
 * DEBUG IMAGE PROCESSING
 * 
 * Dépendance OBLIGATOIRE: ENABLE_IMAGE_DEBUG
 * Tous ces flags sont ignorés si ENABLE_IMAGE_DEBUG n'est pas défini
 **************************************************************************************/

// #define DEBUG_IMAGE_TRANSFORMATIONS  // Enable image transformation visualization
//#define DEBUG_IMAGE_SAVE_TO_FILES       // Save debug images to PNG files
//#define DEBUG_IMAGE_STEREO_CHANNELS  // Enable stereo channel visualization (warm/cold)
// #define DEBUG_IMAGE_SHOW_HISTOGRAMS  // Show pixel value histograms
// #define DEBUG_IMAGE_FRAME_COUNTER    // Add frame counter to debug images
// #define DEBUG_TEMPORAL_SCAN             // Enable temporal scan functionality (auto-save scan images)

// Configuration des paramètres image debug
#define DEBUG_IMAGE_OUTPUT_DIR "./debug_images/"              // Debug image output directory (relative to executable)
#define DEBUG_IMAGE_CAPTURE_FREQUENCY 1                       // Debug image capture frequency (capture every N frames to avoid flooding)
#define DEBUG_IMAGE_MAX_FILES 100                             // Maximum number of debug images to keep (oldest files will be deleted)
//#define DEBUG_TEMPORAL_SCAN_MAX_LINES 10000                    // Temporal scan configuration (auto-save every N lines)
#define DEBUG_FORCE_CAPTURE_TEST_DATA 0                       // Force capture even with test pattern data

/**************************************************************************************
 * DEBUG PERFORMANCE & PROFILING
 * 
 * Dépendance: ENABLE_PERFORMANCE_DEBUG (recommandé)
 **************************************************************************************/

// #define DEBUG_THREAD_PERFORMANCE     // Enable thread performance monitoring
// #define DEBUG_MEMORY_USAGE           // Enable memory usage tracking
// #define DEBUG_CPU_AFFINITY           // Enable CPU affinity debug logging

/**************************************************************************************
 * VALIDATION INTELLIGENTE DES DÉPENDANCES
 * 
 * Validations restantes pour les dépendances qui ne peuvent pas être auto-activées
 **************************************************************************************/

// Validation pour DEBUG_OSCILLATOR_VOLUME_SCAN (nécessite DEBUG_OSCILLATOR_VOLUMES)
#ifdef DEBUG_OSCILLATOR_VOLUME_SCAN
    #ifndef DEBUG_OSCILLATOR_VOLUMES
        #error "❌ DEBUG_OSCILLATOR_VOLUME_SCAN requires DEBUG_OSCILLATOR_VOLUMES to be defined. Please uncomment DEBUG_OSCILLATOR_VOLUMES above."
    #endif
#endif

// Note: Les master switches (ENABLE_*) sont maintenant auto-activés, plus besoin de validation manuelle !

/**************************************************************************************
 * GUIDE D'UTILISATION RAPIDE
 * 
 * Pour activer le debug des oscillateurs (votre cas d'usage):
 * 1. Décommentez ENABLE_IMAGE_DEBUG (ligne ~15)
 * 2. DEBUG_OSCILLATOR_VOLUMES et DEBUG_OSCILLATOR_VOLUME_SCAN sont déjà activés
 * 3. Recompilez votre projet
 * 4. Cherchez les fichiers PNG dans ./debug_images/
 * 5. Cherchez les messages console commençant par "🔧 IMAGE_DEBUG:" et "🔧 OSCILLATOR_SCAN:"
 * 
 * Pour debug audio général:
 * 1. Décommentez ENABLE_AUDIO_DEBUG
 * 2. Activez les flags audio spécifiques dont vous avez besoin
 * 
 * Pour debug communication:
 * 1. Décommentez ENABLE_COMMUNICATION_DEBUG  
 * 2. Activez DEBUG_MIDI ou DEBUG_UDP selon vos besoins
 **************************************************************************************/

 /**************************************************************************************
 * AUTO-ACTIVATION INTELLIGENTE DES MASTER SWITCHES
 * 
 * Les master switches sont automatiquement activés quand des fonctionnalités 
 * correspondantes sont demandées. Plus besoin de se souvenir des dépendances !
 **************************************************************************************/

// Auto-enable ENABLE_IMAGE_DEBUG if any image debug feature is requested
#if defined(DEBUG_OSCILLATOR_VOLUME_SCAN) || defined(DEBUG_IMAGE_SAVE_TO_FILES) || defined(DEBUG_TEMPORAL_SCAN) || defined(DEBUG_IMAGE_TRANSFORMATIONS) || defined(DEBUG_IMAGE_STEREO_CHANNELS) || defined(DEBUG_IMAGE_SHOW_HISTOGRAMS) || defined(DEBUG_IMAGE_FRAME_COUNTER) || defined(DEBUG_RAW_SCANNER_CAPTURE)
    #ifndef ENABLE_IMAGE_DEBUG
        #define ENABLE_IMAGE_DEBUG  // Auto-activated for image debug features
        #pragma message "ℹ️  ENABLE_IMAGE_DEBUG auto-activated for image debug features"
    #endif
#endif

// Auto-enable ENABLE_AUDIO_DEBUG if any audio debug feature is requested
#if defined(DEBUG_AUDIO_SIGNAL) || defined(DEBUG_AUDIO_REVERB) || defined(DEBUG_LOCK_FREE_PAN) || defined(DEBUG_AUTO_VOLUME) || defined(DEBUG_AUDIO_INTERFACE) || defined(DEBUG_RGB_TEMPERATURE)
    #ifndef ENABLE_AUDIO_DEBUG
        #define ENABLE_AUDIO_DEBUG  // Auto-activated for audio debug features
        #pragma message "ℹ️  ENABLE_AUDIO_DEBUG auto-activated for audio debug features"
    #endif
#endif

// Auto-enable ENABLE_COMMUNICATION_DEBUG if any communication debug feature is requested
#if defined(DEBUG_MIDI) || defined(DEBUG_UDP)
    #ifndef ENABLE_COMMUNICATION_DEBUG
        #define ENABLE_COMMUNICATION_DEBUG  // Auto-activated for communication debug features
        #pragma message "ℹ️  ENABLE_COMMUNICATION_DEBUG auto-activated for communication debug features"
    #endif
#endif

// Auto-enable ENABLE_PERFORMANCE_DEBUG if any performance debug feature is requested
#if defined(DEBUG_THREAD_PERFORMANCE) || defined(DEBUG_MEMORY_USAGE) || defined(DEBUG_CPU_AFFINITY)
    #ifndef ENABLE_PERFORMANCE_DEBUG
        #define ENABLE_PERFORMANCE_DEBUG  // Auto-activated for performance debug features
        #pragma message "ℹ️  ENABLE_PERFORMANCE_DEBUG auto-activated for performance debug features"
    #endif
#endif

/**************************************************************************************
 * CONFIGURATION AUTOMATIQUE ET HELPERS
 * 
 * Définitions automatiques basées sur les flags activés
 **************************************************************************************/

// Auto-enable DEBUG_IMAGE_SAVE_TO_FILES if any image debug feature is enabled
#if defined(DEBUG_OSCILLATOR_VOLUME_SCAN) || defined(DEBUG_TEMPORAL_SCAN)
    #ifndef DEBUG_IMAGE_SAVE_TO_FILES
        #define DEBUG_IMAGE_SAVE_TO_FILES  // Auto-enable file saving for scan features
    #endif
#endif

// Helper macro pour les messages de debug conditionnels
#ifdef ENABLE_IMAGE_DEBUG
    #define IMAGE_DEBUG_PRINT(fmt, ...) printf("🔧 IMAGE_DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
    #define IMAGE_DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

#ifdef ENABLE_AUDIO_DEBUG
    #define AUDIO_DEBUG_PRINT(fmt, ...) printf("🔊 AUDIO_DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
    #define AUDIO_DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

#endif // __CONFIG_DEBUG_H__
