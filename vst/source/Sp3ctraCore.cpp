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
    log_info("CORE", "Constructor called");
}

Sp3ctraCore::~Sp3ctraCore() {
    log_info("CORE", "Destructor called");
    shutdown();
}

bool Sp3ctraCore::initialize(const ActiveConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex);
    
    if (initialized.load()) {
        log_info("CORE", "Already initialized, shutting down first");
        shutdownUdp();
        shutdownBuffers();
    }
    
    log_info("CORE", "Initializing...");
    
    // Store active configuration
    active = config;
    
    // Initialize logger with configured level
    logger_init((log_level_t)config.logLevel.load());
    
    // Initialize buffers first
    if (!initializeBuffers()) {
        log_error("CORE", "Failed to initialize buffers");
        return false;
    }
    
    // Initialize UDP
    if (!initializeUdp(config.udpPort.load(), config.udpAddress, config.multicastInterface)) {
        log_error("CORE", "Failed to initialize UDP");
        shutdownBuffers();
        return false;
    }
    
    initialized.store(true);
    log_info("CORE", "Initialization complete");
    
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
        log_info("CORE", "UDP config changed, restarting socket...");
        
        // Restart UDP with new config
        shutdownUdp();
        
        if (!initializeUdp(config.udpPort.load(), config.udpAddress, config.multicastInterface)) {
            log_error("CORE", "Failed to restart UDP");
            return false;
        }
    }
    
    // Update log level if changed
    if (config.logLevel.load() != active.logLevel.load()) {
        logger_init((log_level_t)config.logLevel.load());
        log_info("CORE", "Log level changed to %d", config.logLevel.load());
    }
    
    // Store new active config
    active = config;
    
    return true;
}

bool Sp3ctraCore::restartUdp(int port, const std::string& address, const std::string& interface) {
    std::lock_guard<std::mutex> lock(configMutex);

    if (!initialized.load()) {
        log_error("CORE", "Cannot restart UDP - core not initialized");
        return false;
    }

    log_info("CORE", "Restarting UDP socket only (buffers untouched)...");

    // Close the old socket
    shutdownUdp();

    // 🔧 CRITICAL FIX: Wait longer for socket to be fully released by kernel
    // The UDP thread may take time to exit after stopThread() is called from PluginProcessor
    // This ensures the socket is completely closed before we try to bind a new one
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    log_info("CORE", "Waited 200ms for socket cleanup");

    // 🔧 CRITICAL FIX: Ensure context->running is set to 1 before restarting
    // The UDP thread checks this flag in its main loop
    if (context) {
        context->running = 1;
        log_info("CORE", "Context running flag reset to 1");
    }

    // Create new socket with new parameters
    if (!initializeUdp(port, address, interface)) {
        log_error("CORE", "Failed to restart UDP with new config");
        return false;
    }

    // Update stored config
    active.udpPort.store(port);
    active.udpAddress = address;
    active.multicastInterface = interface;

    log_info("CORE", "UDP restarted on %s:%d (socket fd=%d)",
             address.c_str(), port, socketFd.load());

    return true;
}

void Sp3ctraCore::closeUdpSocket() {
    std::lock_guard<std::mutex> lock(configMutex);

    int sock = socketFd.load();
    if (sock < 0) {
        return;
    }

    log_info("CORE", "Force closing UDP socket fd=%d to unblock recvfrom()", sock);
    udp_cleanup(sock);
    socketFd.store(-1);

    if (context) {
        context->socket = -1;
    }

    udpRunning.store(false);
}

void Sp3ctraCore::shutdown() {
    std::lock_guard<std::mutex> lock(configMutex);
    
    if (!initialized.load()) {
        return;
    }
    
    log_info("CORE", "Shutting down...");
    
    shutdownUdp();
    shutdownBuffers();
    
    initialized.store(false);
    log_info("CORE", "Shutdown complete");
}

bool Sp3ctraCore::initializeBuffers() {
    try {
        // Allocate Context
        context = std::make_unique<Context>();
        memset(context.get(), 0, sizeof(Context));
        
        // Initialize IMU mutex
        if (pthread_mutex_init(&context->imu_mutex, nullptr) != 0) {
            log_error("CORE", "Failed to init IMU mutex");
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
            log_error("CORE", "Failed to init audio image buffers");
            cleanupDoubleBuffer(doubleBuffer.get());
            pthread_mutex_destroy(&context->imu_mutex);
            return false;
        }
        
        // Link buffers to context
        context->doubleBuffer = doubleBuffer.get();
        context->audioImageBuffers = audioImageBuffers.get();
        context->running = 1;              // Controls UDP thread
        context->audio_thread_running = 1; // 🔧 SEPARATE flag for audio thread (VST buffer size changes)
        context->audioData = nullptr;  // Not used in VST
        context->window = nullptr;     // No display in VST
        context->dmxCtx = nullptr;     // No DMX in VST
        
        // 🔧 CRITICAL FIX: Initialize global display buffers (g_displayable_synth_R/G/B)
        // These buffers are written by udpThread() in multithreading.c
        // Without this initialization, the app crashes with NULL pointer dereference
        displayable_synth_buffers_init();
        synth_data_freeze_init();
        image_preprocess_init();
        log_info("CORE", "Global display buffers initialized");
        
        // 🔧 CRITICAL: Expose buffers globally for processBlock to use
        extern AudioImageBuffers *g_audioImageBuffers;
        extern DoubleBuffer *g_doubleBuffer;
        g_audioImageBuffers = audioImageBuffers.get();
        g_doubleBuffer = doubleBuffer.get();
        
        log_info("CORE", "Buffers initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        log_error("CORE", "EXCEPTION during buffer init - %s", e.what());
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
    
    log_info("CORE", "Buffers cleaned up");
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
            log_error("CORE", "udp_Init failed");
            return false;
        }
        
        socketFd.store(sock);
        context->socket = sock;
        udpRunning.store(true);
        
        log_info("CORE", "UDP initialized on %s:%d (socket fd=%d)",
            address.c_str(), port, sock);
        
        return true;
        
    } catch (const std::exception& e) {
        log_error("CORE", "EXCEPTION during UDP init - %s", e.what());
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
        
        log_info("CORE", "UDP shutdown complete");
    }
    
    si_me.reset();
    si_other.reset();
}
