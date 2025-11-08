/* audio_c_api.h - Interface C pour RtAudio */

#ifndef audio_h
#define audio_h

#include "config.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

// Types compatibles avec CoreAudio
typedef float Float32;
typedef uint32_t UInt32;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioData {
  Float32 **buffers;
  UInt32 numChannels;
  UInt32 bufferSize;
} AudioData;

typedef struct {
  float *data; // dynamically allocated with size = g_sp3ctra_config.audio_buffer_size
  int ready; // 0: libre, 1: rempli et en attente de lecture
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} AudioDataBuffers;

extern AudioDataBuffers buffers_L[2];
extern AudioDataBuffers buffers_R[2];

extern volatile int current_buffer_index;
extern pthread_mutex_t buffer_index_mutex;

// Fonctions C pour la compatibilité
void resetAudioDataBufferOffset(void);
void initAudioData(AudioData *audioData, UInt32 numChannels, UInt32 bufferSize);
void audio_Init(void);
void cleanupAudioData(AudioData *audioData);
void audio_Cleanup(void);
int startAudioUnit(void);
void stopAudioUnit(void);
void printAudioDevices(void);
int setAudioDevice(unsigned int deviceId);
void setRequestedAudioDevice(int deviceId);
void setRequestedAudioDeviceName(const char* deviceName);

// Control minimal callback mode for debugging audio dropouts
void setMinimalCallbackMode(int enabled);
void setMinimalTestVolume(float volume);

// Control synth mix levels (thread-safe)
void setSynthAdditiveMixLevel(float level);  // 0.0 - 1.0
void setSynthPolyphonicMixLevel(float level); // 0.0 - 1.0
void setSynthPhotowaveMixLevel(float level);  // 0.0 - 1.0
float getSynthAdditiveMixLevel(void);
float getSynthPolyphonicMixLevel(void);
float getSynthPhotowaveMixLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* audio_h */
