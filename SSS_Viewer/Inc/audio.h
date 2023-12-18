//
//  audio.h
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#ifndef audio_h
#define audio_h

#include <stdio.h>
#include <AudioUnit/AudioUnit.h>

#include "config.h"

typedef enum 
{
    AUDIO_BUFFER_OFFSET_NONE = 0,
    AUDIO_BUFFER_OFFSET_HALF,
    AUDIO_BUFFER_OFFSET_FULL,
} BUFFER_AUDIO_StateTypeDef;

typedef struct AudioData
{
    Float32 **buffers;
    UInt32 numChannels;
    UInt32 bufferSize;
} AudioData;

extern float audio_samples[AUDIO_BUFFER_SIZE * 2];
extern int current_buffer_index; 
extern pthread_mutex_t buffer_index_mutex;

void resetAudioDataBufferOffset(void);
int getAudioDataBufferOffset(void);
void setAudioDataBufferOffsetHALF(void);
void setAudioDataBufferOffsetFULL(void);
void initAudioData(AudioData *audioData, UInt32 numChannels, UInt32 bufferSize);
void audio_Init(AudioData *audioData);
void cleanupAudioData(AudioData *audioData);
void audio_Cleanup(void);
OSStatus startAudioUnit(void);
void stopAudioUnit(void);

#endif /* audio_h */
