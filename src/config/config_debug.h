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
 * Note: Oscillator debug is now controlled by runtime arguments:
 * --debug-additive-osc-image=SAMPLES (e.g., --debug-additive-osc-image=48000)
 **************************************************************************************/

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
 * Note: Image debug is now controlled by runtime arguments:
 * --debug-image=LINES              (raw scanner capture)
 * --debug-additive-osc-image=SAMPLES (oscillator volume capture)
 **************************************************************************************/

// Configuration commune pour tous les types de debug d'images
#define DEBUG_IMAGE_OUTPUT_DIR "./debug_images/"              // Debug image output directory (relative to executable)

/**************************************************************************************
 * DEBUG PERFORMANCE & PROFILING
 * 
 * Dépendance: ENABLE_PERFORMANCE_DEBUG (recommandé)
 **************************************************************************************/

// #define DEBUG_THREAD_PERFORMANCE     // Enable thread performance monitoring
// #define DEBUG_MEMORY_USAGE           // Enable memory usage tracking
// #define DEBUG_CPU_AFFINITY           // Enable CPU affinity debug logging

/**************************************************************************************
 * GUIDE D'UTILISATION RAPIDE
 * 
 * Debug d'images maintenant contrôlé par arguments runtime :
 * 
 * Raw scanner capture:
 *   ./Sp3ctra --debug-image=1000              # Capture 1000 lignes
 *   ./Sp3ctra --debug-image                   # Capture 1000 lignes (défaut)
 * 
 * Oscillateur additif:
 *   ./Sp3ctra --debug-additive-osc-image=48000  # Capture 48000 samples (1 sec)
 * 
 * Combiné:
 *   ./Sp3ctra --debug-image=2000 --debug-additive-osc-image=24000
 * 
 * Images sauvegardées dans: ./debug_images/
 **************************************************************************************/

/**************************************************************************************
 * AUTO-ACTIVATION INTELLIGENTE DES MASTER SWITCHES
 * 
 * Les master switches sont automatiquement activés quand des fonctionnalités 
 * correspondantes sont demandées. Plus besoin de se souvenir des dépendances !
 **************************************************************************************/

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
