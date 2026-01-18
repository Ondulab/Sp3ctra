#include "Sp3ctraCore.h"
#include <juce_core/juce_core.h>

// C includes with extern "C" block
extern "C" {
    #include "../../src/core/context.h"
    #include "../../src/audio/buffers/doublebuffer.h"
    #include "../../src/audio/buffers/audio_image_buffers.h"
    #include "../../src/communication/network/udp.h"
    #include "../../src/config/config_loader.h"
    #include "../../src/utils/logger.h"
    #include "../../src/threading/multithreading.h"
    #include "../../src/processing/image_preprocessor.h"
    #include "luxstral/synth_luxstral_state.h"
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
}

Sp3ctraCore::Sp3ctraCore() {
    juce::Logger::writeToLog("Sp3ctraCore: Constructor called");
}

Sp3ctraCore::~Sp3ctraCore() {
    juce::Logger::writeToLog("Sp3ctraCore: Destructor called");
    shutdown();
}

bool Sp3ctraCore::initialize(const ActiveConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex);
    
    if (initialized.load()) {
        juce::Logger::writeToLog("Sp3ctraCore: Already initialized, shutting down first");
        shutdownUdp();
        shutdownBuffers();
    }
    
    juce::Logger::writeToLog("Sp3ctraCore: Initializing...");
    
    // Store active configuration
    active = config;
    
    // Initialize logger with configured level
    logger_init((log_level_t)config.logLevel.load());
    
    // Initialize buffers first
    if (!initializeBuffers()) {
        juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to initialize buffers");
        return false;
    }
    
    // Initialize UDP
    if (!initializeUdp(config.udpPort.load(), config.udpAddress, config.multicastInterface)) {
        juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to initialize UDP");
        shutdownBuffers();
        return false;
    }
    
    initialized.store(true);
    juce::Logger::writeToLog("Sp3ctraCore: Initialization complete");
    
    return true;
}

bool Sp3ctraCore::applyConfig(const ActiveConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex);
    
    // Check if UDP parameters changed
    bool udpChanged = 
        (config.udpPort.load() != active.udpPort.load()) ||
        (config.udpAddress != active.udpAddress) ||
        (config.multicastInterface != active.multicastInterface);
    
    if (udpChanged) {
        juce::Logger::writeToLog("Sp3ctraCore: UDP config changed, restarting socket...");
        
        // Restart UDP with new config
        shutdownUdp();
        
        if (!initializeUdp(config.udpPort.load(), config.udpAddress, config.multicastInterface)) {
            juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to restart UDP");
            return false;
        }
    }
    
    // Update log level if changed
    if (config.logLevel.load() != active.logLevel.load()) {
        logger_init((log_level_t)config.logLevel.load());
        juce::Logger::writeToLog(juce::String::formatted(
            "Sp3ctraCore: Log level changed to %d", config.logLevel.load()
        ));
    }
    
    // Store new active config
    active = config;
    
    return true;
}

void Sp3ctraCore::shutdown() {
    std::lock_guard<std::mutex> lock(configMutex);
    
    if (!initialized.load()) {
        return;
    }
    
    juce::Logger::writeToLog("Sp3ctraCore: Shutting down...");
    
    shutdownUdp();
    shutdownBuffers();
    
    initialized.store(false);
    juce::Logger::writeToLog("Sp3ctraCore: Shutdown complete");
}

bool Sp3ctraCore::initializeBuffers() {
    try {
        // Allocate Context
        context = std::make_unique<Context>();
        memset(context.get(), 0, sizeof(Context));
        
        // Initialize IMU mutex
        if (pthread_mutex_init(&context->imu_mutex, nullptr) != 0) {
            juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to init IMU mutex");
            return false;
        }
        
        // Allocate DoubleBuffer
        doubleBuffer = std::make_unique<DoubleBuffer>();
        memset(doubleBuffer.get(), 0, sizeof(DoubleBuffer));
        
        // Initialize double buffer using existing C function
        initDoubleBuffer(doubleBuffer.get());
        
        // Allocate AudioImageBuffers
        audioImageBuffers = std::make_unique<AudioImageBuffers>();
        memset(audioImageBuffers.get(), 0, sizeof(AudioImageBuffers));
        
        // Initialize audio image buffers using existing C function
        if (audio_image_buffers_init(audioImageBuffers.get()) != 0) {
            juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to init audio image buffers");
            cleanupDoubleBuffer(doubleBuffer.get());
            pthread_mutex_destroy(&context->imu_mutex);
            return false;
        }
        
        // Link buffers to context
        context->doubleBuffer = doubleBuffer.get();
        context->audioImageBuffers = audioImageBuffers.get();
        context->running = 1;
        context->audioData = nullptr;  // Not used in VST
        context->window = nullptr;     // No display in VST
        context->dmxCtx = nullptr;     // No DMX in VST
        
        // 🔧 CRITICAL FIX: Initialize global display buffers (g_displayable_synth_R/G/B)
        // These buffers are written by udpThread() in multithreading.c
        // Without this initialization, the app crashes with NULL pointer dereference
        displayable_synth_buffers_init();
        synth_data_freeze_init();
        image_preprocess_init();
        juce::Logger::writeToLog("Sp3ctraCore: Global display buffers initialized");
        
        // 🔧 CRITICAL: Expose buffers globally for processBlock to use
        extern AudioImageBuffers *g_audioImageBuffers;
        extern DoubleBuffer *g_doubleBuffer;
        g_audioImageBuffers = audioImageBuffers.get();
        g_doubleBuffer = doubleBuffer.get();
        
        juce::Logger::writeToLog("Sp3ctraCore: Buffers initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("Sp3ctraCore: EXCEPTION during buffer init - " + juce::String(e.what()));
        return false;
    }
}

void Sp3ctraCore::shutdownBuffers() {
    // Cleanup global display buffers
    displayable_synth_buffers_cleanup();
    synth_data_freeze_cleanup();
    image_preprocess_cleanup();
    
    if (audioImageBuffers) {
        audio_image_buffers_cleanup(audioImageBuffers.get());
        audioImageBuffers.reset();
    }
    
    if (doubleBuffer) {
        cleanupDoubleBuffer(doubleBuffer.get());
        doubleBuffer.reset();
    }
    
    if (context) {
        pthread_mutex_destroy(&context->imu_mutex);
        context.reset();
    }
    
    juce::Logger::writeToLog("Sp3ctraCore: Buffers cleaned up");
}

bool Sp3ctraCore::initializeUdp(int port, const std::string& address, const std::string& interface) {
    try {
        // Allocate sockaddr structures
        si_me = std::make_unique<sockaddr_in>();
        si_other = std::make_unique<sockaddr_in>();
        memset(si_me.get(), 0, sizeof(sockaddr_in));
        memset(si_other.get(), 0, sizeof(sockaddr_in));
        
        // Link to context
        context->si_me = si_me.get();
        context->si_other = si_other.get();
        
        // Update global config for udp_Init() to use
        extern sp3ctra_config_t g_sp3ctra_config;
        g_sp3ctra_config.udp_port = port;
        strncpy(g_sp3ctra_config.udp_address, address.c_str(), sizeof(g_sp3ctra_config.udp_address) - 1);
        g_sp3ctra_config.udp_address[sizeof(g_sp3ctra_config.udp_address) - 1] = '\0';
        
        if (!interface.empty()) {
            strncpy(g_sp3ctra_config.multicast_interface, interface.c_str(), sizeof(g_sp3ctra_config.multicast_interface) - 1);
            g_sp3ctra_config.multicast_interface[sizeof(g_sp3ctra_config.multicast_interface) - 1] = '\0';
        }
        
        // Initialize UDP using existing C function
        int sock = udp_Init(si_other.get(), si_me.get());
        
        if (sock < 0) {
            juce::Logger::writeToLog("Sp3ctraCore: ERROR - udp_Init failed");
            return false;
        }
        
        socketFd.store(sock);
        context->socket = sock;
        udpRunning.store(true);
        
        juce::Logger::writeToLog(juce::String::formatted(
            "Sp3ctraCore: UDP initialized on %s:%d (socket fd=%d)",
            address.c_str(),
            port,
            sock
        ));
        
        return true;
        
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("Sp3ctraCore: EXCEPTION during UDP init - " + juce::String(e.what()));
        return false;
    }
}

void Sp3ctraCore::shutdownUdp() {
    int sock = socketFd.load();
    
    if (sock >= 0) {
        udpRunning.store(false);
        udp_cleanup(sock);
        socketFd.store(-1);
        
        if (context) {
            context->socket = -1;
        }
        
        juce::Logger::writeToLog("Sp3ctraCore: UDP shutdown complete");
    }
    
    si_me.reset();
    si_other.reset();
}
