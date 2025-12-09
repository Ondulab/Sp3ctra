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

  // Audio parameters
  unsigned int sampleRate;
  unsigned int bufferSize;
  unsigned int channels;
  int requestedDeviceId; // Device ID specifically requested by user (-1 = auto)
  
  // Multi-channel output support
  bool multiChannelOutputEnabled;  // true if 8+ channels detected and ENABLE_RAW_OUTPUTS=1
  unsigned int actualOutputChannels;  // Actual number of output channels opened

  // Callback static (pour faire le lien avec l'instance)
  static int rtCallback(void *outputBuffer, void *inputBuffer,
                        unsigned int nFrames, double streamTime,
                        RtAudioStreamStatus status, void *userData);

  // Callback de l'instance
  int handleCallback(float *outputBuffer, unsigned int nFrames);

  // Volume control
  float masterVolume;

  // Reverb parameters
  static const int REVERB_BUFFER_SIZE = 32768; // For backward compatibility
  float *reverbBuffer;                         // For backward compatibility
  // int reverbWriteIndex;                        // For backward compatibility
  // - Unused
  int reverbDelays[8]; // For backward compatibility

  // New implementation based on ZitaRev1
  ZitaRev1
      zitaRev; // ZitaRev1 instance for high-quality reverb

  float reverbMix;      // Dry/Wet mix (0.0 - 1.0)
  float reverbRoomSize; // Room size (0.0 - 1.0)
  float reverbDamping;  // Amortissement (0.0 - 1.0)
  float reverbWidth;    // Stereo width (0.0 - 1.0)
  bool reverbEnabled;   // Enable/disable reverb

  // === MULTI-THREADED REVERB SYSTEM ===
  static const int REVERB_THREAD_BUFFER_SIZE =
      8192; // Buffer circulaire pour thread reverb

  // Circular buffer for reverb input data (thread-safe)
  struct ReverbInputBuffer {
    float data[REVERB_THREAD_BUFFER_SIZE];
    std::atomic<int> write_pos;
    std::atomic<int> read_pos;
    std::atomic<int> available_samples;

    ReverbInputBuffer() : write_pos(0), read_pos(0), available_samples(0) {
      std::fill(data, data + REVERB_THREAD_BUFFER_SIZE, 0.0f);
    }
  } reverbInputBuffer;

  // Circular buffer for reverb output data (thread-safe)
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

  // Reverb processing thread
  std::thread reverbThread;
  std::atomic<bool> reverbThreadRunning;
  std::condition_variable reverbCondition;
  std::mutex reverbMutex;

  // Main reverb thread function
  void reverbThreadFunction();

  // Fonctions pour les buffers circulaires thread-safe
  bool writeToReverbInput(float sample);
  bool readFromReverbInput(float &sample);
  bool writeToReverbOutput(float sampleL, float sampleR);
  bool readFromReverbOutput(float &sampleL, float &sampleR);

  // Reverb processing function optimized for callback
  void processReverbOptimized(float inputL, float inputR, float &outputL,
                              float &outputR);

public:
  AudioSystem(unsigned int sampleRate = 48000,
              unsigned int bufferSize = 100,
              unsigned int channels = AUDIO_CHANNEL);
  ~AudioSystem();

  // API principale
  bool initialize();
  bool start();
  void stop();
  bool isActive() const;

  // Functions to interact with audio system
  bool setAudioData(const float *data, size_t size);

  // Audio system information
  std::vector<std::string> getAvailableDevices();
  bool setDevice(unsigned int deviceId);
  unsigned int getCurrentDevice() const;
  void setRequestedDeviceId(int deviceId);

  // Accessor for RtAudio object (use with caution)
  RtAudio *getAudioDevice() const { return audio; }

  // Volume control
  void setMasterVolume(float volume);
  float getMasterVolume() const;

  // Latency parameters
  bool setBufferSize(unsigned int size);
  unsigned int getBufferSize() const;

  // Reverb control
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

// Global function for minimal backward compatibility
extern AudioSystem *gAudioSystem;

// These functions can be kept for compatibility
extern "C" {
void audio_Init(void);
void audio_Cleanup();
int startAudioUnit();
void stopAudioUnit();
}

#endif // AUDIO_RTAUDIO_H
