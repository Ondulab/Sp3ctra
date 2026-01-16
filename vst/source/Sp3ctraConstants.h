#pragma once

/**
 * Sp3ctra VST - Fixed Configuration Constants
 * 
 * These parameters are COMPILE-TIME constants and cannot be changed at runtime.
 * They define core architectural constraints for buffer sizes, performance budgets, etc.
 * 
 * If you need to change these values, you must recompile the plugin.
 */

namespace Sp3ctraConstants {
    // ============================================================
    // BUFFER CONFIGURATION (CRITICAL - DO NOT CHANGE)
    // ============================================================
    
    /**
     * Fixed DPI for ALL buffer allocations
     * Buffers are ALWAYS allocated for 400 DPI (3456 pixels), regardless of actual sensor_dpi setting
     * This allows runtime switching between 200/400 DPI without buffer reallocation
     */
    constexpr int FIXED_BUFFER_DPI = 400;
    constexpr int FIXED_BUFFER_PIXELS = 3456;  // 8.64" @ 400 DPI
    
    /**
     * DPI at 200: 1728 pixels (8.64" @ 200 DPI)
     * The actual sensor_dpi parameter only affects UDP parsing, not buffer sizes
     */
    constexpr int BUFFER_PIXELS_200DPI = 1728;
    
    // ============================================================
    // AUDIO CONFIGURATION
    // ============================================================
    
    /**
     * Fixed audio parameters
     * These match typical DAW settings and RT audio constraints
     */
    constexpr int AUDIO_BUFFER_SIZE = 512;       // Samples per buffer
    constexpr int SAMPLING_FREQUENCY = 48000;     // Hz
    
    /**
     * RT audio budget: max 50% of buffer time for processing
     * At 48kHz, 512 samples = 10.67ms → 5.3ms budget
     */
    constexpr double RT_BUDGET_PERCENT = 0.5;
    
    // ============================================================
    // NETWORK PROTOCOL
    // ============================================================
    
    /**
     * UDP packet structure constants
     */
    constexpr int MAX_PACKET_SIZE = 8192;         // Bytes
    constexpr int IMAGE_DATA_HEADER = 0x12;       // Packet type for CIS data
    constexpr int IMU_DATA_HEADER = 0x13;         // Packet type for IMU data
    
    /**
     * Default network configuration (can be overridden by user via APVTS)
     */
    constexpr int DEFAULT_UDP_PORT = 55151;
    constexpr const char* DEFAULT_UDP_ADDRESS = "239.100.100.100";  // Multicast
    constexpr const char* DEFAULT_MULTICAST_INTERFACE = "";         // Auto-detect
    
    // ============================================================
    // SYNTHESIS ENGINE (Future)
    // ============================================================
    
    /**
     * Polyphony limits for future synthesis engines
     */
    constexpr int MAX_VOICES = 32;                // LuxStral polyphony
    constexpr int MAX_PARTIALS_PER_VOICE = 64;    // Spectral synthesis
    
    // ============================================================
    // LOGGING
    // ============================================================
    
    /**
     * Log levels (matches logger.h)
     * 0 = ERROR, 1 = WARNING, 2 = INFO, 3 = DEBUG
     */
    constexpr int DEFAULT_LOG_LEVEL = 2;  // INFO
}
