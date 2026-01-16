#pragma once

#include <memory>
#include <atomic>
#include <string>
#include <mutex>

// Forward declarations for C structures
extern "C" {
    struct DoubleBuffer;
    struct AudioImageBuffers;
    struct sockaddr_in;
    
    // Include context.h here to avoid incomplete type issues
    #include "../../src/core/context.h"
}

/**
 * @brief Core Sp3ctra state encapsulation for VST
 * 
 * This class encapsulates all the global state from the standalone application
 * to allow multiple VST instances to coexist without conflicts.
 * 
 * Configuration is now managed by APVTS (AudioProcessorValueTreeState) in PluginProcessor.
 * No more .ini file loading - all settings are saved in DAW projects.
 * 
 * Thread Safety:
 * - Constructor/Destructor: Main thread only
 * - initialize()/shutdown(): Main thread only
 * - UDP thread: Reads/writes via Context pointers
 * - Audio thread: Will read DoubleBuffer (lock-free in future)
 */
class Sp3ctraCore {
public:
    /**
     * @brief Configuration structure (provided by APVTS)
     */
    struct ActiveConfig {
        std::atomic<int> udpPort{55151};
        std::string udpAddress = "239.100.100.100";  // Protected by configMutex
        std::string multicastInterface = "";         // Protected by configMutex
        std::atomic<int> logLevel{2};
        
        ActiveConfig() = default;
        
        // Custom copy constructor (atomics are not copyable)
        ActiveConfig(const ActiveConfig& other) 
            : udpPort(other.udpPort.load()),
              udpAddress(other.udpAddress),
              multicastInterface(other.multicastInterface),
              logLevel(other.logLevel.load()) {}
        
        // Custom copy assignment
        ActiveConfig& operator=(const ActiveConfig& other) {
            if (this != &other) {
                udpPort.store(other.udpPort.load());
                udpAddress = other.udpAddress;
                multicastInterface = other.multicastInterface;
                logLevel.store(other.logLevel.load());
            }
            return *this;
        }
    };
    
    Sp3ctraCore();
    ~Sp3ctraCore();
    
    // Non-copyable, non-movable (owns unique resources)
    Sp3ctraCore(const Sp3ctraCore&) = delete;
    Sp3ctraCore& operator=(const Sp3ctraCore&) = delete;
    Sp3ctraCore(Sp3ctraCore&&) = delete;
    Sp3ctraCore& operator=(Sp3ctraCore&&) = delete;
    
    /**
     * @brief Initialize UDP and buffers with given configuration (from APVTS)
     * @param config Configuration to apply
     * @return true on success, false on failure
     */
    bool initialize(const ActiveConfig& config);
    
    /**
     * @brief Apply new configuration (hot-reload)
     * @param config New configuration to apply
     * @return true on success, false on failure
     * @note This will restart the UDP socket if port/address changed
     */
    bool applyConfig(const ActiveConfig& config);
    
    /**
     * @brief Shutdown UDP and cleanup resources
     */
    void shutdown();
    
    /**
     * @brief Check if UDP is running
     */
    bool isUdpRunning() const { return udpRunning.load(); }
    
    /**
     * @brief Check if core is initialized
     */
    bool isInitialized() const { return initialized.load(); }
    
    // Thread-safe accessors for UDP thread
    Context* getContext() { return context.get(); }
    DoubleBuffer* getDoubleBuffer() { return doubleBuffer.get(); }
    AudioImageBuffers* getAudioImageBuffers() { return audioImageBuffers.get(); }
    
    // Configuration accessor (thread-safe)
    ActiveConfig getActiveConfig() const {
        std::lock_guard<std::mutex> lock(configMutex);
        return active;
    }
    
    int getSocketFd() const { return socketFd.load(); }

private:
    // Configuration (provided by APVTS, no defaults needed)
    ActiveConfig active;
    mutable std::mutex configMutex;
    
    // Core state (C structures wrapped in unique_ptr)
    std::unique_ptr<Context> context;
    std::unique_ptr<DoubleBuffer> doubleBuffer;
    std::unique_ptr<AudioImageBuffers> audioImageBuffers;
    std::unique_ptr<sockaddr_in> si_me;
    std::unique_ptr<sockaddr_in> si_other;
    
    // UDP state
    std::atomic<int> socketFd{-1};
    std::atomic<bool> udpRunning{false};
    std::atomic<bool> initialized{false};
    
    // Internal helpers
    bool initializeUdp(int port, const std::string& address, const std::string& interface);
    void shutdownUdp();
    bool initializeBuffers();
    void shutdownBuffers();
};
