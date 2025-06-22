/* audio_rtaudio.cpp */

#include "audio_rtaudio.h"
#include "audio_c_api.h"
#include "midi_controller.h" // For gMidiController
#include "synth_fft.h"       // For fft_audio_buffers and related variables
#include <cstring>
#include <iostream>
#include <rtaudio/RtAudio.h> // Explicitly include RtAudio.h
#include <stdexcept>         // For std::exception

// Variables globales pour compatibilité avec l'ancien code
AudioDataBuffers buffers_L[2];
AudioDataBuffers buffers_R[2];
volatile int current_buffer_index = 0;
pthread_mutex_t buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

AudioSystem *gAudioSystem = nullptr;

// Global variable to store requested audio device ID before AudioSystem is
// created
extern "C" {
int g_requested_audio_device_id = -1;
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
    static const float sample_rate = 48000.0f;
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
  static unsigned int fft_readOffset = 0;
  static int fft_localReadIndex = 0;

  // Cache MIDI levels to avoid repeated function calls
  static float cached_level_ifft = 1.0f;
  static float cached_level_fft = 0.5f;
  static float cached_volume = 1.0f;
  static int cache_counter = 0;

  // Update cache every 64 calls (~1.33ms at 48kHz, 0.67ms at 96kHz)
  if (++cache_counter >= 64) {
    cache_counter = 0;
    cached_volume = this->masterVolume;
    if (gMidiController && gMidiController->isAnyControllerConnected()) {
      cached_level_ifft = gMidiController->getMixLevelSynthIfft();
      cached_level_fft = gMidiController->getMixLevelSynthFft();
    }
  }

  unsigned int framesToRender = nFrames;

  // Process frames using multiple buffers if needed
  while (framesToRender > 0) {
    // How many frames available in current buffer?
    unsigned int framesAvailable = AUDIO_BUFFER_SIZE - readOffset;
    unsigned int chunk =
        (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

    // Get source pointers directly - avoid memcpy when possible
    float *source_ifft = nullptr;
    float *source_fft = nullptr;

    if (buffers_R[localReadIndex].ready == 1) {
      source_ifft = &buffers_R[localReadIndex].data[readOffset];
    }

    if (fft_audio_buffers[fft_localReadIndex].ready == 1) {
      unsigned int fft_framesAvailable = AUDIO_BUFFER_SIZE - fft_readOffset;
      if (fft_framesAvailable >= chunk) {
        source_fft =
            &fft_audio_buffers[fft_localReadIndex].data[fft_readOffset];
      }
    }

    // Get reverb send levels (cached less frequently for performance)
    static float cached_reverb_send_ifft = 0.7f;
    static float cached_reverb_send_fft = 0.0f;
    static int reverb_cache_counter = 0;

    // Update reverb cache every 128 calls (~1.33ms at 96kHz)
    if (++reverb_cache_counter >= 128) {
      reverb_cache_counter = 0;
      if (gMidiController && gMidiController->isAnyControllerConnected()) {
        cached_reverb_send_ifft = gMidiController->getReverbSendSynthIfft();
        cached_reverb_send_fft = gMidiController->getReverbSendSynthFft();
      }
    }

    // OPTIMIZED MIXING - Direct to output with threaded reverb
    for (unsigned int i = 0; i < chunk; i++) {
      float dry_sample = 0.0f;

      // Add IFFT contribution
      if (source_ifft) {
        dry_sample += source_ifft[i] * cached_level_ifft;
      }

      // Add FFT contribution
      if (source_fft) {
        dry_sample += source_fft[i] * cached_level_fft;
      }

      // Send to reverb thread (non-blocking)
      if (reverbEnabled &&
          (cached_reverb_send_ifft > 0.01f || cached_reverb_send_fft > 0.01f)) {
        float reverb_input = 0.0f;
        if (source_ifft && cached_reverb_send_ifft > 0.01f) {
          reverb_input +=
              source_ifft[i] * cached_level_ifft * cached_reverb_send_ifft;
        }
        if (source_fft && cached_reverb_send_fft > 0.01f) {
          reverb_input +=
              source_fft[i] * cached_level_fft * cached_reverb_send_fft;
        }

        // Non-blocking write to reverb thread (ignore if buffer full)
        writeToReverbInput(reverb_input);
      }

      // Read reverb output (non-blocking)
      float reverb_left = 0.0f, reverb_right = 0.0f;
      readFromReverbOutput(reverb_left,
                           reverb_right); // Returns silence if no data

      // Mix dry + reverb and apply volume
      float final_left = (dry_sample + reverb_left) * cached_volume;
      float final_right = (dry_sample + reverb_right) * cached_volume;

      // Limiting
      final_left = (final_left > 1.0f)    ? 1.0f
                   : (final_left < -1.0f) ? -1.0f
                                          : final_left;
      final_right = (final_right > 1.0f)    ? 1.0f
                    : (final_right < -1.0f) ? -1.0f
                                            : final_right;

      // Output
      outLeft[i] = final_left;
      outRight[i] = final_right;
    }

    // Advance pointers
    outLeft += chunk;
    outRight += chunk;
    readOffset += chunk;
    fft_readOffset += chunk;
    framesToRender -= chunk;

    // Handle buffer transitions - IFFT
    if (readOffset >= AUDIO_BUFFER_SIZE) {
      if (buffers_R[localReadIndex].ready == 1) {
        pthread_mutex_lock(&buffers_R[localReadIndex].mutex);
        buffers_R[localReadIndex].ready = 0;
        pthread_cond_signal(&buffers_R[localReadIndex].cond);
        pthread_mutex_unlock(&buffers_R[localReadIndex].mutex);
      }
      localReadIndex = (localReadIndex == 0) ? 1 : 0;
      readOffset = 0;
    }

    // Handle buffer transitions - FFT
    if (fft_readOffset >= AUDIO_BUFFER_SIZE) {
      if (fft_audio_buffers[fft_localReadIndex].ready == 1) {
        pthread_mutex_lock(&fft_audio_buffers[fft_localReadIndex].mutex);
        fft_audio_buffers[fft_localReadIndex].ready = 0;
        pthread_cond_signal(&fft_audio_buffers[fft_localReadIndex].cond);
        pthread_mutex_unlock(&fft_audio_buffers[fft_localReadIndex].mutex);
      }
      fft_localReadIndex = (fft_localReadIndex == 0) ? 1 : 0;
      fft_readOffset = 0;
    }
  }

  return 0;
}

// Constructeur
AudioSystem::AudioSystem(unsigned int sampleRate, unsigned int bufferSize,
                         unsigned int channels)
    : audio(nullptr), isRunning(false), // Moved isRunning before members that
                                        // might use it implicitly or explicitly
      sampleRate(sampleRate), bufferSize(bufferSize), channels(channels),
      requestedDeviceId(g_requested_audio_device_id), // Use global variable if
                                                      // set, otherwise -1
      masterVolume(1.0f), reverbBuffer(nullptr), reverbMix(0.5f),
      reverbRoomSize(0.95f), reverbDamping(0.4f), reverbWidth(1.0f),
      reverbEnabled(true), reverbThreadRunning(false) {

  std::cout << "\033[1;32m[ZitaRev1] Reverb enabled by default with "
               "Zita-Rev1 algorithm\033[0m"
            << std::endl;

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
  std::cout << "Attempting to initialize RtAudio with ALSA API..." << std::endl;
  audio = new RtAudio(RtAudio::LINUX_ALSA);
#else
  audio = new RtAudio();
#endif

  // Vérifier si RtAudio a été correctement créé
  if (!audio) {
    std::cerr << "Unable to create RtAudio instance" << std::endl;
    return false;
  }

  // Get and print available devices
  unsigned int deviceCount = 0;
  try {
    deviceCount = audio->getDeviceCount();
  } catch (const std::exception &error) {
    std::cerr << "Error getting device count: " << error.what() << std::endl;
    delete audio;
    audio = nullptr;
    return false;
  }

  std::cout << "Available output devices:" << std::endl;
  unsigned int preferredDeviceId = audio->getDefaultOutputDevice(); // Default
  bool foundSpecificPreferred = false;
  bool foundRequestedDevice = false;

  // FORCE BOSSDAC USAGE - Auto-detect and force BossDAC selection
  std::cout << "🔧 Auto-detecting BossDAC for forced usage..." << std::endl;
  for (unsigned int i = 0; i < deviceCount; i++) {
    try {
      RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
      if (info.outputChannels > 0) {
        std::string deviceName(info.name);
        if (deviceName.find("BossDAC") != std::string::npos ||
            deviceName.find("pcm512x") != std::string::npos) {
          preferredDeviceId = i;
          foundSpecificPreferred = true;
          std::cout << "🎯 FORCED: Using BossDAC device ID " << i << ": "
                    << deviceName << std::endl;
          break;
        }
      }
    } catch (const std::exception &error) {
      // Skip problematic devices during BossDAC detection
    }
  }

  // First, check if a specific device was requested
  if (requestedDeviceId >= 0) {
    std::cout << "User requested specific audio device ID: "
              << requestedDeviceId << std::endl;
  }

  for (unsigned int i = 0; i < deviceCount; i++) {
    try {
      RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
      bool isDefault = info.isDefaultOutput; // Store default status

      if (info.outputChannels > 0) { // Only consider output devices
        std::cout << "  Device ID " << i << ": " << info.name
                  << (isDefault ? " (Default Output)" : "")
                  << " [Output Channels: " << info.outputChannels << "]"
                  << std::endl;

        // Check if this is the requested device
        if (requestedDeviceId >= 0 && i == (unsigned int)requestedDeviceId) {
          std::cout << "    -> Found requested device ID " << i << ": "
                    << info.name << std::endl;
          preferredDeviceId = i;
          foundRequestedDevice = true;
          foundSpecificPreferred = true;
        } else if (requestedDeviceId < 0) { // Only do auto-detection if no
                                            // specific device requested
          std::string deviceName(info.name);
          // Prefer "Headphones" or "bcm2835 ALSA" (non-HDMI)
          bool isHeadphones =
              (deviceName.find("Headphones") != std::string::npos);
          bool isAnalogue =
              (deviceName.find("bcm2835 ALSA") != std::string::npos &&
               deviceName.find("HDMI") == std::string::npos);

          if (isHeadphones) {
            std::cout << "    -> Found 'Headphones' device: " << deviceName
                      << " with ID " << i << std::endl;
            preferredDeviceId = i;
            foundSpecificPreferred = true;
            // Prioritize headphones over other analogue if both found
          } else if (isAnalogue && !foundSpecificPreferred) {
            std::cout << "    -> Found 'bcm2835 ALSA' (non-HDMI) device: "
                      << deviceName << " with ID " << i << std::endl;
            preferredDeviceId = i;
            // Don't set foundSpecificPreferred to true here, to allow
            // 'Headphones' to override if found later
          }
        }
      }
    } catch (const std::exception &error) {
      // This is where the "Unknown error 524" might originate if getDeviceInfo
      // fails for certain hw IDs
      std::cerr
          << "RtApiAlsa::getDeviceInfo: snd_pcm_open error for device (hw:" << i
          << ",0 likely), " << error.what() << std::endl;
    }
  }

  // Validate requested device if specified
  if (requestedDeviceId >= 0 && !foundRequestedDevice) {
    std::cerr << "ERROR: Requested audio device ID " << requestedDeviceId
              << " not found or not available!" << std::endl;
    std::cerr << "Available device IDs with output channels: ";
    for (unsigned int i = 0; i < deviceCount; i++) {
      try {
        RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
        if (info.outputChannels > 0) {
          std::cerr << i << " ";
        }
      } catch (const std::exception &error) {
        // Skip problematic devices
      }
    }
    std::cerr << std::endl;
    delete audio;
    audio = nullptr;
    return false;
  }

  if (foundSpecificPreferred) {
    std::cout << "Using specifically preferred device ID " << preferredDeviceId
              << " (" << audio->getDeviceInfo(preferredDeviceId).name << ")"
              << std::endl;
  } else if (preferredDeviceId != audio->getDefaultOutputDevice() &&
             deviceCount > 0) { // A bcm2835 ALSA (non-HDMI) was found but not
                                // "Headphones"
    std::cout << "Using preferred analogue device ID " << preferredDeviceId
              << " (" << audio->getDeviceInfo(preferredDeviceId).name << ")"
              << std::endl;
  } else if (deviceCount > 0) {
    std::cout
        << "No specific preferred device found, using default output device ID "
        << preferredDeviceId << " ("
        << audio->getDeviceInfo(preferredDeviceId).name << ")" << std::endl;
  } else {
    std::cout << "No output devices found!" << std::endl;
    delete audio;
    audio = nullptr;
    return false;
  }

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
  options.streamName = "CISYNTH_Pi5_Optimized";
#else
  options.numberOfBuffers = 8; // Standard for x86/x64
  options.streamName = "CISYNTH_Standard";
#endif

  // High priority for audio thread on Pi Module 5
  options.priority = 90; // Real-time priority for audio processing

  // DIAGNOSTIC: Vérifier les capacités du périphérique avant ouverture
  std::cout << "\n=== DIAGNOSTIC PÉRIPHÉRIQUE AUDIO ===" << std::endl;
  std::cout << "Device ID demandé: " << preferredDeviceId << std::endl;

  try {
    RtAudio::DeviceInfo deviceInfo = audio->getDeviceInfo(preferredDeviceId);
    std::cout << "✅ getDeviceInfo() réussie pour device " << preferredDeviceId
              << std::endl;
    std::cout << "Device Name: " << deviceInfo.name << std::endl;
    std::cout << "Output Channels: " << deviceInfo.outputChannels << std::endl;
    std::cout << "Duplex Channels: " << deviceInfo.duplexChannels << std::endl;
    std::cout << "Input Channels: " << deviceInfo.inputChannels << std::endl;
    std::cout << "Native Formats: ";
    if (deviceInfo.nativeFormats & RTAUDIO_SINT8)
      std::cout << "INT8 ";
    if (deviceInfo.nativeFormats & RTAUDIO_SINT16)
      std::cout << "INT16 ";
    if (deviceInfo.nativeFormats & RTAUDIO_SINT24)
      std::cout << "INT24 ";
    if (deviceInfo.nativeFormats & RTAUDIO_SINT32)
      std::cout << "INT32 ";
    if (deviceInfo.nativeFormats & RTAUDIO_FLOAT32)
      std::cout << "FLOAT32 ";
    if (deviceInfo.nativeFormats & RTAUDIO_FLOAT64)
      std::cout << "FLOAT64 ";
    std::cout << std::endl;
    std::cout << "Sample Rates: ";
    for (unsigned int rate : deviceInfo.sampleRates) {
      std::cout << rate << "Hz ";
    }
    std::cout << std::endl;
    std::cout << "Preferred Sample Rate: " << deviceInfo.preferredSampleRate
              << "Hz" << std::endl;

    // Vérifier si la fréquence configurée est supportée
    bool supportsConfigRate = false;
    unsigned int configRate = SAMPLING_FREQUENCY;
    for (unsigned int rate : deviceInfo.sampleRates) {
      if (rate == configRate) {
        supportsConfigRate = true;
        break;
      }
    }

    if (!supportsConfigRate) {
      std::cerr << "\n❌ ERREUR: Le périphérique ne supporte pas " << configRate
                << "Hz !" << std::endl;
      std::cerr << "Fréquences supportées: ";
      for (unsigned int rate : deviceInfo.sampleRates) {
        std::cerr << rate << "Hz ";
      }
      std::cerr << std::endl;
      // Continuons quand même pour voir ce qui se passe
    } else {
      std::cout << "✅ Le périphérique supporte " << configRate << "Hz"
                << std::endl;
    }
  } catch (std::exception &e) {
    std::cerr << "❌ getDeviceInfo() a échoué: " << e.what() << std::endl;
    std::cerr
        << "Impossible de récupérer les capacités détaillées du périphérique."
        << std::endl;
    std::cerr << "Cela explique peut-être pourquoi ALSA génère des erreurs 524."
              << std::endl;
  }
  std::cout << "======================================\n" << std::endl;

  // Use SAMPLING_FREQUENCY from config.h instead of hard-coding 96kHz
  unsigned int configSampleRate = SAMPLING_FREQUENCY;
  if (sampleRate != configSampleRate) {
    std::cout << "🔧 CONFIGURATION: Changement de " << sampleRate << "Hz vers "
              << configSampleRate << "Hz (défini dans config.h)" << std::endl;
    sampleRate = configSampleRate;
  }

  // Ouvrir le flux audio avec les options de faible latence
  try {
    std::cout << "Tentative d'ouverture du stream avec:" << std::endl;
    std::cout << "  - Device ID: " << params.deviceId << std::endl;
    std::cout << "  - Channels: " << params.nChannels << std::endl;
    std::cout << "  - Sample Rate: " << sampleRate << "Hz" << std::endl;
    std::cout << "  - Buffer Size: " << bufferSize << " frames" << std::endl;
    std::cout << "  - Format: RTAUDIO_FLOAT32" << std::endl;

    audio->openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    // Vérifier la fréquence réellement négociée
    if (audio->isStreamOpen()) {
      std::cout << "✅ Stream ouvert avec succès !" << std::endl;

      unsigned int actualSampleRate = audio->getStreamSampleRate();
      std::cout << "📊 Fréquence négociée: " << actualSampleRate << "Hz"
                << std::endl;
      std::cout << "📊 Latence du stream: " << audio->getStreamLatency()
                << " frames" << std::endl;

      std::cout << "\n🔍 DIAGNOSTIC CRITIQUE:" << std::endl;
      std::cout << "   Demandé: " << configSampleRate << "Hz" << std::endl;
      std::cout << "   Négocié: " << actualSampleRate << "Hz" << std::endl;

      if (actualSampleRate != configSampleRate) {
        std::cerr << "\n🚨 PROBLÈME DÉTECTÉ !" << std::endl;
        std::cerr << "   Le périphérique ne supporte PAS " << configSampleRate
                  << "Hz !" << std::endl;
        std::cerr << "   Il fonctionne à " << actualSampleRate
                  << "Hz au lieu de " << configSampleRate << "Hz" << std::endl;

        float pitchRatio = (float)actualSampleRate / (float)configSampleRate;
        std::cerr << "   Ratio de pitch: " << pitchRatio << " ("
                  << (pitchRatio < 1.0f ? "plus grave" : "plus aigu") << ")"
                  << std::endl;

        if (configSampleRate == 96000 && actualSampleRate == 48000) {
          std::cerr
              << "   Vos sons sont d'UNE OCTAVE plus grave (48kHz vs 96kHz) !"
              << std::endl;
        }

        std::cerr << "\n💡 SOLUTIONS POSSIBLES:" << std::endl;
        std::cerr << "   1. Changer SAMPLING_FREQUENCY à " << actualSampleRate
                  << " dans config.h" << std::endl;
        std::cerr << "   2. Utiliser un périphérique supportant "
                  << configSampleRate << "Hz" << std::endl;
        std::cerr << "   3. Vérifier que votre récepteur audio supporte "
                  << configSampleRate << "Hz" << std::endl;
      } else {
        std::cout << "🎯 PARFAIT: " << configSampleRate
                  << "Hz négocié avec succès !" << std::endl;
        std::cout << "   Votre configuration audio est optimale." << std::endl;
      }
      std::cout << "======================================\n" << std::endl;
    }
  } catch (std::exception &e) {
    std::cerr << "RtAudio error: " << e.what() << std::endl;
    delete audio;    // Nettoyer l'objet RtAudio en cas d'échec
    audio = nullptr; // Mettre le pointeur à nullptr
    return false;
  }

  std::cout << "RtAudio initialisé: "
            << "SR=" << sampleRate << "Hz, "
            << "BS=" << bufferSize << " frames, "
            << "Latence=" << bufferSize * 1000.0 / sampleRate << "ms"
            << std::endl;

  return true;
}

// Démarrage du flux audio
bool AudioSystem::start() {
  if (!audio || !audio->isStreamOpen())
    return false;

  // Démarrer le thread de réverbération avant le stream audio
  if (!reverbThreadRunning.load()) {
    reverbThreadRunning.store(true);
    try {
      reverbThread = std::thread(&AudioSystem::reverbThreadFunction, this);
      std::cout
          << "\033[1;33m[REVERB THREAD] Thread de réverbération lancé\033[0m"
          << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Erreur création thread réverbération: " << e.what()
                << std::endl;
      reverbThreadRunning.store(false);
      return false;
    }
  }

  try {
    audio->startStream();
  } catch (std::exception &e) {
    std::cerr << "Erreur démarrage RtAudio: " << e.what() << std::endl;

    // Arrêter le thread de réverbération en cas d'échec
    if (reverbThreadRunning.load()) {
      reverbThreadRunning.store(false);
      if (reverbThread.joinable()) {
        reverbThread.join();
      }
    }
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
      std::cerr << "Erreur arrêt RtAudio: " << e.what() << std::endl;
    }
    isRunning = false;
  }

  // Arrêter le thread de réverbération
  if (reverbThreadRunning.load()) {
    reverbThreadRunning.store(false);
    reverbCondition.notify_all(); // Réveiller le thread s'il attend

    if (reverbThread.joinable()) {
      reverbThread.join();
      std::cout
          << "\033[1;33m[REVERB THREAD] Thread de réverbération joint\033[0m"
          << std::endl;
    }
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
    audio->openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    if (wasRunning) {
      audio->startStream();
    }
  } catch (std::exception &e) {
    std::cerr << "Erreur changement périphérique: " << e.what() << std::endl;
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
  std::cout << "\033[1;36mREVERB: " << (enable ? "ON" : "OFF") << "\033[0m"
            << std::endl;
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
  std::cout << "Audio device ID " << deviceId << " requested for initialization"
            << std::endl;
}

// Fonctions C pour la compatibilité avec le code existant
extern "C" {

// Fonctions de gestion des buffers audio
void resetAudioDataBufferOffset(void) {
  // Cette fonction n'est plus nécessaire avec RtAudio
  // Mais on la garde pour compatibilité
}

int getAudioDataBufferOffset(void) {
  // Cette fonction n'est plus nécessaire avec RtAudio
  return AUDIO_BUFFER_OFFSET_NONE;
}

void setAudioDataBufferOffsetHALF(void) {
  // Cette fonction n'est plus nécessaire avec RtAudio
}

void setAudioDataBufferOffsetFULL(void) {
  // Cette fonction n'est plus nécessaire avec RtAudio
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
    buffers_L[i].ready = 0;
    buffers_R[i].ready = 0;
  }

  // Créer et initialiser le système audio RtAudio
  if (!gAudioSystem) {
    gAudioSystem = new AudioSystem();
    if (gAudioSystem) {
      gAudioSystem->initialize();
    }
  }

  // Initialiser l'égaliseur à 3 bandes
  if (!gEqualizer) {
    float sampleRate = (gAudioSystem) ? SAMPLING_FREQUENCY : 44100.0f;
    eq_Init(sampleRate);
    std::cout
        << "\033[1;32m[ThreeBandEQ] Égaliseur à 3 bandes initialisé\033[0m"
        << std::endl;
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

// Lister les périphériques audio disponibles
void printAudioDevices() {
  if (!gAudioSystem || !gAudioSystem->getAudioDevice()) {
    printf("Système audio non initialisé\n");
    return;
  }

  std::vector<std::string> devices = gAudioSystem->getAvailableDevices();
  unsigned int defaultDevice =
      gAudioSystem->getAudioDevice()->getDefaultOutputDevice();

  printf("Périphériques audio disponibles:\n");
  for (unsigned int i = 0; i < devices.size(); i++) {
    printf("  [%d] %s %s\n", i, devices[i].c_str(),
           (i == defaultDevice) ? "(défaut)" : "");
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

} // extern "C"
