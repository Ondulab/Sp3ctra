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

typedef enum {
  AUDIO_BUFFER_OFFSET_NONE = 0,
  AUDIO_BUFFER_OFFSET_HALF,
  AUDIO_BUFFER_OFFSET_FULL,
} BUFFER_AUDIO_StateTypeDef;

typedef struct AudioData {
  Float32 **buffers;
  UInt32 numChannels;
  UInt32 bufferSize;
} AudioData;

typedef struct {
  float data[AUDIO_BUFFER_SIZE];
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
int getAudioDataBufferOffset(void);
void setAudioDataBufferOffsetHALF(void);
void setAudioDataBufferOffsetFULL(void);
void initAudioData(AudioData *audioData, UInt32 numChannels, UInt32 bufferSize);
void audio_Init(void);
void cleanupAudioData(AudioData *audioData);
void audio_Cleanup(void);
int startAudioUnit(void);
void stopAudioUnit(void);
void printAudioDevices(void);
int setAudioDevice(unsigned int deviceId);
void setRequestedAudioDevice(int deviceId);

// Control minimal callback mode for debugging audio dropouts
void setMinimalCallbackMode(int enabled);
void setMinimalTestVolume(float volume);

#ifdef __cplusplus
}
#endif

#endif /* audio_h */
