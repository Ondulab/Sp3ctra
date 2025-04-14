//
//  audio.c
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//
#include <stdio.h>
#include <stdlib.h>

#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>

#include "config.h"
#include "audio.h"

pthread_mutex_t buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

AudioDataBuffers buffers_R[2];
AudioDataBuffers buffers_L[2];
volatile int current_buffer_index = 0;

static UInt32 readOffset = 0;      // How many frames have been consumed so far in the current buffer
static int localReadIndex = 0;     // Which of the two buffers we're currently reading

//static BUFFER_AUDIO_StateTypeDef bufferAudioState = AUDIO_BUFFER_OFFSET_HALF;

static AudioUnit audioUnit; // Déclaration comme variable statique

static inline float generateWhiteNoiseSample(void);

// Générer un échantillon aléatoire pour le bruit blanc
static inline float generateWhiteNoiseSample(void)
{
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

OSStatus audioCallback(void *inRefCon,
                       AudioUnitRenderActionFlags *ioActionFlags,
                       const AudioTimeStamp *inTimeStamp,
                       UInt32 inBusNumber,
                       UInt32 inNumberFrames,
                       AudioBufferList *ioData)
{
    Float32 *outLeft  = (Float32 *)ioData->mBuffers[0].mData;
    Float32 *outRight = (Float32 *)ioData->mBuffers[1].mData;

    UInt32 framesToRender = inNumberFrames;
    while (framesToRender > 0)
    {
        // If the current buffer isn't ready, we have an under-run situation;
        // fill the remainder with silence
        if (buffers_R[localReadIndex].ready != 1)
        {
            memset(outLeft,  0, framesToRender * sizeof(Float32));
            memset(outRight, 0, framesToRender * sizeof(Float32));
            // We won't break the audio, just fill with silence.
            // In a real system, you might break out or handle differently
            break;
        }

        // How many frames are left in the current buffer?
        UInt32 framesAvailable = AUDIO_BUFFER_SIZE - readOffset;

        // We either consume all that’s left in the buffer, or only as many as we still need
        UInt32 chunk = (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

        // Copy the chunk from our buffer to the output
        // (Left channel only, Right channel set to silence)
        memcpy(outLeft, &buffers_R[localReadIndex].data[readOffset], chunk * sizeof(Float32));
        memcpy(outRight, &buffers_R[localReadIndex].data[readOffset], chunk * sizeof(Float32));

        // Advance pointers
        outLeft  += chunk;
        outRight += chunk;
        readOffset += chunk;
        framesToRender -= chunk;

        // If we've just consumed the entire buffer, mark it free and flip
        if (readOffset >= AUDIO_BUFFER_SIZE)
        {
            // Done with this buffer
            pthread_mutex_lock(&buffers_R[localReadIndex].mutex);
            buffers_R[localReadIndex].ready = 0;
            pthread_cond_signal(&buffers_R[localReadIndex].cond);
            pthread_mutex_unlock(&buffers_R[localReadIndex].mutex);

            // Flip to the other buffer
            localReadIndex = (localReadIndex == 0) ? 1 : 0;
            readOffset = 0;
        }
    }

    return noErr;
}

#if 0
void resetAudioDataBufferOffset(void)
{
    bufferAudioState = AUDIO_BUFFER_OFFSET_NONE;
}

int getAudioDataBufferOffset(void)
{
    return bufferAudioState;
}
#endif

void initAudioData(AudioData *audioData, UInt32 numChannels, UInt32 bufferSize) 
{
    audioData->numChannels = numChannels;
    audioData->bufferSize = bufferSize;
    audioData->buffers = (Float32 **)malloc(numChannels * sizeof(Float32 *));

    for (UInt32 i = 0; i < numChannels; i++) {
        audioData->buffers[i] = (Float32 *)calloc(bufferSize, sizeof(Float32));
    }
}

// Initialisation de l'audio
void audio_Init(AudioData *audioData)
{
    OSStatus status;

    // Configuration de l'Audio Unit
    AudioComponentDescription desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };

    // Trouver l'Audio Unit
    AudioComponent output = AudioComponentFindNext(NULL, &desc);
    status = AudioComponentInstanceNew(output, &audioUnit);

    AudioStreamBasicDescription audioFormat = {
        .mSampleRate       = SAMPLING_FREQUENCY,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsFloat
                              | kAudioFormatFlagIsPacked
                              | kAudioFormatFlagIsNonInterleaved,
        .mFramesPerPacket  = 1,
        .mChannelsPerFrame = AUDIO_CHANNEL,     //2
        .mBytesPerFrame    = sizeof(Float32),   // par canal en float
        .mBitsPerChannel   = 32,
        .mBytesPerPacket   = sizeof(Float32)    // mBytesPerFrame * mFramesPerPacket
    };

    // Appliquer le format audio
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &audioFormat,
                                  sizeof(audioFormat));

    // Définir la fonction de callback
    AURenderCallbackStruct callbackStruct = {
        .inputProc = audioCallback,
        .inputProcRefCon = audioData
    };
    
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Global,
                                  0,
                                  &callbackStruct,
                                  sizeof(callbackStruct));
    

    // Initialiser l'Audio Unit
    status = AudioUnitInitialize(audioUnit);
}

void cleanupAudioData(AudioData *audioData) 
{
    for (UInt32 i = 0; i < audioData->numChannels; i++) {
        free(audioData->buffers[i]);
    }
    free(audioData->buffers);
}

// Fonction de nettoyage
void audio_Cleanup(void)
{
    AudioOutputUnitStop(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
}

OSStatus startAudioUnit(void)
{
    return AudioOutputUnitStart(audioUnit);
}

void stopAudioUnit(void) 
{
    AudioOutputUnitStop(audioUnit);
}

