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

    // Copie du morceau de buffer vers la sortie audio
    memcpy(outLeft, &buffers_R[localReadIndex].data[readOffset],
           chunk * sizeof(float));
    memcpy(outRight, &buffers_R[localReadIndex].data[readOffset],
           chunk * sizeof(float));

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
      channels(channels), isRunning(false) {
  processBuffer.resize(bufferSize * channels);
}

// Destructeur
AudioSystem::~AudioSystem() {
  stop();
  if (audio) {
    if (audio->isStreamOpen()) {
      audio->closeStream();
    }
    delete audio;
  }
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

  // Options pour optimiser la latence
  RtAudio::StreamOptions options;
  options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;
  options.numberOfBuffers = 2; // Valeur minimale pour la latence

  // Ouvrir le flux audio avec les options de faible latence
  RtAudioErrorType error =
      audio->openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate,
                        &bufferSize, &AudioSystem::rtCallback, this, &options);

  if (error != RtAudioErrorType::RTAUDIO_NO_ERROR) {
    std::cerr << "Erreur RtAudio: " << audio->getErrorText() << std::endl;
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

  RtAudioErrorType error = audio->startStream();
  if (error != RtAudioErrorType::RTAUDIO_NO_ERROR) {
    std::cerr << "Erreur démarrage RtAudio: " << audio->getErrorText()
              << std::endl;
    return false;
  }

  isRunning = true;
  return true;
}

// Arrêt du flux audio
void AudioSystem::stop() {
  if (audio && audio->isStreamRunning()) {
    RtAudioErrorType error = audio->stopStream();
    if (error != RtAudioErrorType::RTAUDIO_NO_ERROR) {
      std::cerr << "Erreur arrêt RtAudio: " << audio->getErrorText()
                << std::endl;
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

  // Options pour optimiser la latence
  RtAudio::StreamOptions options;
  options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;
  options.numberOfBuffers = 2;

  // Ouvrir le flux audio avec les options de faible latence
  RtAudioErrorType error =
      audio->openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate,
                        &bufferSize, &AudioSystem::rtCallback, this, &options);

  if (error != RtAudioErrorType::RTAUDIO_NO_ERROR) {
    std::cerr << "Erreur changement périphérique: " << audio->getErrorText()
              << std::endl;
    return false;
  }

  if (wasRunning) {
    error = audio->startStream();
    if (error != RtAudioErrorType::RTAUDIO_NO_ERROR) {
      std::cerr << "Erreur redémarrage après changement: "
                << audio->getErrorText() << std::endl;
      return false;
    }
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

} // extern "C"
