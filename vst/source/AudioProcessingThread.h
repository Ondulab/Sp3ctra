#pragma once

#include <juce_core/juce_core.h>
#include "Sp3ctraCore.h"

// Forward declaration of C function
extern "C" {
    void* audioProcessingThread(void* arg);
    #include "../../src/core/context.h"
}

/**
 * @brief JUCE thread wrapper for Audio Processing
 * 
 * This class wraps the existing C audioProcessingThread() function from multithreading.c
 * into a JUCE Thread for clean integration with the VST plugin lifecycle.
 * 
 * CRITICAL: In standalone mode, audioProcessingThread calls synth_AudioProcess() in a loop.
 * The RtAudio callback only READS the generated audio buffers.
 * 
 * In VST mode, this thread performs the same role:
 * - Continuously calls synth_AudioProcess() to generate audio
 * - processBlock() only READS the generated buffers (no synthesis in callback!)
 * 
 * 🔧 CRITICAL FIX: This thread uses its OWN stop flag (audioThreadShouldRun)
 * instead of sharing ctx->running with UdpReceiverThread. This prevents
 * buffer size changes from killing the UDP thread!
 */
class AudioProcessingThread : public juce::Thread {
public:
    /**
     * @brief Constructor
     * @param core Pointer to Sp3ctraCore (must remain valid for thread lifetime)
     */
    explicit AudioProcessingThread(Sp3ctraCore* core)
        : Thread("Sp3ctraAudioProcessing"), core(core) {
        juce::Logger::writeToLog("AudioProcessingThread: Constructor called");
    }
    
    /**
     * @brief Destructor
     * @note Automatically stops thread if still running
     */
    ~AudioProcessingThread() override {
        juce::Logger::writeToLog("AudioProcessingThread: Destructor called");
        
        // Ensure thread is stopped (JUCE best practice)
        if (isThreadRunning()) {
            requestStop();
            stopThread(2000);  // 2 second timeout
        }
    }
    
    /**
     * @brief Thread execution function
     * 
     * Calls the existing C audioProcessingThread() function which handles:
     * - Continuous synth_AudioProcess() calls
     * - Audio buffer generation
     * - Context->running flag for shutdown
     */
    void run() override {
        juce::Logger::writeToLog("AudioProcessingThread: Thread starting...");
        
        if (!core) {
            juce::Logger::writeToLog("AudioProcessingThread: ERROR - core is null!");
            return;
        }
        
        Context* ctx = core->getContext();
        if (!ctx) {
            juce::Logger::writeToLog("AudioProcessingThread: ERROR - Context is null!");
            return;
        }
        
        // 🔧 CRITICAL FIX: Set audio_thread_running, NOT running!
        // This allows stopping ONLY the audio thread during buffer size changes
        // without killing the UDP thread (which uses ctx->running)
        ctx->audio_thread_running = 1;
        
        juce::Logger::writeToLog("AudioProcessingThread: Calling C audioProcessingThread() function...");
        
        // Call existing C function (blocks until Context->audio_thread_running = 0)
        audioProcessingThread((void*)ctx);
        
        juce::Logger::writeToLog("AudioProcessingThread: audioProcessingThread() returned, thread exiting");
    }
    
    /**
     * @brief Request thread stop (custom method)
     */
    void requestStop() {
        juce::Logger::writeToLog("AudioProcessingThread: Requesting thread stop");
        
        // 🔧 CRITICAL FIX: Set audio_thread_running = 0, NOT running!
        // This stops ONLY the audio thread, UDP thread keeps running
        if (core) {
            Context* ctx = core->getContext();
            if (ctx) {
                ctx->audio_thread_running = 0;
            }
        }
    }
    
private:
    Sp3ctraCore* core;  // Non-owning pointer (owned by PluginProcessor)
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioProcessingThread)
};
