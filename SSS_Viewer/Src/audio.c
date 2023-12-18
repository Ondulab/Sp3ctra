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

float audio_samples[AUDIO_BUFFER_SIZE * 2];

static BUFFER_AUDIO_StateTypeDef bufferAudioState = AUDIO_BUFFER_OFFSET_HALF;
int current_buffer_index = 0; // 0 pour la première moitié, 1 pour la seconde


static AudioUnit audioUnit; // Déclaration comme variable statique

static inline float generateWhiteNoiseSample(void);

// Générer un échantillon aléatoire pour le bruit blanc
static inline float generateWhiteNoiseSample(void)
{
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

#define TWO_PI (3.14159 * 2)

OSStatus audioCallbacktest(void *inRefCon,
                       AudioUnitRenderActionFlags *ioActionFlags,
                       const AudioTimeStamp *inTimeStamp,
                       UInt32 inBusNumber,
                       UInt32 inNumberFrames,
                       AudioBufferList *ioData)
{
    //pthread_mutex_lock(&buffer_index_mutex);
    
    AudioData *audioData = (AudioData *)inRefCon;
    
    static float phase = 0.0f; // Phase actuelle de la sinusoïde
    static const float frequency = 475.0f; // Fréquence de la sinusoïde, par exemple 440 Hz pour la note A

    for (UInt32 frame = 0; frame < inNumberFrames; ++frame)
    {
        // Générer l'échantillon de la sinusoïde
        float sample = sinf(phase);
        
        // Calculer la phase suivante
        phase += (TWO_PI * frequency) / SAMPLING_FREQUENCY; // 'sampleRate' devrait être la fréquence d'échantillonnage de votre système
        if (phase >= TWO_PI) phase -= TWO_PI;

        for (UInt32 channel = 0; channel < audioData->numChannels; ++channel)
        {
            // Ici, copiez l'échantillon sinusoïdal dans chaque canal
            if (frame < audioData->bufferSize)
            {
                    audioData->buffers[channel][frame] = sample;
            }
        }
    }

    // Copier les données dans ioData
    for (UInt32 channel = 0; channel < audioData->numChannels; ++channel)
    {
        if (ioData->mBuffers[channel].mData == NULL) {
            // Gestion d'erreur ou log
            return -1;
        }
        memcpy(ioData->mBuffers[channel].mData, audioData->buffers[channel], inNumberFrames * sizeof(Float32));
    }
    
    //pthread_mutex_unlock(&buffer_index_mutex);
    return noErr;
}


OSStatus audioCallback(void *inRefCon,
                       AudioUnitRenderActionFlags *ioActionFlags,
                       const AudioTimeStamp *inTimeStamp,
                       UInt32 inBusNumber,
                       UInt32 inNumberFrames,
                       AudioBufferList *ioData)
{
    static int count = 0;
    count++;
    
    float *playback_buffer = audio_samples + ((current_buffer_index == 0) ? AUDIO_BUFFER_SIZE : 0);
    
    AudioData *audioData = (AudioData *)inRefCon;

    if(0)
    {
        for (UInt32 frame = 0; frame < inNumberFrames; ++frame)
        {
            //for testing float sample = generateWhiteNoiseSample();
            for (UInt32 channel = 0; channel < audioData->numChannels; ++channel)
            {
                if (frame < audioData->bufferSize)
                {
                    audioData->buffers[channel][frame] = playback_buffer[frame];
                }
            }
        }
    }

    // Copier les données dans ioData
    for (UInt32 channel = 0; channel < audioData->numChannels; ++channel) 
    {
        if (ioData->mBuffers[channel].mData == NULL) {
            // Gestion d'erreur ou log
            return -1;
        }
        //memcpy(ioData->mBuffers[channel].mData, audioData->buffers[channel], inNumberFrames * sizeof(Float32));
        memcpy(ioData->mBuffers[channel].mData, playback_buffer, inNumberFrames * sizeof(Float32));
    }
    
    pthread_mutex_unlock(&buffer_index_mutex);
    return noErr;
}

void resetAudioDataBufferOffset(void)
{
    bufferAudioState = AUDIO_BUFFER_OFFSET_NONE;
}

int getAudioDataBufferOffset(void)
{
    return bufferAudioState;
}

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

    // Définir le format audio
    AudioStreamBasicDescription audioFormat = {
        .mSampleRate = SAMPLING_FREQUENCY,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
        .mFramesPerPacket = 2,
        .mChannelsPerFrame = AUDIO_CHANNEL,
        .mBitsPerChannel = 32,
        .mBytesPerPacket = 2 * AUDIO_CHANNEL,
        .mBytesPerFrame = 2 * AUDIO_CHANNEL
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

