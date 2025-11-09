/* audio_rtaudio.cpp */

#include "audio_rtaudio.h"
#include "audio_c_api.h"
#include "midi_controller.h" // For gMidiController
#include "synth_polyphonic.h" // For polyphonic_audio_buffers and related variables
#include "../../synthesis/photowave/synth_photowave.h" // For photowave_audio_buffers
#include "../../config/config_debug.h"    // For debug configuration macros
#include "../../config/config_audio.h"    // For HDMI format configuration
#include "../../config/config_loader.h"   // For runtime configuration access
#include "../../utils/image_debug.h"      // For continuous volume capture
#include "../../synthesis/additive/wave_generation.h"  // For waves[] access
#include "../../utils/logger.h"           // For structured logging
#include <algorithm>         // For std::transform
#include <cstring>
#include <iostream>
#include <rtaudio/RtAudio.h> // Explicitly include RtAudio.h
#include <stdexcept>         // For std::exception
#include <set>               // For std::set
#include <cstdlib>           // For malloc/calloc/free

// Global format selection variables for dynamic audio format
static RtAudioFormat g_selected_audio_format = RTAUDIO_FLOAT32;
static const char* g_selected_audio_format_name __attribute__((unused)) = "FLOAT32";

// Global variables for compatibility with legacy code
AudioDataBuffers buffers_L[2];
AudioDataBuffers buffers_R[2];
volatile int current_buffer_index = 0;
pthread_mutex_t buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

AudioSystem *gAudioSystem = nullptr;

// Global variables for synth mix levels (accessed from audio callback)
// Volatile ensures thread visibility on modern architectures
static volatile float g_synth_additive_mix_level = 1.0f;
static volatile float g_synth_polyphonic_mix_level = 0.5f;
static volatile float g_synth_photowave_mix_level = 0.0f;  // Photowave disabled by default

// Global variables to store requested audio device before AudioSystem is created
extern "C" {
int g_requested_audio_device_id = -1;
char* g_requested_audio_device_name = NULL;
}

// Minimal audio callback control
static bool use_minimal_callback = false;
static float minimal_test_volume = 0.1f;

// Callbacks
int AudioSystem::rtCallback(void *outputBuffer, void *inputBuffer,
                            unsigned int nFrames, double streamTime,
                            RtAudioStreamStatus status, void *userData) {
  (void)inputBuffer; // Mark inputBuffer as unused
  (void)streamTime;  // Mark streamTime as unused
  (void)status;      // Mark status as unused
  auto *audioSystem = static_cast<AudioSystem *>(userData);
  return audioSystem->handleCallback(static_cast<float *>(outputBuffer),
                                     nFrames);
}

int AudioSystem::handleCallback(float *outputBuffer, unsigned int nFrames) {
  // MINIMAL CALLBACK MODE - for debugging audio dropouts
  if (use_minimal_callback) {
    float *outLeft = outputBuffer;
    float *outRight = outputBuffer + nFrames;

    // Simple test tone generation - no complex processing
    static float phase = 0.0f;
    static const float frequency = 440.0f; // A4 note
    static const float sample_rate = g_sp3ctra_config.sampling_frequency;
    static const float phase_increment = 2.0f * M_PI * frequency / sample_rate;

    for (unsigned int i = 0; i < nFrames; i++) {
      float sample = sinf(phase) * minimal_test_volume;
      outLeft[i] = sample;
      outRight[i] = sample;

      phase += phase_increment;
      if (phase >= 2.0f * M_PI) {
        phase -= 2.0f * M_PI;
      }
    }

    return 0; // Success - no dropouts with minimal processing
  }

  // ULTRA-OPTIMIZED CALLBACK - for 96kHz performance
  // Configuration stéréo non-entrelacée (comme RtAudio par défaut)
  float *outLeft = outputBuffer;
  float *outRight = outputBuffer + nFrames;

  // Variables statiques pour maintenir l'état entre les appels
  static unsigned int readOffset = 0;
  static int localReadIndex = 0;
  static unsigned int polyphonic_readOffset = 0;
  static int polyphonic_localReadIndex = 0;
  static unsigned int photowave_readOffset = 0;
  static int photowave_localReadIndex = 0;

  // Cache volume level to avoid repeated access
  static float cached_volume = 1.0f;
  static int cache_counter = 0;

  // Update cache automatically for smooth volume transitions
  if (++cache_counter >= 8) {  // Static value instead of dynamic macro
    cache_counter = 0;
    cached_volume = this->masterVolume;
  }
  
  // Read mix levels from global variables (controlled via MIDI)
  float cached_level_additive = g_synth_additive_mix_level;
  float cached_level_polyphonic = g_synth_polyphonic_mix_level;
  float cached_level_photowave = g_synth_photowave_mix_level;

  unsigned int framesToRender = nFrames;

  // Process frames using multiple buffers if needed
  while (framesToRender > 0) {
    // How many frames available in current buffer?
    unsigned int framesAvailable = g_sp3ctra_config.audio_buffer_size - readOffset;
    unsigned int chunk =
        (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

    // Get source pointers directly - avoid memcpy when possible
    float *source_additive_left = nullptr;
    float *source_additive_right = nullptr;
    float *source_fft = nullptr;
    float *source_photowave = nullptr;

    // Always use stereo mode with both left and right channels
    if (buffers_L[localReadIndex].ready == 1) {
      source_additive_left = &buffers_L[localReadIndex].data[readOffset];
    }
    if (buffers_R[localReadIndex].ready == 1) {
      source_additive_right = &buffers_R[localReadIndex].data[readOffset];
    }

    if (polyphonic_audio_buffers[polyphonic_localReadIndex].ready == 1) {
      unsigned int polyphonic_framesAvailable =
          g_sp3ctra_config.audio_buffer_size - polyphonic_readOffset;
      if (polyphonic_framesAvailable >= chunk) {
        source_fft = &polyphonic_audio_buffers[polyphonic_localReadIndex]
                          .data[polyphonic_readOffset];
      }
    }

    if (photowave_audio_buffers[photowave_localReadIndex].ready == 1) {
      unsigned int photowave_framesAvailable =
          g_sp3ctra_config.audio_buffer_size - photowave_readOffset;
      if (photowave_framesAvailable >= chunk) {
        source_photowave = &photowave_audio_buffers[photowave_localReadIndex]
                               .data[photowave_readOffset];
      }
    }

  // OPTIMIZED MIXING - Direct to output with threaded reverb
  for (unsigned int i = 0; i < chunk; i++) {
      float dry_sample_left = 0.0f;
      float dry_sample_right = 0.0f;

      // Add Additive synthesis contribution - separate left and right channels
      if (source_additive_left) {
        dry_sample_left += source_additive_left[i] * cached_level_additive;
      }
      if (source_additive_right) {
        dry_sample_right += source_additive_right[i] * cached_level_additive;
      }
      
#ifdef DEBUG_AUDIO_SIGNAL
      // DEBUG: Log signal levels every 4800 samples (~100ms at 48kHz)
      static int debug_counter = 0;
      if (++debug_counter >= 4800) {
        debug_counter = 0;
        printf("SIGNAL DEBUG: dry_L=%.6f, dry_R=%.6f, additive_level=%.3f, reverb_send=%.3f\n",
               dry_sample_left, dry_sample_right, cached_level_additive, cached_reverb_send_additive);
        if (source_additive_left) {
          printf("SOURCE DEBUG: additive_L=%.6f, additive_R=%.6f\n",
                 source_additive_left[i], source_additive_right ? source_additive_right[i] : 0.0f);
        }
      }
#endif

      // Add polyphonic contribution (same for both channels)
      if (source_fft) {
        dry_sample_left += source_fft[i] * cached_level_polyphonic;
        dry_sample_right += source_fft[i] * cached_level_polyphonic;
      }

      // Add Photowave contribution (same for both channels)
      // CPU OPTIMIZATION: Skip photowave processing if mix level is essentially zero
      if (source_photowave && cached_level_photowave > 0.01f) {
        dry_sample_left += source_photowave[i] * cached_level_photowave;
        dry_sample_right += source_photowave[i] * cached_level_photowave;
      }

      // Direct reverb processing in callback - ULTRA OPTIMIZED
      float reverb_left = 0.0f, reverb_right = 0.0f;

#if ENABLE_REVERB
#ifdef DEBUG_AUDIO_REVERB
      // DEBUG: Log reverb condition check
      static int reverb_debug_counter = 0;
      if (++reverb_debug_counter >= 4800) {
        reverb_debug_counter = 0;
        printf("REVERB CONDITION: ENABLE_REVERB=%d, reverbEnabled=%d, send_additive=%.3f, send_poly=%.3f\n",
               ENABLE_REVERB, reverbEnabled ? 1 : 0, cached_reverb_send_additive, cached_reverb_send_polyphonic);
      }
#endif
      
      if (reverbEnabled && (cached_reverb_send_additive > 0.01f ||
                            cached_reverb_send_polyphonic > 0.01f)) {
        float reverb_input_left = 0.0f;
        float reverb_input_right = 0.0f;

        if (source_additive_left && cached_reverb_send_additive > 0.01f) {
          reverb_input_left += source_additive_left[i] * cached_level_additive *
                               cached_reverb_send_additive;
        }
        if (source_additive_right && cached_reverb_send_additive > 0.01f) {
          reverb_input_right += source_additive_right[i] *
                                cached_level_additive *
                                cached_reverb_send_additive;
        }
        if (source_fft && cached_reverb_send_polyphonic > 0.01f) {
          float polyphonic_reverb = source_fft[i] * cached_level_polyphonic *
                                    cached_reverb_send_polyphonic;
          reverb_input_left += polyphonic_reverb;
          reverb_input_right += polyphonic_reverb;
        }

        // Single-sample reverb processing (optimized) - use average of
        // left/right for input
        float reverb_input_mono =
            (reverb_input_left + reverb_input_right) * 0.5f;
        processReverbOptimized(reverb_input_mono, reverb_input_mono,
                               reverb_left, reverb_right);
      }
#endif

      // Mix dry + reverb and apply volume
      float final_left = (dry_sample_left + reverb_left) * cached_volume;
      float final_right = (dry_sample_right + reverb_right) * cached_volume;

      // Limiting
      final_left = (final_left > 1.0f)    ? 1.0f
                   : (final_left < -1.0f) ? -1.0f
                                          : final_left;
      final_right = (final_right > 1.0f)    ? 1.0f
                    : (final_right < -1.0f) ? -1.0f
                                            : final_right;

      // Output (with optional L/R swap)
#if SWAP_LEFT_RIGHT_CHANNELS
      outLeft[i] = final_right;   // Swap: left gets right
      outRight[i] = final_left;   // Swap: right gets left
#else
      outLeft[i] = final_left;    // Normal: left gets left
      outRight[i] = final_right;  // Normal: right gets right
#endif
      
    }

    // Advance pointers
    outLeft += chunk;
    outRight += chunk;
    readOffset += chunk;
    polyphonic_readOffset += chunk;
    photowave_readOffset += chunk;
    framesToRender -= chunk;

    // Handle buffer transitions - Additive synthesis (RT-SAFE VERSION)
    if (readOffset >= (unsigned int)g_sp3ctra_config.audio_buffer_size) {
      // RT-SAFE: Use atomic store instead of mutex for buffer ready state
      if (buffers_L[localReadIndex].ready == 1) {
        __atomic_store_n(&buffers_L[localReadIndex].ready, 0, __ATOMIC_RELEASE);
        // Note: pthread_cond_signal removed - producer thread will poll ready state
      }
      if (buffers_R[localReadIndex].ready == 1) {
        __atomic_store_n(&buffers_R[localReadIndex].ready, 0, __ATOMIC_RELEASE);
        // Note: pthread_cond_signal removed - producer thread will poll ready state  
      }
      localReadIndex = (localReadIndex == 0) ? 1 : 0;
      readOffset = 0;
    }

    // Handle buffer transitions - Polyphonic (RT-SAFE VERSION)
    if (polyphonic_readOffset >= (unsigned int)g_sp3ctra_config.audio_buffer_size) {
      if (polyphonic_audio_buffers[polyphonic_localReadIndex].ready == 1) {
        // RT-SAFE: Use atomic store instead of mutex for buffer ready state
        __atomic_store_n(&polyphonic_audio_buffers[polyphonic_localReadIndex].ready, 0, __ATOMIC_RELEASE);
        // Note: pthread_cond_signal removed - producer thread will poll ready state
      }
      polyphonic_localReadIndex = (polyphonic_localReadIndex == 0) ? 1 : 0;
      polyphonic_readOffset = 0;
    }

    // Handle buffer transitions - Photowave (RT-SAFE VERSION)
    if (photowave_readOffset >= (unsigned int)g_sp3ctra_config.audio_buffer_size) {
      if (photowave_audio_buffers[photowave_localReadIndex].ready == 1) {
        // RT-SAFE: Use atomic store instead of mutex for buffer ready state
        __atomic_store_n(&photowave_audio_buffers[photowave_localReadIndex].ready, 0, __ATOMIC_RELEASE);
        // Note: pthread_cond_signal removed - producer thread will poll ready state
      }
      photowave_localReadIndex = (photowave_localReadIndex == 0) ? 1 : 0;
      photowave_readOffset = 0;
    }
  }

  return 0;
}

// Constructeur
AudioSystem::AudioSystem(unsigned int sampleRate, unsigned int bufferSize,
                         unsigned int channels)
    : audio(nullptr), isRunning(false), // Moved isRunning before members that
                                        // might use it implicitly or explicitly
      sampleRate(g_sp3ctra_config.sampling_frequency), bufferSize(g_sp3ctra_config.audio_buffer_size), channels(channels),
      requestedDeviceId(g_requested_audio_device_id), // Use global variable if
                                                      // set, otherwise -1
      masterVolume(1.0f), reverbBuffer(nullptr), reverbMix(DEFAULT_REVERB_MIX),
      reverbRoomSize(DEFAULT_REVERB_ROOM_SIZE),
      reverbDamping(DEFAULT_REVERB_DAMPING), reverbWidth(DEFAULT_REVERB_WIDTH),
      reverbEnabled(ENABLE_REVERB), reverbThreadRunning(false) {

  log_info("AUDIO", "Reverb enabled by default with Zita-Rev1 algorithm");

  processBuffer.resize(bufferSize * channels);

  // Initialisation du buffer de réverbération (pour compatibilité)
  reverbBuffer = new float[REVERB_BUFFER_SIZE];
  for (int i = 0; i < REVERB_BUFFER_SIZE; i++) {
    reverbBuffer[i] = 0.0f;
  }

  // Configuration des délais pour la réverbération (pour compatibilité)
  reverbDelays[0] = 1116;
  reverbDelays[1] = 1356;
  reverbDelays[2] = 1422;
  reverbDelays[3] = 1617;
  reverbDelays[4] = 1188;
  reverbDelays[5] = 1277;
  reverbDelays[6] = 1491;
  reverbDelays[7] = 1557;

  // Configuration de ZitaRev1 avec des valeurs pour une réverbération longue et
  // douce
  zitaRev.init(sampleRate);
  zitaRev.set_roomsize(
      0.95f); // Très grande taille de pièce pour une réverb longue
  zitaRev.set_damping(0.4f); // Amortissement des hautes fréquences réduit pour
                             // plus de brillance
  zitaRev.set_width(1.0f);   // Largeur stéréo maximale
  zitaRev.set_delay(
      0.08f);            // Pre-delay plus important pour clarté et séparation
  zitaRev.set_mix(0.7f); // 70% wet pour équilibre entre clarté et présence
}

// Destructeur
AudioSystem::~AudioSystem() {
  stop();

  // Libération du buffer de réverbération
  if (reverbBuffer) {
    delete[] reverbBuffer;
    reverbBuffer = nullptr;
  }

  if (audio) {
    if (audio->isStreamOpen()) {
      audio->closeStream();
    }
    delete audio;
  }
}

// Fonction de traitement de la réverbération
void AudioSystem::processReverb(float inputL, float inputR, float &outputL,
                                float &outputR) {
  // Si réverbération désactivée, sortie = entrée
  if (!reverbEnabled) {
    outputL = inputL;
    outputR = inputR;
    return;
  }

  // Mise à jour des paramètres ZitaRev1 en fonction des contrôles MIDI
  zitaRev.set_roomsize(reverbRoomSize);
  zitaRev.set_damping(reverbDamping);
  zitaRev.set_width(reverbWidth);
  // Le mix est géré séparément dans notre code

  // Buffers temporaires pour traitement ZitaRev1
  float inBufferL[1] = {inputL};
  float inBufferR[1] = {inputR};
  float outBufferL[1] = {0.0f};
  float outBufferR[1] = {0.0f};

  // Traiter via ZitaRev1 (algorithme de réverbération de haute qualité)
  zitaRev.process(inBufferL, inBufferR, outBufferL, outBufferR, 1);

  // Mélanger le signal sec et le signal traité (wet)
  // Utiliser une courbe linéaire pour une réverbération plus douce
  float wetGain =
      reverbMix; // Relation directe entre le paramètre et le gain wet

  // Balance simple entre signal sec et humide
  float dryGain = 1.0f - reverbMix; // Relation inverse pour un total de 100%

  // Mélanger les signaux
  outputL = inputL * dryGain + outBufferL[0] * wetGain;
  outputR = inputR * dryGain + outBufferR[0] * wetGain;
}

// ULTRA-OPTIMIZED reverb function for real-time callback
void AudioSystem::processReverbOptimized(float inputL, float inputR,
                                         float &outputL, float &outputR) {
#ifdef DEBUG_AUDIO_REVERB
  // DEBUG: Log reverb function calls
  static int reverb_call_counter = 0;
  if (++reverb_call_counter >= 4800) {
    reverb_call_counter = 0;
    printf("REVERB CALLED: inputL=%.6f, inputR=%.6f, reverbEnabled=%d, reverbMix=%.3f\n",
           inputL, inputR, reverbEnabled ? 1 : 0, reverbMix);
  }
#endif
  
  // CPU OPTIMIZATION: Skip all reverb processing if mix is zero or reverb
  // disabled
  if (!reverbEnabled || reverbMix <= 0.0f) {
    outputL = inputL;
    outputR = inputR;
#ifdef DEBUG_AUDIO_REVERB
    printf("REVERB SKIPPED: reverbEnabled=%d, reverbMix=%.3f\n", reverbEnabled ? 1 : 0, reverbMix);
#endif
    return;
  }

  // Cache statique pour éviter les calculs répétitifs
  static float cached_wet_gain = reverbMix; // Initialiser directement avec la valeur de reverb
  static float cached_dry_gain = 1.0f - reverbMix; // Compense pour avoir 100% du signal
  static int param_update_counter = 0;
  static bool reverb_initialized = false;

  // Initialiser la reverb une seule fois au démarrage
  if (!reverb_initialized) {
    // Effacer explicitement les buffers internes pour éviter les craquements
    zitaRev.clear();
    // Initialiser les paramètres de base
    zitaRev.set_roomsize(reverbRoomSize);
    zitaRev.set_damping(reverbDamping);
    zitaRev.set_width(reverbWidth);
    // Initialiser les gains avec les bonnes valeurs
    cached_wet_gain = reverbMix;
    cached_dry_gain = 1.0f - reverbMix;
    reverb_initialized = true;
  }
  
  // Mise à jour régulière des paramètres
  if (++param_update_counter >= 256) {
    param_update_counter = 0;
    cached_wet_gain = reverbMix;
    cached_dry_gain = 1.0f - reverbMix;

    // Mettre à jour les paramètres ZitaRev1 moins fréquemment
    zitaRev.set_roomsize(reverbRoomSize);
    zitaRev.set_damping(reverbDamping);
    zitaRev.set_width(reverbWidth);
  }

  // Traitement ZitaRev1 optimisé - un seul échantillon
  float inBufferL[1] = {inputL};
  float inBufferR[1] = {inputR};
  float outBufferL[1] = {0.0f};
  float outBufferR[1] = {0.0f};

  // Appel direct à ZitaRev1 (le coût principal)
  zitaRev.process(inBufferL, inBufferR, outBufferL, outBufferR, 1);

  // Mix optimisé avec gains en cache
  outputL = inputL * cached_dry_gain + outBufferL[0] * cached_wet_gain;
  outputR = inputR * cached_dry_gain + outBufferR[0] * cached_wet_gain;
  
#ifdef DEBUG_AUDIO_REVERB
  // DEBUG: Log reverb output values
  static int reverb_output_counter = 0;
  if (++reverb_output_counter >= 4800) {
    reverb_output_counter = 0;
    printf("REVERB OUTPUT: zita_outL=%.6f, zita_outR=%.6f, wet_gain=%.3f, dry_gain=%.3f\n",
           outBufferL[0], outBufferR[0], cached_wet_gain, cached_dry_gain);
    printf("FINAL REVERB: outputL=%.6f, outputR=%.6f (dry=%.6f + wet=%.6f)\n",
           outputL, outputR, inputL * cached_dry_gain, outBufferL[0] * cached_wet_gain);
  }
#endif
}

// === MULTI-THREADED REVERB IMPLEMENTATION ===

// Fonction pour écrire dans le buffer d'entrée de la réverbération
// (thread-safe)
bool AudioSystem::writeToReverbInput(float sample) {
  int currentWrite = reverbInputBuffer.write_pos.load();
  int nextWrite = (currentWrite + 1) % REVERB_THREAD_BUFFER_SIZE;

  // Vérifier si le buffer n'est pas plein
  if (nextWrite == reverbInputBuffer.read_pos.load()) {
    return false; // Buffer plein
  }

  reverbInputBuffer.data[currentWrite] = sample;
  reverbInputBuffer.write_pos.store(nextWrite);
  reverbInputBuffer.available_samples.fetch_add(1);

  return true;
}

// Fonction pour lire depuis le buffer d'entrée de la réverbération
// (thread-safe)
bool AudioSystem::readFromReverbInput(float &sample) {
  if (reverbInputBuffer.available_samples.load() == 0) {
    return false; // Buffer vide
  }

  int currentRead = reverbInputBuffer.read_pos.load();
  sample = reverbInputBuffer.data[currentRead];

  int nextRead = (currentRead + 1) % REVERB_THREAD_BUFFER_SIZE;
  reverbInputBuffer.read_pos.store(nextRead);
  reverbInputBuffer.available_samples.fetch_sub(1);

  return true;
}

// Fonction pour écrire dans le buffer de sortie de la réverbération
// (thread-safe)
bool AudioSystem::writeToReverbOutput(float sampleL, float sampleR) {
  int currentWrite = reverbOutputBuffer.write_pos.load();
  int nextWrite = (currentWrite + 1) % REVERB_THREAD_BUFFER_SIZE;

  // Vérifier si le buffer n'est pas plein
  if (nextWrite == reverbOutputBuffer.read_pos.load()) {
    return false; // Buffer plein
  }

  reverbOutputBuffer.left[currentWrite] = sampleL;
  reverbOutputBuffer.right[currentWrite] = sampleR;
  reverbOutputBuffer.write_pos.store(nextWrite);
  reverbOutputBuffer.available_samples.fetch_add(1);

  return true;
}

// Fonction pour lire depuis le buffer de sortie de la réverbération
// (thread-safe)
bool AudioSystem::readFromReverbOutput(float &sampleL, float &sampleR) {
  if (reverbOutputBuffer.available_samples.load() == 0) {
    sampleL = sampleR = 0.0f; // Silence si pas de données
    return false;
  }

  int currentRead = reverbOutputBuffer.read_pos.load();
  sampleL = reverbOutputBuffer.left[currentRead];
  sampleR = reverbOutputBuffer.right[currentRead];

  int nextRead = (currentRead + 1) % REVERB_THREAD_BUFFER_SIZE;
  reverbOutputBuffer.read_pos.store(nextRead);
  reverbOutputBuffer.available_samples.fetch_sub(1);

  return true;
}

// Fonction principale du thread de réverbération
void AudioSystem::reverbThreadFunction() {
  std::cout
      << "\033[1;33m[REVERB THREAD] Thread de réverbération démarré\033[0m"
      << std::endl;

  const int processingBlockSize = 64; // Traiter par blocs pour l'efficacité
  float inputBuffer[processingBlockSize];
  float outputBufferL[processingBlockSize];
  float outputBufferR[processingBlockSize];

  while (reverbThreadRunning.load()) {
    int samplesProcessed = 0;

    // Lire un bloc de samples depuis le buffer d'entrée
    for (int i = 0; i < processingBlockSize; i++) {
      float sample;
      if (readFromReverbInput(sample)) {
        inputBuffer[i] = sample;
        samplesProcessed++;
      } else {
        inputBuffer[i] = 0.0f; // Silence si pas de données
      }
    }

    if (samplesProcessed > 0 && reverbEnabled) {
      // Traiter la réverbération par blocs pour l'efficacité
      for (int i = 0; i < processingBlockSize; i++) {
        // Mise à jour des paramètres ZitaRev1 (de temps en temps)
        if (i == 0) { // Seulement au début du bloc
          zitaRev.set_roomsize(reverbRoomSize);
          zitaRev.set_damping(reverbDamping);
          zitaRev.set_width(reverbWidth);
        }

        // Traitement mono vers stéréo
        float inBufferL[1] = {inputBuffer[i]};
        float inBufferR[1] = {inputBuffer[i]};
        float outBufferL_single[1] = {0.0f};
        float outBufferR_single[1] = {0.0f};

        // Traiter via ZitaRev1
        zitaRev.process(inBufferL, inBufferR, outBufferL_single,
                        outBufferR_single, 1);

        // Appliquer le mix dry/wet
        float wetGain = reverbMix;
        float dryGain = 1.0f - reverbMix;

        outputBufferL[i] =
            inputBuffer[i] * dryGain + outBufferL_single[0] * wetGain;
        outputBufferR[i] =
            inputBuffer[i] * dryGain + outBufferR_single[0] * wetGain;
      }

      // Écrire les résultats dans le buffer de sortie
      for (int i = 0; i < processingBlockSize; i++) {
        // Tenter d'écrire, ignorer si le buffer de sortie est plein
        writeToReverbOutput(outputBufferL[i], outputBufferR[i]);
      }
    } else {
      // Pas de samples à traiter ou reverb désactivée, attendre un peu
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  std::cout << "\033[1;33m[REVERB THREAD] Thread de réverbération arrêté\033[0m"
            << std::endl;
}

// Initialisation
bool AudioSystem::initialize() {
  // Forcer l'utilisation de l'API ALSA sur Linux
#ifdef __linux__
  log_info("AUDIO", "Attempting to initialize RtAudio with ALSA API");
  audio = new RtAudio(RtAudio::LINUX_ALSA);
#else
  audio = new RtAudio();
#endif

  // Vérifier si RtAudio a été correctement créé
  if (!audio) {
    log_error("AUDIO", "Unable to create RtAudio instance");
    return false;
  }

  // Get and print available devices
  unsigned int deviceCount = 0;
  try {
    deviceCount = audio->getDeviceCount();
  } catch (const std::exception &error) {
    log_error("AUDIO", "Error getting device count: %s", error.what());
    delete audio;
    audio = nullptr;
    return false;
  }

  // Simplified device selection - use default device unless specific device requested
  unsigned int preferredDeviceId = audio->getDefaultOutputDevice();
  bool foundRequestedDevice = false;
  
  // Handle specific device requests (ID or name)
  if (requestedDeviceId >= 0 || g_requested_audio_device_name != NULL) {
    // Only enumerate devices when specifically requested
    log_info("AUDIO", "Searching for specific audio device");
    
    // Perform full enumeration only when needed
    std::vector<std::pair<unsigned int, std::string>> accessibleDevices;
    for (unsigned int i = 0; i < deviceCount; i++) {
      try {
        RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
        if (info.outputChannels > 0 && !info.name.empty()) {
          accessibleDevices.push_back(std::make_pair(i, info.name));
          
          // Check for requested device ID
          if (requestedDeviceId >= 0 && i == (unsigned int)requestedDeviceId) {
            preferredDeviceId = i;
            foundRequestedDevice = true;
            log_info("AUDIO", "Found requested device ID %u: %s", i, info.name.c_str());
          }
          
          // Check for requested device name
          if (g_requested_audio_device_name != NULL) {
            std::string deviceName = info.name;
            std::string searchName = g_requested_audio_device_name;
            std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), ::tolower);
            std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);
            
            if (deviceName.find(searchName) != std::string::npos) {
              preferredDeviceId = i;
              foundRequestedDevice = true;
              log_info("AUDIO", "Found requested device '%s': %s", 
                      g_requested_audio_device_name, info.name.c_str());
              break;
            }
          }
        }
      } catch (const std::exception &error) {
        // Silently skip problematic devices during specific search
        continue;
      }
    }
    
    // Validate that requested device was found
    if ((requestedDeviceId >= 0 || g_requested_audio_device_name != NULL) && !foundRequestedDevice) {
      log_error("AUDIO", "Requested audio device not found");
      if (requestedDeviceId >= 0) {
        log_error("AUDIO", "Device ID %d is not available", requestedDeviceId);
      }
      if (g_requested_audio_device_name != NULL) {
        log_error("AUDIO", "Device '%s' is not available", g_requested_audio_device_name);
      }
      log_error("AUDIO", "Use --list-audio-devices to see available devices");
      return false;
    }
  } else {
    // Default behavior: use default device without enumeration
    try {
      RtAudio::DeviceInfo defaultInfo = audio->getDeviceInfo(preferredDeviceId);
      log_info("AUDIO", "Using default audio device: %s", defaultInfo.name.c_str());
    } catch (const std::exception &error) {
      log_error("AUDIO", "Cannot access default audio device: %s", error.what());
      return false;
    }
  }

  // Device selection is now complete - preferredDeviceId contains the selected device
  // No additional enumeration or validation needed since it was handled above

  // Paramètres du stream
  RtAudio::StreamParameters params;
  params.deviceId =
      preferredDeviceId; // Utiliser le preferredDeviceId trouvé ou le défaut
  params.nChannels = channels;
  params.firstChannel = 0;

  // Options pour optimiser la stabilité sur Raspberry Pi Module 5
  RtAudio::StreamOptions options;
  options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME;

  // Buffer configuration optimized for Pi Module 5
#ifdef __ARM_ARCH
  options.numberOfBuffers = 12; // Increased for ARM (Pi Module 5) stability
  options.streamName = "Sp3ctra_Pi5_Optimized";
#else
  options.numberOfBuffers = 8; // Standard for x86/x64
  options.streamName = "Sp3ctra_Standard";
#endif

  // High priority for audio thread on Pi Module 5 - reduced for better compatibility
  options.priority = 70; // Real-time priority optimized for Pi permissions

  // Check device capabilities before opening
  try {
    RtAudio::DeviceInfo deviceInfo = audio->getDeviceInfo(preferredDeviceId);
    log_info("AUDIO", "Audio device: %s (ID: %u)", deviceInfo.name.c_str(), preferredDeviceId);

    // Check if configured frequency is supported
    bool supportsConfigRate = false;
    unsigned int configRate = g_sp3ctra_config.sampling_frequency;
    for (unsigned int rate : deviceInfo.sampleRates) {
      if (rate == configRate) {
        supportsConfigRate = true;
        break;
      }
    }

    if (!supportsConfigRate) {
      log_error("AUDIO", "Device does not support %uHz", configRate);
      return false;
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "Device query failed: %s", e.what());
    return false;
  }

  // Use SAMPLING_FREQUENCY from config.h instead of hard-coding 96kHz
  unsigned int configSampleRate = g_sp3ctra_config.sampling_frequency;
  if (sampleRate != configSampleRate) {
    log_info("AUDIO", "CONFIGURATION: Change from %uHz to %uHz (defined in config.h)", 
            sampleRate, configSampleRate);
    sampleRate = configSampleRate;
  }

  // Open audio stream with low latency options
  try {
    audio->openStream(&params, nullptr, g_selected_audio_format, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    // Check actually negotiated parameters
    if (audio->isStreamOpen()) {
      unsigned int actualSampleRate = audio->getStreamSampleRate();
      
      // Check for critical mismatches only
      if (bufferSize != (unsigned int)g_sp3ctra_config.audio_buffer_size) {
        log_error("AUDIO", "Buffer size mismatch - Config: %d frames, Hardware: %u frames", 
                 g_sp3ctra_config.audio_buffer_size, bufferSize);
        log_error("AUDIO", "Change audio_buffer_size to %u in sp3ctra.ini", bufferSize);
        return false;
      }

      if (actualSampleRate != configSampleRate) {
        log_error("AUDIO", "Sample rate mismatch - Requested: %uHz, Got: %uHz", 
                 configSampleRate, actualSampleRate);
        return false;
      }

      log_info("AUDIO", "Stream opened successfully: %uHz, %u frames", 
              actualSampleRate, bufferSize);
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "RtAudio error: %s", e.what());
    delete audio;    // Clean up RtAudio object on failure
    audio = nullptr; // Set pointer to nullptr
    return false;
  }

  return true;
}

// Démarrage du flux audio
bool AudioSystem::start() {
  if (!audio || !audio->isStreamOpen())
    return false;

  try {
    audio->startStream();
  } catch (std::exception &e) {
    log_error("AUDIO", "RtAudio start error: %s", e.what());
    return false;
  }

  isRunning = true;
  return true;
}

// Arrêt du flux audio
void AudioSystem::stop() {
  if (audio && audio->isStreamRunning()) {
    try {
      audio->stopStream();
    } catch (std::exception &e) {
      log_error("AUDIO", "RtAudio stop error: %s", e.what());
    }
    isRunning = false;
  }
}

// Vérifier si le système est actif
bool AudioSystem::isActive() const { return audio && audio->isStreamRunning(); }

// Mise à jour des données audio
bool AudioSystem::setAudioData(const float *data, size_t size) {
  if (!data || size == 0)
    return false;

  // Protection d'accès au buffer
  std::lock_guard<std::mutex> lock(bufferMutex);

  // Copie des données dans notre buffer
  size_t copySize = std::min(size, processBuffer.size());
  std::memcpy(processBuffer.data(), data, copySize * sizeof(float));

  return true;
}

// Récupérer la liste des périphériques disponibles
std::vector<std::string> AudioSystem::getAvailableDevices() {
  std::vector<std::string> devices;

  if (!audio)
    return devices;

  unsigned int deviceCount = audio->getDeviceCount();
  for (unsigned int i = 0; i < deviceCount; i++) {
    RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
    if (info.outputChannels > 0) {
      devices.push_back(info.name);
    }
  }

  return devices;
}

// Modifier le périphérique de sortie
bool AudioSystem::setDevice(unsigned int deviceId) {
  if (!audio)
    return false;

  // Il faut arrêter et redémarrer le stream pour changer de périphérique
  bool wasRunning = audio->isStreamRunning();
  if (wasRunning) {
    audio->stopStream();
  }

  if (audio->isStreamOpen()) {
    audio->closeStream();
  }

  // Paramètres du stream
  RtAudio::StreamParameters params;
  params.deviceId = deviceId;
  params.nChannels = channels;
  params.firstChannel = 0;

  // Options pour optimiser la stabilité sur Raspberry Pi
  RtAudio::StreamOptions options;
  options.flags =
      RTAUDIO_NONINTERLEAVED; // Suppression de RTAUDIO_MINIMIZE_LATENCY
  options.numberOfBuffers =
      8; // Increased from 4 to 8 for better stability on Pi

  // Ouvrir le flux audio avec les options de faible latence
  try {
    audio->openStream(&params, nullptr, RTAUDIO_FORMAT_TYPE, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    if (wasRunning) {
      audio->startStream();
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "Device change error: %s", e.what());
    return false;
  }

  return true;
}

// Récupérer le périphérique actuel
unsigned int AudioSystem::getCurrentDevice() const {
  if (!audio || !audio->isStreamOpen())
    return 0;

  // Pas de méthode directe pour récupérer le périphérique courant
  // Retourner le périphérique par défaut comme solution de contournement
  return audio->getDefaultOutputDevice();
}

// Modifier la taille du buffer (impact sur la latence)
bool AudioSystem::setBufferSize(unsigned int size) {
  // Pour changer la taille du buffer, il faut recréer le stream
  if (size == bufferSize)
    return true;

  bufferSize = size;

  // Redimensionner le buffer de traitement
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    processBuffer.resize(bufferSize * channels);
  }

  // Si le stream est ouvert, le recréer
  if (audio && audio->isStreamOpen()) {
    return setDevice(getCurrentDevice());
  }

  return true;
}

// Récupérer la taille du buffer
unsigned int AudioSystem::getBufferSize() const { return bufferSize; }

// Set master volume (0.0 - 1.0)
void AudioSystem::setMasterVolume(float volume) {
  // Clamp volume entre 0.0 et 1.0
  masterVolume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
}

// Get master volume
float AudioSystem::getMasterVolume() const { return masterVolume; }

// === Contrôles de réverbération ===

// Activer/désactiver la réverbération
void AudioSystem::enableReverb(bool enable) {
  reverbEnabled = enable;
  log_info("AUDIO", "REVERB: %s", enable ? "ON" : "OFF");
}

// Vérifier si la réverbération est activée
bool AudioSystem::isReverbEnabled() const { return reverbEnabled; }

// Régler le mix dry/wet (0.0 - 1.0)
void AudioSystem::setReverbMix(float mix) {
  if (mix < 0.0f)
    reverbMix = 0.0f;
  else if (mix > 1.0f)
    reverbMix = 1.0f;
  else
    reverbMix = mix;

  // Plus de log ici pour éviter les doublons avec les logs colorés de
  // midi_controller.cpp
}

// Obtenir le mix dry/wet actuel
float AudioSystem::getReverbMix() const { return reverbMix; }

// Régler la taille de la pièce (0.0 - 1.0)
void AudioSystem::setReverbRoomSize(float size) {
  if (size < 0.0f)
    reverbRoomSize = 0.0f;
  else if (size > 1.0f)
    reverbRoomSize = 1.0f;
  else
    reverbRoomSize = size;

  // Plus de log ici pour éviter les doublons avec les logs colorés de
  // midi_controller.cpp
}

// Obtenir la taille de la pièce actuelle
float AudioSystem::getReverbRoomSize() const { return reverbRoomSize; }

// Régler l'amortissement (0.0 - 1.0)
void AudioSystem::setReverbDamping(float damping) {
  if (damping < 0.0f)
    reverbDamping = 0.0f;
  else if (damping > 1.0f)
    reverbDamping = 1.0f;
  else
    reverbDamping = damping;

  // Plus de log ici pour éviter les doublons avec les logs colorés de
  // midi_controller.cpp
}

// Obtenir l'amortissement actuel
float AudioSystem::getReverbDamping() const { return reverbDamping; }

// Régler la largeur stéréo (0.0 - 1.0)
void AudioSystem::setReverbWidth(float width) {
  if (width < 0.0f)
    reverbWidth = 0.0f;
  else if (width > 1.0f)
    reverbWidth = 1.0f;
  else
    reverbWidth = width;

  // Plus de log ici pour éviter les doublons avec les logs colorés de
  // midi_controller.cpp
}

// Obtenir la largeur stéréo actuelle
float AudioSystem::getReverbWidth() const { return reverbWidth; }

// Set requested device ID for initialization
void AudioSystem::setRequestedDeviceId(int deviceId) {
  requestedDeviceId = deviceId;
  log_info("AUDIO", "Audio device ID %d requested for initialization", deviceId);
}

// Fonctions C pour la compatibilité avec le code existant
extern "C" {

// Fonctions de gestion des buffers audio
void resetAudioDataBufferOffset(void) {
  // Cette fonction n'est plus nécessaire avec RtAudio
  // Mais on la garde pour compatibilité
}

// Initialisation et nettoyage des données audio
void initAudioData(AudioData *audioData, UInt32 numChannels,
                   UInt32 bufferSize) {
  if (audioData) {
    audioData->numChannels = numChannels;
    audioData->bufferSize = bufferSize;
    audioData->buffers = (Float32 **)malloc(numChannels * sizeof(Float32 *));

    for (UInt32 i = 0; i < numChannels; i++) {
      audioData->buffers[i] = (Float32 *)calloc(bufferSize, sizeof(Float32));
    }
  }
}

void audio_Init(void) {
  // Initialiser les buffers pour la compatibilité
  for (int i = 0; i < 2; i++) {
    pthread_mutex_init(&buffers_L[i].mutex, NULL);
    pthread_mutex_init(&buffers_R[i].mutex, NULL);
    pthread_cond_init(&buffers_L[i].cond, NULL);
    pthread_cond_init(&buffers_R[i].cond, NULL);
    
    // CRITICAL: Initialize ready state atomically for RT-safe operation
    __atomic_store_n(&buffers_L[i].ready, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&buffers_R[i].ready, 0, __ATOMIC_SEQ_CST);

    // Allocate dynamic audio buffers based on runtime configuration
    if (!buffers_L[i].data) {
      buffers_L[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      // CRITICAL: Ensure buffers are zeroed (calloc should do this, but make sure)
      memset(buffers_L[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
    if (!buffers_R[i].data) {
      buffers_R[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      // CRITICAL: Ensure buffers are zeroed (calloc should do this, but make sure)
      memset(buffers_R[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
  }
  
  // CRITICAL: Initialize buffer index atomically
  __atomic_store_n(&current_buffer_index, 0, __ATOMIC_SEQ_CST);
  
  log_info("AUDIO", "RT-safe audio buffers initialized with zero content and atomic ready states");

  // Créer et initialiser le système audio RtAudio
  if (!gAudioSystem) {
    gAudioSystem = new AudioSystem();
    if (gAudioSystem) {
      gAudioSystem->initialize();
    }
  }

  // Initialiser l'égaliseur à 3 bandes
  if (!gEqualizer) {
    float sampleRate = (gAudioSystem) ? g_sp3ctra_config.sampling_frequency : 44100.0f;
    eq_Init(sampleRate);
    log_info("AUDIO", "Three-band equalizer initialized");
  }
}

void cleanupAudioData(AudioData *audioData) {
  if (audioData && audioData->buffers) {
    for (UInt32 i = 0; i < audioData->numChannels; i++) {
      if (audioData->buffers[i]) {
        free(audioData->buffers[i]);
      }
    }
    free(audioData->buffers);
    audioData->buffers = nullptr;
  }
}

void audio_Cleanup() {
  // Nettoyage des mutex et conditions
  for (int i = 0; i < 2; i++) {
    // Free dynamically allocated audio buffers
    if (buffers_L[i].data) {
      free(buffers_L[i].data);
      buffers_L[i].data = nullptr;
    }
    if (buffers_R[i].data) {
      free(buffers_R[i].data);
      buffers_R[i].data = nullptr;
    }
    pthread_mutex_destroy(&buffers_L[i].mutex);
    pthread_mutex_destroy(&buffers_R[i].mutex);
    pthread_cond_destroy(&buffers_L[i].cond);
    pthread_cond_destroy(&buffers_R[i].cond);
  }

  // Nettoyage du système RtAudio
  if (gAudioSystem) {
    delete gAudioSystem;
    gAudioSystem = nullptr;
  }

  // Nettoyage de l'égaliseur
  if (gEqualizer) {
    eq_Cleanup();
  }
}

// Fonctions de contrôle audio
int startAudioUnit() {
  if (gAudioSystem) {
    return gAudioSystem->start() ? 0 : -1;
  }
  return -1;
}

void stopAudioUnit() {
  if (gAudioSystem) {
    gAudioSystem->stop();
  }
}

// Lister les périphériques audio disponibles avec énumération complète
void printAudioDevices() {
  if (!gAudioSystem || !gAudioSystem->getAudioDevice()) {
    printf("Système audio non initialisé\n");
    return;
  }

  RtAudio* audio = gAudioSystem->getAudioDevice();
  unsigned int deviceCount = 0;
  unsigned int defaultDevice = 0;
  
  // Get device count with error handling
  try {
    deviceCount = audio->getDeviceCount();
    defaultDevice = audio->getDefaultOutputDevice();
  } catch (const std::exception &error) {
    printf("❌ Error getting device count: %s\n", error.what());
    return;
  }

  printf("Available output devices:\n");
  printf("🔧 Complete device enumeration (verbose mode)...\n");
  printf("🔍 RtAudio reports %d total devices\n", deviceCount);
  
  std::vector<std::pair<unsigned int, std::string>> accessibleDevices;
  int failedDevices = 0;
  int emptyDevices = 0;
  
  // Enhanced enumeration with better error handling for macOS
  // First, always try to show the default device (which we know works)
  printf("🎯 Default device (ID %d):\n", defaultDevice);
  try {
    RtAudio::DeviceInfo defaultInfo = audio->getDeviceInfo(defaultDevice);
    if (defaultInfo.outputChannels > 0 && !defaultInfo.name.empty()) {
      accessibleDevices.push_back(std::make_pair(defaultDevice, defaultInfo.name));
      
      printf("📋 Device ID %d: %s (Default Output) [%d channels]\n", 
             defaultDevice, defaultInfo.name.c_str(), defaultInfo.outputChannels);
      
      // Show supported sample rates
      printf("    Sample rates: ");
      if (!defaultInfo.sampleRates.empty()) {
        for (size_t j = 0; j < defaultInfo.sampleRates.size(); j++) {
          printf("%dHz", defaultInfo.sampleRates[j]);
          if (j < defaultInfo.sampleRates.size() - 1) printf(", ");
        }
      } else {
        printf("None reported");
      }
      printf("\n");
      
      // Show supported formats
      printf("    Formats: ");
      bool hasFormats = false;
      if (defaultInfo.nativeFormats & RTAUDIO_SINT16) { printf("INT16 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_SINT24) { printf("INT24 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_SINT32) { printf("INT32 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_FLOAT32) { printf("FLOAT32 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_FLOAT64) { printf("FLOAT64 "); hasFormats = true; }
      if (!hasFormats) printf("None reported");
      printf("\n");
    }
  } catch (const std::exception &error) {
    printf("❌ Default device ID %d: Query failed (%s)\n", defaultDevice, error.what());
  }
  
  printf("\n🔍 Scanning all reported device IDs (0-%d):\n", deviceCount - 1);
  
  // Then scan through the reported device range
  for (unsigned int i = 0; i < deviceCount; i++) {
    // Skip default device if we already processed it
    if (i == defaultDevice) {
      printf("ℹ️  Device ID %d: (Already shown as default device)\n", i);
      continue;
    }
    
    try {
      RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
      
      // Check if device has valid output channels
      if (info.outputChannels > 0) {
        // Check if device name is valid (not empty)
        if (!info.name.empty()) {
          accessibleDevices.push_back(std::make_pair(i, info.name));
          
          printf("📋 Device ID %d: %s [%d channels]\n", 
                 i, info.name.c_str(), info.outputChannels);
          
          // Show supported sample rates (with bounds checking)
          printf("    Sample rates: ");
          if (!info.sampleRates.empty()) {
            for (size_t j = 0; j < info.sampleRates.size(); j++) {
              printf("%dHz", info.sampleRates[j]);
              if (j < info.sampleRates.size() - 1) printf(", ");
            }
          } else {
            printf("None reported");
          }
          printf("\n");
          
          // Show supported formats
          printf("    Formats: ");
          bool hasFormats = false;
          if (info.nativeFormats & RTAUDIO_SINT16) { printf("INT16 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_SINT24) { printf("INT24 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_SINT32) { printf("INT32 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_FLOAT32) { printf("FLOAT32 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_FLOAT64) { printf("FLOAT64 "); hasFormats = true; }
          if (!hasFormats) printf("None reported");
          printf("\n");
          
          // Additional diagnostic info for macOS
          printf("    Input channels: %d, Duplex channels: %d\n", 
                 info.inputChannels, info.duplexChannels);
          if (info.isDefaultOutput) printf("    ✅ Marked as default output\n");
          if (info.isDefaultInput) printf("    ✅ Marked as default input\n");
          
        } else {
          emptyDevices++;
          printf("⚠️  Device ID %d: Has %d output channels but empty name\n", 
                 i, info.outputChannels);
        }
      } else {
        // Device exists but has no output channels - show minimal info
        if (!info.name.empty()) {
          printf("ℹ️  Device ID %d: %s [Input only - %d input channels]\n", 
                 i, info.name.c_str(), info.inputChannels);
        } else {
          printf("ℹ️  Device ID %d: Unnamed device [Input only - %d input channels]\n", 
                 i, info.inputChannels);
        }
      }
    } catch (const std::exception &error) {
      failedDevices++;
      printf("❌ Device ID %d: Query failed (%s)\n", i, error.what());
      
      // On macOS, this is often due to device enumeration issues
      // Continue with next device instead of stopping
      continue;
    }
  }
  
  printf("\n🎵 Summary: %zu accessible devices, %d failed queries, %d empty names\n", 
         accessibleDevices.size(), failedDevices, emptyDevices);
  
  if (accessibleDevices.empty()) {
    printf("❌ No accessible audio output devices found!\n");
    printf("💡 Troubleshooting for macOS:\n");
    printf("   1. Check System Preferences > Sound > Output\n");
    printf("   2. Try disconnecting/reconnecting USB audio devices\n");
    printf("   3. Restart Audio MIDI Setup application\n");
    printf("   4. Check if other audio applications are blocking access\n");
    printf("   5. Try running: sudo killall coreaudiod\n");
  } else {
    printf("✅ Use --audio-device=<ID> to select a specific device\n");
    printf("✅ Default device ID %d will be used if none specified\n", defaultDevice);
    
    // Show quick device list for easy reference
    printf("\n📋 Quick device reference:\n");
    for (const auto& device : accessibleDevices) {
      printf("   ID %d: %s%s\n", device.first, device.second.c_str(),
             (device.first == defaultDevice) ? " (default)" : "");
    }
  }
}

// Définir le périphérique audio actif
int setAudioDevice(unsigned int deviceId) {
  if (gAudioSystem) {
    // Set the requested device ID for next initialization
    gAudioSystem->setRequestedDeviceId((int)deviceId);
    return gAudioSystem->setDevice(deviceId) ? 1 : 0;
  }
  return 0;
}

void setRequestedAudioDevice(int deviceId) {
  if (gAudioSystem) {
    gAudioSystem->setRequestedDeviceId(deviceId);
  } else {
    // Store the device ID for when audio system gets created
    g_requested_audio_device_id = deviceId;
  }
}

void setRequestedAudioDeviceName(const char* deviceName) {
  // Store the device name for when audio system gets created
  if (g_requested_audio_device_name) {
    free(g_requested_audio_device_name);
  }
  g_requested_audio_device_name = strdup(deviceName);
  printf("Audio device name '%s' requested for initialization\n", deviceName);
}

// Control minimal callback mode for debugging audio dropouts
void setMinimalCallbackMode(int enabled) {
  use_minimal_callback = (enabled != 0);
  printf("🔧 Audio callback mode: %s\n",
         enabled ? "MINIMAL (440Hz test tone)" : "FULL (synth processing)");
}

void setMinimalTestVolume(float volume) {
  minimal_test_volume = (volume < 0.0f)   ? 0.0f
                        : (volume > 1.0f) ? 1.0f
                                          : volume;
  printf("🔊 Minimal test volume set to: %.2f\n", minimal_test_volume);
}

void setSynthAdditiveMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_additive_mix_level = level;
}

void setSynthPolyphonicMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_polyphonic_mix_level = level;
}

float getSynthAdditiveMixLevel(void) {
  return g_synth_additive_mix_level;
}

float getSynthPolyphonicMixLevel(void) {
  return g_synth_polyphonic_mix_level;
}

void setSynthPhotowaveMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_photowave_mix_level = level;
}

float getSynthPhotowaveMixLevel(void) {
  return g_synth_photowave_mix_level;
}

} // extern "C"
