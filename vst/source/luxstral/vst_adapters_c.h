/*
 * vst_adapters_c.h
 *
 * C-only adaptation layer for LuxStral engine
 * This header can be included from pure C files
 *
 * Author: zhonx
 * Created: January 2026
 */

#ifndef __VST_ADAPTERS_C_H__
#define __VST_ADAPTERS_C_H__

/* Standard C includes -------------------------------------------*/
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>

/* Configuration - include actual definition ---------------------*/
// No forward declaration - include the real definition
// (will be included via doublebuffer.h anyway)

/* Configuration Macros ------------------------------------------*/

// Audio configuration from VST context  
#define AUDIO_BUFFER_SIZE (g_sp3ctra_config.audio_buffer_size)
#define AUDIO_SAMPLE_RATE (g_sp3ctra_config.sampling_frequency)

// Note: CIS_MAX_PIXELS_NB is already defined in config_instrument.h
// Note: SUMMATION_BASE_LEVEL is already defined in config_synth_luxstral.h
// We use the config values, not the hardcoded defines

// LuxStral synthesis configuration from VST context
// VOLUME_AMP_RESOLUTION is 1.0f (normalized waveforms)
#define VOLUME_AMP_RESOLUTION (1.0f)

// Envelope times (use actual config fields)
#define ATTACK_TIME_MS (g_sp3ctra_config.tau_up_base_ms)
#define RELEASE_TIME_MS (g_sp3ctra_config.tau_down_base_ms)

// Gap limiter defaults (not in config struct - use compile-time defaults)
#define GAP_LIMITER_ENABLED (1)
#define GAP_LIMITER_THRESHOLD (0.01f)
#define GAP_LIMITER_ATTACK_MS (10.0f)
#define GAP_LIMITER_RELEASE_MS (50.0f)

// Log frequency for periodic messages
#define LOG_FREQUENCY 100

// Platform detection
#ifdef __APPLE__
  #define PLATFORM_MACOS 1
#elif defined(__linux__)
  #define PLATFORM_LINUX 1
#endif

/* Include existing type definitions from source ----------------*/
#include "doublebuffer.h"          // DoubleBuffer
#include "context.h"                // shared_var
#include "config_instrument.h"      // get_cis_pixels_nb()
#include "config_loader.h"          // sp3ctra_config_t

/* Logging Functions ---------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

void vst_log_info(const char* message);
void vst_log_warning(const char* message);
void vst_log_error(const char* message);

#ifdef __cplusplus
}
#endif

/* Logging Macros ------------------------------------------------*/
#define log_info(tag, fmt, ...) \
    do { \
        char buffer[512]; \
        snprintf(buffer, sizeof(buffer), "[%s] " fmt, tag, ##__VA_ARGS__); \
        vst_log_info(buffer); \
    } while(0)

#define log_warning(tag, fmt, ...) \
    do { \
        char buffer[512]; \
        snprintf(buffer, sizeof(buffer), "[%s] WARNING: " fmt, tag, ##__VA_ARGS__); \
        vst_log_warning(buffer); \
    } while(0)

#define log_error(tag, fmt, ...) \
    do { \
        char buffer[512]; \
        snprintf(buffer, sizeof(buffer), "[%s] ERROR: " fmt, tag, ##__VA_ARGS__); \
        vst_log_error(buffer); \
    } while(0)

/* Error Handling ------------------------------------------------*/
static inline void die(const char* msg) {
    log_error("FATAL", "%s", msg);
    // In VST we can't exit, just log the error
}

/* Image Debug Stubs ---------------------------------------------*/
static inline void image_debug_init(void) {}
static inline void image_debug_mark_new_image_boundary(void) {}
static inline void image_debug_capture_raw_scanner_line(
    const uint8_t *r, const uint8_t *g, const uint8_t *b) {
    (void)r; (void)g; (void)b;
}
static inline int image_debug_is_oscillator_capture_enabled(void) { return 0; }
static inline void image_debug_capture_volume_sample_fast(
    int note, float current_vol, float target_vol) {
    (void)note; (void)current_vol; (void)target_vol;
}

/* Audio Buffers -------------------------------------------------*/
typedef struct {
    float *data;
    volatile int ready;
    uint64_t write_timestamp_us;
} AudioImageBuffer;

// RENAMED to avoid conflicts with audio_c_api.h
extern AudioImageBuffer luxstral_buffers_L[2];
extern AudioImageBuffer luxstral_buffers_R[2];
extern volatile int luxstral_buffer_index;

/* VST Audio Callback Synchronization ----------------------------*/
// Condition variable to synchronize audioProcessingThread with DAW callback
// The thread waits for processBlock() to signal after consuming a buffer
extern pthread_mutex_t g_vst_callback_sync_mutex;
extern pthread_cond_t g_vst_callback_sync_cond;
extern volatile int g_vst_callback_consumed_buffer;  // Flag: 1 = callback read buffer, thread can proceed

// Compatibility macros for LuxStral code
#define buffers_L luxstral_buffers_L
#define buffers_R luxstral_buffers_R
#define current_buffer_index luxstral_buffer_index

// Audio buffer initialization functions (C-compatible declarations)
#ifdef __cplusplus
extern "C" {
#endif

int luxstral_init_audio_buffers(int buffer_size);
void luxstral_cleanup_audio_buffers(void);
void luxstral_init_callback_sync(void);
void luxstral_cleanup_callback_sync(void);
void luxstral_signal_buffer_consumed(void);  // Called by processBlock()
void luxstral_wait_for_buffer_consumed(void); // Called by audioProcessingThread()

#ifdef __cplusplus
}
// C++ only: bool return type
extern "C" bool luxstral_are_audio_buffers_ready(void);
#else
int luxstral_are_audio_buffers_ready(void);  // bool not available in C89
#endif

/* RT Profiler - use real definition from rt_profiler.h --------*/
// No stubs needed - rt_profiler.h is already included via includes chain

/* Lock-Free Pan System Stubs -----------------------------------*/
static inline void lock_free_pan_init(void) {}
static inline void lock_free_pan_cleanup(void) {}
static inline void lock_free_pan_update(int note, float pan_position) {
    (void)note; (void)pan_position;
}
static inline void lock_free_pan_get_gains(int note, float *left, float *right) {
    *left = 0.707f;
    *right = 0.707f;
}

/* Wave Generation Types -----------------------------------------*/
// Note: wave_generation.h defines these types properly
// DO NOT redefine here to avoid conflicts

#endif /* __VST_ADAPTERS_C_H__ */
