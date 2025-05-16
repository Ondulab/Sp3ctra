/* audio_rtaudio.cpp */

#include "audio_rtaudio.h"
#include "audio_c_api.h"
#include <cstring>
#include <iostream>

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
  auto *audioSystem = static_cast<AudioSystem *>(userData);
  return audioSystem->handleCallback(static_cast<float *>(outputBuffer),
                                     nFrames);
}

int AudioSystem::handleCallback(float *outputBuffer, unsigned int nFrames) {
  // Configuration stéréo non-entrelacée (comme RtAudio par défaut)
  float *outLeft = outputBuffer;
  float *outRight = outputBuffer + nFrames;

  // Variables statiques pour maintenir l'état entre les appels
  static unsigned int readOffset =
      0; // Combien de frames ont été consommées dans le buffer actuel
  static int localReadIndex = 0; // Quel buffer on lit actuellement (indépendant
                                 // de current_buffer_index)

  // Buffer temporaire pour appliquer le volume
  static float tempBuffer[AUDIO_BUFFER_SIZE];

  unsigned int framesToRender = nFrames;

  // Traitement des frames demandées, potentiellement en utilisant plusieurs
  // buffers
  while (framesToRender > 0) {
    // Si le buffer actuel n'est pas prêt = underrun
    if (buffers_R[localReadIndex].ready != 1) {
      // On génère du silence uniquement pour le reste manquant, pas le buffer
      // entier
      memset(outLeft, 0, framesToRender * sizeof(float));
      memset(outRight, 0, framesToRender * sizeof(float));
      break; // On sort, mais sans erreur - juste du silence pour cette partie
    }

    // Combien de frames reste-t-il dans le buffer actuel?
    unsigned int framesAvailable = AUDIO_BUFFER_SIZE - readOffset;

    // On consomme soit tout ce qui reste, soit ce dont on a besoin
    unsigned int chunk =
        (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

    // Copier les données dans un buffer temporaire et appliquer le volume
    memcpy(tempBuffer, &buffers_R[localReadIndex].data[readOffset],
           chunk * sizeof(float));

    // Appliquer le volume master (avec log pour le débogage)
    static float lastLoggedVolume = -1.0f;
    float currentVolume = this->masterVolume;

    // Log le volume seulement s'il a changé significativement
    if (fabs(currentVolume - lastLoggedVolume) > 0.01f) {
      std::cout << "AUDIO: Applying volume: " << currentVolume << std::endl;
      lastLoggedVolume = currentVolume;
    }

    // Amplification avec le volume courant
    for (unsigned int i = 0; i < chunk; i++) {
      tempBuffer[i] *= currentVolume;
    }

    // Appliquer la réverbération si elle est activée
    for (unsigned int i = 0; i < chunk; i++) {
      float inputL = tempBuffer[i];
      float inputR =
          tempBuffer[i]; // Même signal pour gauche et droite en entrée
      float outputL, outputR;

      // Traiter la réverbération
      processReverb(inputL, inputR, outputL, outputR);

      // Écrire le résultat dans les buffers de sortie
      outLeft[i] = outputL;
      outRight[i] = outputR;
    }

    // Avancer les pointeurs
    outLeft += chunk;
    outRight += chunk;
    readOffset += chunk;
    framesToRender -= chunk;

    // Si on a consommé tout le buffer, on le marque comme libre et on passe au
    // suivant
    if (readOffset >= AUDIO_BUFFER_SIZE) {
      // Terminé avec ce buffer
      pthread_mutex_lock(&buffers_R[localReadIndex].mutex);
      buffers_R[localReadIndex].ready = 0;
      pthread_cond_signal(&buffers_R[localReadIndex].cond);
      pthread_mutex_unlock(&buffers_R[localReadIndex].mutex);

      // Passage au buffer suivant
      localReadIndex = (localReadIndex == 0) ? 1 : 0;
      readOffset = 0;
    }
  }

  return 0;
}

// Constructeur
AudioSystem::AudioSystem(unsigned int sampleRate, unsigned int bufferSize,
                         unsigned int channels)
    : audio(nullptr), sampleRate(sampleRate), bufferSize(bufferSize),
      channels(channels), isRunning(false), masterVolume(1.0f),
      reverbBuffer(nullptr), reverbWriteIndex(0), reverbMix(0.33f),
      reverbRoomSize(0.5f), reverbDamping(0.5f), reverbWidth(1.0f),
      reverbEnabled(true) {

  std::cout << "\033[1;32m[ZitaRev1] Réverbération activée par défaut avec "
               "l'algorithme Zita-Rev1\033[0m"
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

  // Configuration de ZitaRev1 avec des valeurs optimales
  zitaRev.init(sampleRate);
  zitaRev.set_roomsize(0.75f); // Grande taille de pièce (équivalent à SIZE)
  zitaRev.set_damping(0.7f);   // Amortissement des hautes fréquences modéré
  zitaRev.set_width(1.0f);     // Largeur stéréo maximale
  zitaRev.set_delay(
      0.05f);            // Léger pre-delay pour clarté (équivalent à PREDELAY)
  zitaRev.set_mix(0.8f); // 80% wet (ajustable via reverbMix)
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
  // Utiliser une courbe exponentielle pour le mixage pour un meilleur contrôle
  float wetGain =
      reverbMix * reverbMix *
      3.0f; // Amplification exponentielle pour rendre l'effet très audible

  // Conserver une partie du signal sec même quand le mix est au maximum (plus
  // musical)
  float dryGain = 1.0f - reverbMix * 0.7f;

  // Mélanger les signaux
  outputL = inputL * dryGain + outBufferL[0] * wetGain;
  outputR = inputR * dryGain + outBufferR[0] * wetGain;
}

// Initialisation
bool AudioSystem::initialize() {
  audio = new RtAudio();

  // Vérifier si RtAudio a été correctement créé
  if (!audio) {
    std::cerr << "Impossible de créer l'instance RtAudio" << std::endl;
    return false;
  }

  // Paramètres du stream
  RtAudio::StreamParameters params;
  params.deviceId = audio->getDefaultOutputDevice();
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
    std::cerr << "Erreur RtAudio: " << e.what() << std::endl;
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
    return gAudioSystem->setDevice(deviceId) ? 1 : 0;
  }
  return 0;
}

} // extern "C"
