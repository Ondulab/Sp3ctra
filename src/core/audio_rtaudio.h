/* audio_rtaudio.h */

#ifndef AUDIO_RTAUDIO_H
#define AUDIO_RTAUDIO_H

#include "config.h"
#include <atomic>
#include <mutex>
#include <rtaudio/RtAudio.h>
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

  // Callback static (pour faire le lien avec l'instance)
  static int rtCallback(void *outputBuffer, void *inputBuffer,
                        unsigned int nFrames, double streamTime,
                        RtAudioStreamStatus status, void *userData);

  // Callback de l'instance
  int handleCallback(float *outputBuffer, unsigned int nFrames);

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

  // Accesseur pour l'objet RtAudio (à utiliser avec précaution)
  RtAudio *getAudioDevice() const { return audio; }

  // Paramètres de latence
  bool setBufferSize(unsigned int size);
  unsigned int getBufferSize() const;
};

// Fonction globale pour la rétrocompatibilité minimale
extern AudioSystem *gAudioSystem;

// Ces fonctions peuvent être conservées pour la compatibilité
extern "C" {
void audio_Init();
void audio_Cleanup();
int startAudioUnit();
void stopAudioUnit();
}

#endif // AUDIO_RTAUDIO_H
