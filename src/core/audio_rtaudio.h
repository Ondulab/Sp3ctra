/* audio_rtaudio.h */

#ifndef AUDIO_RTAUDIO_H
#define AUDIO_RTAUDIO_H

#include "ZitaRev1.h"
#include "audio_c_api.h"
#include "config.h"
#include "three_band_eq.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <rtaudio/RtAudio.h>
#include <thread>
#include <vector>

class AudioSystem {
private:
  RtAudio *audio;
  std::vector<float> processBuffer;
  std::mutex bufferMutex;
  std::atomic<bool> isRunning;

  // Paramètres audio
  unsigned int sampleRate;
  unsigned int bufferSize;
  unsigned int channels;
  int requestedDeviceId; // Device ID specifically requested by user (-1 = auto)

  // Callback static (pour faire le lien avec l'instance)
  static int rtCallback(void *outputBuffer, void *inputBuffer,
                        unsigned int nFrames, double streamTime,
                        RtAudioStreamStatus status, void *userData);

  // Callback de l'instance
  int handleCallback(float *outputBuffer, unsigned int nFrames);

  // Volume control
  float masterVolume;

  // Paramètres de réverbération
  static const int REVERB_BUFFER_SIZE = 32768; // Pour compatibilité arrière
  float *reverbBuffer;                         // Pour compatibilité arrière
  // int reverbWriteIndex;                        // Pour compatibilité arrière
  // - Unused
  int reverbDelays[8]; // Pour compatibilité arrière

  // Nouvelle implémentation basée sur ZitaRev1
  ZitaRev1
      zitaRev; // Instance de ZitaRev1 pour une réverbération de haute qualité

  float reverbMix;      // Dry/Wet mix (0.0 - 1.0)
  float reverbRoomSize; // Taille de la pièce (0.0 - 1.0)
  float reverbDamping;  // Amortissement (0.0 - 1.0)
  float reverbWidth;    // Largeur stéréo (0.0 - 1.0)
  bool reverbEnabled;   // Activation/désactivation de la réverbération

  // === MULTI-THREADED REVERB SYSTEM ===
  static const int REVERB_THREAD_BUFFER_SIZE =
      8192; // Buffer circulaire pour thread reverb

  // Buffer circulaire pour données d'entrée de la réverbération (thread-safe)
  struct ReverbInputBuffer {
    float data[REVERB_THREAD_BUFFER_SIZE];
    std::atomic<int> write_pos;
    std::atomic<int> read_pos;
    std::atomic<int> available_samples;

    ReverbInputBuffer() : write_pos(0), read_pos(0), available_samples(0) {
      std::fill(data, data + REVERB_THREAD_BUFFER_SIZE, 0.0f);
    }
  } reverbInputBuffer;

  // Buffer circulaire pour données de sortie de la réverbération (thread-safe)
  struct ReverbOutputBuffer {
    float left[REVERB_THREAD_BUFFER_SIZE];
    float right[REVERB_THREAD_BUFFER_SIZE];
    std::atomic<int> write_pos;
    std::atomic<int> read_pos;
    std::atomic<int> available_samples;

    ReverbOutputBuffer() : write_pos(0), read_pos(0), available_samples(0) {
      std::fill(left, left + REVERB_THREAD_BUFFER_SIZE, 0.0f);
      std::fill(right, right + REVERB_THREAD_BUFFER_SIZE, 0.0f);
    }
  } reverbOutputBuffer;

  // Thread de traitement de la réverbération
  std::thread reverbThread;
  std::atomic<bool> reverbThreadRunning;
  std::condition_variable reverbCondition;
  std::mutex reverbMutex;

  // Fonction principale du thread de réverbération
  void reverbThreadFunction();

  // Fonctions pour les buffers circulaires thread-safe
  bool writeToReverbInput(float sample);
  bool readFromReverbInput(float &sample);
  bool writeToReverbOutput(float sampleL, float sampleR);
  bool readFromReverbOutput(float &sampleL, float &sampleR);

  // Fonction de traitement de la réverbération (legacy)
  void processReverb(float inputL, float inputR, float &outputL,
                     float &outputR);

  // Fonction de traitement de la réverbération optimisée pour callback
  void processReverbOptimized(float inputL, float inputR, float &outputL,
                              float &outputR);

public:
  AudioSystem(unsigned int sampleRate = SAMPLING_FREQUENCY,
              unsigned int bufferSize = AUDIO_BUFFER_SIZE,
              unsigned int channels = AUDIO_CHANNEL);
  ~AudioSystem();

  // API principale
  bool initialize();
  bool start();
  void stop();
  bool isActive() const;

  // Fonctions pour interagir avec le système audio
  bool setAudioData(const float *data, size_t size);

  // Informations sur le système audio
  std::vector<std::string> getAvailableDevices();
  bool setDevice(unsigned int deviceId);
  unsigned int getCurrentDevice() const;
  void setRequestedDeviceId(int deviceId);

  // Accesseur pour l'objet RtAudio (à utiliser avec précaution)
  RtAudio *getAudioDevice() const { return audio; }

  // Volume control
  void setMasterVolume(float volume);
  float getMasterVolume() const;

  // Paramètres de latence
  bool setBufferSize(unsigned int size);
  unsigned int getBufferSize() const;

  // Contrôle de réverbération
  void enableReverb(bool enable);
  bool isReverbEnabled() const;
  void setReverbMix(float mix);
  float getReverbMix() const;
  void setReverbRoomSize(float size);
  float getReverbRoomSize() const;
  void setReverbDamping(float damping);
  float getReverbDamping() const;
  void setReverbWidth(float width);
  float getReverbWidth() const;
};

// Fonction globale pour la rétrocompatibilité minimale
extern AudioSystem *gAudioSystem;

// Ces fonctions peuvent être conservées pour la compatibilité
extern "C" {
void audio_Init(void);
void audio_Cleanup();
int startAudioUnit();
void stopAudioUnit();
}

#endif // AUDIO_RTAUDIO_H
