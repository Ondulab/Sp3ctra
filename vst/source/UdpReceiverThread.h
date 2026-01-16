#pragma once

#include <juce_core/juce_core.h>
#include "Sp3ctraCore.h"

// Forward declaration of C function
extern "C" {
    void* udpThread(void* arg);
    #include "../../src/core/context.h"
}

/**
 * @brief JUCE thread wrapper for UDP reception
 * 
 * This class wraps the existing C udpThread() function from multithreading.c
 * into a JUCE Thread for clean integration with the VST plugin lifecycle.
 * 
 * Thread Safety:
 * - run() executes on separate thread
 * - Uses Context->running flag for clean shutdown
 * - JUCE handles thread lifecycle automatically
 */
class UdpReceiverThread : public juce::Thread {
public:
    /**
     * @brief Constructor
     * @param core Pointer to Sp3ctraCore (must remain valid for thread lifetime)
     */
    explicit UdpReceiverThread(Sp3ctraCore* core)
        : Thread("Sp3ctraUDP"), core(core) {
        juce::Logger::writeToLog("UdpReceiverThread: Constructor called");
    }
    
    /**
     * @brief Destructor
     * @note Automatically stops thread if still running
     */
    ~UdpReceiverThread() override {
        juce::Logger::writeToLog("UdpReceiverThread: Destructor called");
        
        // Ensure thread is stopped (JUCE best practice)
        if (isThreadRunning()) {
            requestStop();
            stopThread(2000);  // 2 second timeout
        }
    }
    
    /**
     * @brief Thread execution function
     * 
     * Calls the existing C udpThread() function which handles:
     * - Packet reception (IMAGE_DATA, IMU)
     * - Buffer updates (DoubleBuffer, AudioImageBuffers)
     * - Context->running flag for shutdown
     */
    void run() override {
        juce::Logger::writeToLog("UdpReceiverThread: Thread starting...");
        
        if (!core) {
            juce::Logger::writeToLog("UdpReceiverThread: ERROR - core is null!");
            return;
        }
        
        Context* ctx = core->getContext();
        if (!ctx) {
            juce::Logger::writeToLog("UdpReceiverThread: ERROR - Context is null!");
            return;
        }
        
        // Set running flag
        ctx->running = 1;
        
        juce::Logger::writeToLog("UdpReceiverThread: Calling C udpThread() function...");
        
        // Call existing C function (blocks until Context->running = 0)
        udpThread((void*)ctx);
        
        juce::Logger::writeToLog("UdpReceiverThread: udpThread() returned, thread exiting");
    }
    
    /**
     * @brief Request thread stop (custom method)
     */
    void requestStop() {
        juce::Logger::writeToLog("UdpReceiverThread: Requesting thread stop");
        
        // Set Context->running = 0 to stop C udpThread loop
        if (core) {
            Context* ctx = core->getContext();
            if (ctx) {
                ctx->running = 0;
            }
        }
    }
    
private:
    Sp3ctraCore* core;  // Non-owning pointer (owned by PluginProcessor)
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UdpReceiverThread)
};
