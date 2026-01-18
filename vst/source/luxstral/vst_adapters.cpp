/*
 * vst_adapters.cpp
 *
 * C++ implementation of VST adapter functions for LuxStral engine
 *
 * Author: zhonx
 * Created: January 2026
 */

#include <juce_core/juce_core.h>
#include "vst_adapters.h"
#include <cstring>
#include <cstdlib>

// Note: vst_adapters.h already includes everything we need
// No need to include vst_adapters_c.h here (would cause redefinitions)

// Note: shared_var is already defined in context.h and instantiated in multithreading.c

// VST-specific audio buffers for LuxStral (RENAMED to avoid conflicts)
AudioImageBuffer luxstral_buffers_L[2] = {{nullptr, 0, 0}, {nullptr, 0, 0}};
AudioImageBuffer luxstral_buffers_R[2] = {{nullptr, 0, 0}, {nullptr, 0, 0}};
volatile int luxstral_buffer_index = 0;

// VST callback synchronization (producer/consumer handoff)
pthread_mutex_t g_vst_callback_sync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_vst_callback_sync_cond = PTHREAD_COND_INITIALIZER;
volatile int g_vst_callback_consumed_buffer = 1;  // Start as "consumed" so thread can generate first buffer

// Flag to track buffer initialization
static bool luxstral_audio_buffers_initialized = false;
static int luxstral_audio_buffer_size = 0;  // Track current buffer size for reallocation

// NOTE: wavesGeneratorParams, waves, and unitary_waveform are defined in wave_generation.c
// Don't redefine them here to avoid duplicate symbols

// RT Profiler (disabled in VST)
// Define g_rt_profiler here with proper type from rt_profiler.h
extern "C" {
#include "../../src/utils/rt_profiler.h"
}

// Global instance (disabled)
RTProfiler g_rt_profiler = {0};

/* Logging Functions Implementation ------------------------------*/

extern "C" {

void vst_log_info(const char* message) {
    juce::Logger::writeToLog(juce::String(message));
}

void vst_log_warning(const char* message) {
    juce::Logger::writeToLog("WARNING: " + juce::String(message));
}

void vst_log_error(const char* message) {
    juce::Logger::writeToLog("ERROR: " + juce::String(message));
    DBG("ERROR: " << message);  // Also output to debugger
}

/**
 * @brief Initialize LuxStral audio buffers for VST
 * 
 * This function allocates the audio buffers that synth_luxstral.c writes to
 * and processBlock() reads from. Must be called before synthesis starts.
 * 
 * If buffers are already initialized with a different size, they are
 * reallocated to the new size (required when DAW changes buffer size).
 * 
 * @param buffer_size Size of each audio buffer in samples
 * @return 0 on success, -1 on error
 */
int luxstral_init_audio_buffers(int buffer_size) {
    // Check if reallocation is needed (different size)
    if (luxstral_audio_buffers_initialized) {
        if (luxstral_audio_buffer_size == buffer_size) {
            juce::Logger::writeToLog("LuxStral: Audio buffers already initialized with correct size");
            return 0;
        }
        
        // Buffer size changed - need to reallocate
        juce::Logger::writeToLog(juce::String::formatted(
            "LuxStral: Buffer size changed (%d -> %d), reallocating...",
            luxstral_audio_buffer_size, buffer_size));
        luxstral_cleanup_audio_buffers();
    }
    
    if (buffer_size <= 0) {
        juce::Logger::writeToLog("LuxStral: ERROR - Invalid buffer size");
        return -1;
    }
    
    juce::Logger::writeToLog(juce::String::formatted(
        "LuxStral: Initializing audio buffers (size=%d samples)", buffer_size));
    
    // Allocate buffers for both double-buffer slots
    for (int i = 0; i < 2; i++) {
        // Left channel
        luxstral_buffers_L[i].data = (float*)calloc(buffer_size, sizeof(float));
        if (!luxstral_buffers_L[i].data) {
            juce::Logger::writeToLog("LuxStral: ERROR - Failed to allocate left buffer");
            luxstral_cleanup_audio_buffers();
            return -1;
        }
        luxstral_buffers_L[i].ready = 0;
        luxstral_buffers_L[i].write_timestamp_us = 0;
        
        // Right channel
        luxstral_buffers_R[i].data = (float*)calloc(buffer_size, sizeof(float));
        if (!luxstral_buffers_R[i].data) {
            juce::Logger::writeToLog("LuxStral: ERROR - Failed to allocate right buffer");
            luxstral_cleanup_audio_buffers();
            return -1;
        }
        luxstral_buffers_R[i].ready = 0;
        luxstral_buffers_R[i].write_timestamp_us = 0;
    }
    
    luxstral_buffer_index = 0;
    luxstral_audio_buffer_size = buffer_size;  // Store current size
    luxstral_audio_buffers_initialized = true;
    
    juce::Logger::writeToLog("LuxStral: Audio buffers initialized successfully");
    return 0;
}

/**
 * @brief Cleanup LuxStral audio buffers
 */
void luxstral_cleanup_audio_buffers(void) {
    for (int i = 0; i < 2; i++) {
        if (luxstral_buffers_L[i].data) {
            free(luxstral_buffers_L[i].data);
            luxstral_buffers_L[i].data = nullptr;
        }
        luxstral_buffers_L[i].ready = 0;
        
        if (luxstral_buffers_R[i].data) {
            free(luxstral_buffers_R[i].data);
            luxstral_buffers_R[i].data = nullptr;
        }
        luxstral_buffers_R[i].ready = 0;
    }
    
    luxstral_audio_buffers_initialized = false;
    juce::Logger::writeToLog("LuxStral: Audio buffers cleaned up");
}

/**
 * @brief Check if audio buffers are initialized
 * @return true if initialized, false otherwise
 */
bool luxstral_are_audio_buffers_ready(void) {
    return luxstral_audio_buffers_initialized;
}

/**
 * @brief Initialize callback synchronization system
 * 
 * Called once during plugin initialization. The mutex and condition variable
 * are statically initialized, so this is mostly a placeholder for future
 * dynamic initialization if needed.
 */
void luxstral_init_callback_sync(void) {
    g_vst_callback_consumed_buffer = 1;  // Start ready for first synthesis
    juce::Logger::writeToLog("LuxStral: Callback synchronization initialized");
}

/**
 * @brief Cleanup callback synchronization system
 */
void luxstral_cleanup_callback_sync(void) {
    // Wake up any waiting threads before cleanup
    pthread_mutex_lock(&g_vst_callback_sync_mutex);
    g_vst_callback_consumed_buffer = 1;
    pthread_cond_broadcast(&g_vst_callback_sync_cond);
    pthread_mutex_unlock(&g_vst_callback_sync_mutex);
    
    juce::Logger::writeToLog("LuxStral: Callback synchronization cleaned up");
}

/**
 * @brief Signal that processBlock() has consumed a buffer
 * 
 * This is called by the VST's processBlock() after reading audio data.
 * It wakes up the audioProcessingThread so it can generate the next buffer.
 * 
 * RT-SAFE: This function is called from the audio thread, so it must be fast!
 */
void luxstral_signal_buffer_consumed(void) {
    // RT-safe: no locking in audio callback
    __atomic_store_n(&g_vst_callback_consumed_buffer, 1, __ATOMIC_RELEASE);
    pthread_cond_signal(&g_vst_callback_sync_cond);  // Wake up audioProcessingThread
}

/**
 * @brief Wait for processBlock() to consume the current buffer
 * 
 * This is called by audioProcessingThread before generating a new buffer.
 * It blocks until processBlock() signals that it has read the current buffer.
 * 
 * NON-RT: This runs in the synthesis thread, blocking is acceptable here.
 */
void luxstral_wait_for_buffer_consumed(void) {
    pthread_mutex_lock(&g_vst_callback_sync_mutex);

    // Wait until callback has consumed the buffer (with timeout to avoid deadlock)
    while (__atomic_load_n(&g_vst_callback_consumed_buffer, __ATOMIC_ACQUIRE) == 0) {
        struct timespec timeout;
        struct timeval now;
        gettimeofday(&now, NULL);

        // 200ms timeout to avoid deadlock when audio is stopped.
        timeout.tv_sec = now.tv_sec;
        timeout.tv_nsec = now.tv_usec * 1000 + 200000000;
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }

        if (pthread_cond_timedwait(&g_vst_callback_sync_cond,
                                   &g_vst_callback_sync_mutex,
                                   &timeout) == ETIMEDOUT) {
            pthread_mutex_unlock(&g_vst_callback_sync_mutex);
            return;
        }
    }

    // Buffer was consumed, reset flag so we wait next time
    __atomic_store_n(&g_vst_callback_consumed_buffer, 0, __ATOMIC_RELEASE);

    pthread_mutex_unlock(&g_vst_callback_sync_mutex);
}

} // extern "C"
