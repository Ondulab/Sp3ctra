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
  // Configuration stéréo non-entrelacée (comme RtAudio par défaut)
  float *outLeft = outputBuffer;
  float *outRight = outputBuffer + nFrames;

  // Variables statiques pour maintenir l'état entre les appels
  static unsigned int readOffset = 0; // Combien de frames ont été consommées
                                      // dans le buffer actuel (synth_IfftMode)
  static int localReadIndex =
      0; // Quel buffer on lit actuellement (synth_IfftMode)

  // Variables pour synth_fftMode
  static unsigned int fft_readOffset = 0;
  static int fft_localReadIndex = 0;
  static float tempFftBuffer[AUDIO_BUFFER_SIZE]; // Buffer temporaire pour
                                                 // synth_fftMode data

  // Buffer temporaire pour appliquer le volume (sera utilisé pour le signal de
  // synth_IfftMode initialement)
  static float tempBuffer[AUDIO_BUFFER_SIZE];

  unsigned int framesToRender = nFrames;

  // Traitement des frames demandées, potentiellement en utilisant plusieurs
  // buffers
  while (framesToRender > 0) {
    // Combien de frames reste-t-il dans le buffer actuel? (synth_IfftMode)
    unsigned int framesAvailable = AUDIO_BUFFER_SIZE - readOffset;

    // On consomme soit tout ce qui reste, soit ce dont on a besoin
    unsigned int chunk =
        (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

    // Copier les données de synth_IfftMode dans tempBuffer
    if (buffers_R[localReadIndex].ready != 1) {
      // Underrun pour synth_IfftMode, remplir tempBuffer de silence pour ce
      // chunk Cela évite de silencier synth_fft si synth_ifft n'est pas prêt.
      memset(tempBuffer, 0, chunk * sizeof(float));
      // Ne pas 'break' ici, continuer pour traiter synth_fft.
      // La gestion de readOffset et localReadIndex pour buffers_R se fera quand
      // même, mais elle lira potentiellement des buffers non prêts ou anciens
      // si le producteur IFFT est bloqué. C'est un pis-aller pour le débogage.
      // Le producteur IFFT doit être vérifié.
    } else {
      memcpy(tempBuffer, &buffers_R[localReadIndex].data[readOffset],
             chunk * sizeof(float));
    }

    // Récupérer les niveaux de mixage
    float level_ifft = 0.0f; // Default for IFFT set to 0.0f as requested
    float level_fft = 0.5f;  // Default if no controller - Kept at 0.5f
    if (gMidiController && gMidiController->isAnyControllerConnected()) {
      level_ifft = gMidiController->getMixLevelSynthIfft();
      // The duplicate line for level_ifft inside the if was already removed or
      // is a comment.
      level_fft = gMidiController->getMixLevelSynthFft();
    }

    // DEBUG: Levels are now controlled by MIDI or defaults again
    // level_ifft = 0.0f; // REVERTED
    // level_fft = 0.5f; // REVERTED

    // Lire les données de synth_fftMode
    if (fft_audio_buffers[fft_localReadIndex].ready != 1) {
      // Underrun pour synth_fftMode, remplir tempFftBuffer de silence
      memset(tempFftBuffer, 0, chunk * sizeof(float));
    } else {
      // Copier les données de synth_fftMode dans tempFftBuffer
      unsigned int fft_framesAvailable = AUDIO_BUFFER_SIZE - fft_readOffset;
      unsigned int fft_chunk =
          (chunk < fft_framesAvailable)
              ? chunk
              : fft_framesAvailable; // Should be same as 'chunk' if buffers are
                                     // aligned

      if (fft_chunk <
          chunk) { // Not enough data in current fft buffer, fill rest with 0
        memcpy(tempFftBuffer,
               &fft_audio_buffers[fft_localReadIndex].data[fft_readOffset],
               fft_chunk * sizeof(float));
        memset(tempFftBuffer + fft_chunk, 0,
               (chunk - fft_chunk) * sizeof(float));
      } else {
        memcpy(tempFftBuffer,
               &fft_audio_buffers[fft_localReadIndex].data[fft_readOffset],
               chunk * sizeof(float));
      }
    }

    // Récupérer les niveaux d'envoi vers la réverbération
    float reverb_send_ifft =
        0.7f; // Valeur par défaut identique au constructeur MidiController
    float reverb_send_fft = 0.0f; // Valeur par défaut (initialement à 0)
    if (gMidiController && gMidiController->isAnyControllerConnected()) {
      reverb_send_ifft = gMidiController->getReverbSendSynthIfft();
      reverb_send_fft = gMidiController->getReverbSendSynthFft();
    }

    // DEBUG: Ensure FFT path is dry for testing - REVERTED
    // reverb_send_fft = 0.0f;

    // Appliquer le volume master (avec log pour le débogage)
    static float lastLoggedVolume = -1.0f;
    float currentVolume = this->masterVolume;

    // Log le volume seulement s'il a changé significativement
    if (fabs(currentVolume - lastLoggedVolume) > 0.01f) {
      // std::cout << "AUDIO: Applying volume: " << currentVolume << std::endl;
      lastLoggedVolume = currentVolume;
    }

    // Buffers temporaires pour stocker les sorties de réverbération
    float reverbOutputL_ifft[AUDIO_BUFFER_SIZE];
    float reverbOutputR_ifft[AUDIO_BUFFER_SIZE];
    float reverbOutputL_fft[AUDIO_BUFFER_SIZE];
    float reverbOutputR_fft[AUDIO_BUFFER_SIZE];

    // Traiter la réverbération pour le signal synth_IFFT
    for (unsigned int i = 0; i < chunk; i++) {
      float sample_ifft = tempBuffer[i] * currentVolume; // Apply volume
      float inputL_ifft = sample_ifft;
      float inputR_ifft = sample_ifft; // Même signal pour les deux canaux

      // Appliquer la réverbération au signal synth_IFFT
      if (reverb_send_ifft > 0.0f && reverbEnabled) {
        // Processus de réverbération ajusté par le niveau d'envoi
        float tempL, tempR;
        processReverb(inputL_ifft * reverb_send_ifft,
                      inputR_ifft * reverb_send_ifft, tempL, tempR);
        // Stocker le signal réverbéré
        reverbOutputL_ifft[i] = tempL;
        reverbOutputR_ifft[i] = tempR;
      } else {
        // Pas d'envoi à la réverbération - affecter des zéros pour éviter le
        // bruit
        reverbOutputL_ifft[i] = 0.0f;
        reverbOutputR_ifft[i] = 0.0f;
      }
    }

    // Pour le signal synth_FFT, simplifions considérablement le traitement pour
    // éviter les problèmes
    // Ne traiter la réverbération que si BOTH le volume FFT ET le niveau
    // d'envoi reverb sont > 0
    bool fft_has_reverb =
        (reverb_send_fft > 0.01f) && reverbEnabled && (level_fft > 0.01f);

    if (fft_has_reverb) {
      // Préparer un buffer temporaire pour le traitement de réverbération du
      // synth_FFT
      float temp_fft_buffer[AUDIO_BUFFER_SIZE];

      // Copier les données avec volume ET niveau de mixage appliqués
      for (unsigned int i = 0; i < chunk; i++) {
        temp_fft_buffer[i] =
            tempFftBuffer[i] * currentVolume * reverb_send_fft * level_fft;
      }

      // Appliquer la réverbération au buffer entier plutôt qu'échantillon par
      // échantillon
      float tempOutL[AUDIO_BUFFER_SIZE] = {0};
      float tempOutR[AUDIO_BUFFER_SIZE] = {0};

      // Traiter la réverbération en une seule fois
      for (unsigned int i = 0; i < chunk; i++) {
        float tmp_l = 0, tmp_r = 0;
        processReverb(temp_fft_buffer[i], temp_fft_buffer[i], tmp_l, tmp_r);
        tempOutL[i] = tmp_l;
        tempOutR[i] = tmp_r;
      }

      // Stocker les résultats
      for (unsigned int i = 0; i < chunk; i++) {
        reverbOutputL_fft[i] = tempOutL[i];
        reverbOutputR_fft[i] = tempOutR[i];
      }
    } else {
      // Aucune réverbération pour synth_FFT - mettre des zéros
      for (unsigned int i = 0; i < chunk; i++) {
        reverbOutputL_fft[i] = 0.0f;
        reverbOutputR_fft[i] = 0.0f;
      }
    }

    // Mixer tous les signaux ensemble (dry + wet) avec une approche simplifiée
    for (unsigned int i = 0; i < chunk; i++) {
      // Signal sec du synth IFFT avec volume et mixage appliqués
      float dry_ifft = tempBuffer[i] * currentVolume * level_ifft;

      // Signal sec du synth FFT avec volume et mixage appliqués
      float dry_fft = tempFftBuffer[i] * currentVolume * level_fft;

      // Signal humide du synth IFFT
      float wet_ifft_L = reverbOutputL_ifft[i];
      float wet_ifft_R = reverbOutputR_ifft[i];

      // Signal humide du synth FFT
      float wet_fft_L = reverbOutputL_fft[i];
      float wet_fft_R = reverbOutputR_fft[i];

      // Mixage final
      outLeft[i] = dry_ifft + dry_fft + (wet_ifft_L * level_ifft) +
                   (wet_fft_L * level_fft);
      outRight[i] = dry_ifft + dry_fft + (wet_ifft_R * level_ifft) +
                    (wet_fft_R * level_fft);

      // Limiter pour éviter les crêtes excessives
      if (outLeft[i] > 1.0f)
        outLeft[i] = 1.0f;
      if (outLeft[i] < -1.0f)
        outLeft[i] = -1.0f;
      if (outRight[i] > 1.0f)
        outRight[i] = 1.0f;
      if (outRight[i] < -1.0f)
        outRight[i] = -1.0f;
    }

    // Appliquer l'égaliseur à trois bandes si activé au signal final
    if (gEqualizer && gEqualizer->isEnabled()) {
      float *outLeftPtr[1] = {outLeft};
      float *outRightPtr[1] = {outRight};
      gEqualizer->process(chunk, 1, outLeftPtr);
      gEqualizer->process(chunk, 1, outRightPtr);
    }

    // Avancer les pointeurs
    outLeft += chunk;
    outRight += chunk;
    readOffset += chunk;
    framesToRender -= chunk;

    // Si on a consommé tout le buffer de synth_IfftMode, on le marque comme
    // libre et on passe au suivant
    if (readOffset >= AUDIO_BUFFER_SIZE) {
      pthread_mutex_lock(&buffers_R[localReadIndex].mutex);
      buffers_R[localReadIndex].ready = 0;
      pthread_cond_signal(&buffers_R[localReadIndex].cond);
      pthread_mutex_unlock(&buffers_R[localReadIndex].mutex);
      localReadIndex = (localReadIndex == 0) ? 1 : 0;
      readOffset = 0;
    }

    // Gérer le buffer de synth_fftMode
    fft_readOffset += chunk; // Consommé 'chunk' frames de fft_buffer aussi
    if (fft_readOffset >= AUDIO_BUFFER_SIZE) {
      pthread_mutex_lock(&fft_audio_buffers[fft_localReadIndex].mutex);
      fft_audio_buffers[fft_localReadIndex].ready = 0;
      pthread_cond_signal(&fft_audio_buffers[fft_localReadIndex].cond);
      pthread_mutex_unlock(&fft_audio_buffers[fft_localReadIndex].mutex);
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
      requestedDeviceId(-1), // -1 = auto-detect, otherwise use specific device
      masterVolume(1.0f), reverbBuffer(nullptr), reverbMix(0.5f),
      reverbRoomSize(0.95f), reverbDamping(0.4f), reverbWidth(1.0f),
      reverbEnabled(true) {

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

  // Options pour optimiser la stabilité sur Jetson Nano
  RtAudio::StreamOptions options;
  options.flags =
      RTAUDIO_NONINTERLEAVED; // Suppression de RTAUDIO_MINIMIZE_LATENCY
  options.numberOfBuffers =
      4; // Augmentation du nombre de buffers pour plus de stabilité

  // Ouvrir le flux audio avec les options de faible latence
  try {
    audio->openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);
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

  try {
    audio->startStream();
  } catch (std::exception &e) {
    std::cerr << "Erreur démarrage RtAudio: " << e.what() << std::endl;
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

  // Options pour optimiser la stabilité sur Jetson Nano
  RtAudio::StreamOptions options;
  options.flags =
      RTAUDIO_NONINTERLEAVED; // Suppression de RTAUDIO_MINIMIZE_LATENCY
  options.numberOfBuffers =
      4; // Augmentation du nombre de buffers pour plus de stabilité

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

} // extern "C"
