/* audio_rtaudio.cpp */

#include "audio_rtaudio.h"
#include "audio_c_api.h"
#include "midi_controller.h" // For gMidiController
#include "synth_luxsynth.h" // For polyphonic_audio_buffers and related variables
#include "../../synthesis/luxwave/synth_luxwave.h" // For photowave_audio_buffers
#include "../../config/config_debug.h"    // For debug configuration macros
#include "../../config/config_audio.h"    // For HDMI format configuration
#include "../../config/config_loader.h"   // For runtime configuration access
#include "../../utils/image_debug.h"      // For continuous volume capture
#include "../../synthesis/luxstral/wave_generation.h"  // For waves[] access
#include "../../utils/logger.h"           // For structured logging
#include "../../utils/rt_profiler.h"      // For RT performance profiling
#include <algorithm>         // For std::transform
#include <cstring>
#include <iostream>
#include <rtaudio/RtAudio.h> // Explicitly include RtAudio.h
#include <stdexcept>         // For std::exception
#include <set>               // For std::set
#include <cstdlib>           // For malloc/calloc/free
#include <sys/time.h>        // For gettimeofday

// Global format selection variables for dynamic audio format
static RtAudioFormat g_selected_audio_format = RTAUDIO_FLOAT32;
static const char* g_selected_audio_format_name __attribute__((unused)) = "FLOAT32";

// Global variables for compatibility with legacy code
AudioDataBuffers buffers_L[2];
AudioDataBuffers buffers_R[2];
volatile int current_buffer_index = 0;
pthread_mutex_t buffer_index_mutex = PTHREAD_MUTEX_INITIALIZER;

AudioSystem *gAudioSystem = nullptr;

// Global variables for synth mix levels (accessed from audio callback)
// Volatile ensures thread visibility on modern architectures
static volatile float g_synth_luxstral_mix_level = 1.0f;
static volatile float g_synth_luxsynth_mix_level = 0.5f;
static volatile float g_synth_luxwave_mix_level = 0.0f;  // LuxWave disabled by default

// Global variables for reverb send levels (accessed from audio callback)
static volatile float g_reverb_send_luxstral = 1.0f;   // 100% reverb send for additive by default
static volatile float g_reverb_send_luxsynth = 0.5f; // 50% reverb send for polyphonic by default
static volatile float g_reverb_send_luxwave = 0.0f;  // No reverb send for photowave by default

// Global variables to store requested audio device before AudioSystem is created
extern "C" {
int g_requested_audio_device_id = -1;
char* g_requested_audio_device_name = NULL;
}

// Minimal audio callback control
static bool use_minimal_callback = false;
static float minimal_test_volume = 0.1f;

// RT Performance Profiler (global for instrumentation)
RTProfiler g_rt_profiler;

// Callbacks
int AudioSystem::rtCallback(void *outputBuffer, void *inputBuffer,
                            unsigned int nFrames, double streamTime,
                            RtAudioStreamStatus status, void *userData) {
  (void)inputBuffer; // Mark inputBuffer as unused
  (void)streamTime;  // Mark streamTime as unused
  (void)status;      // Mark status as unused
  auto *audioSystem = static_cast<AudioSystem *>(userData);
  return audioSystem->handleCallback(static_cast<float *>(outputBuffer),
                                     nFrames);
}

int AudioSystem::handleCallback(float *outputBuffer, unsigned int nFrames) {
  // RT Performance Profiler - Start measurement
  rt_profiler_callback_start(&g_rt_profiler);
  
  // RT CALLBACK WATCHDOG: Detect callback freeze/slowness
  static uint64_t last_callback_time_us = 0;
  static int freeze_warning_counter = 0;
  
  struct timeval tv_start;
  gettimeofday(&tv_start, NULL);
  uint64_t current_time_us = (uint64_t)tv_start.tv_sec * 1000000ULL + (uint64_t)tv_start.tv_usec;
  
  // Detect if callback hasn't been called for too long (freeze detection)
  if (last_callback_time_us > 0) {
    uint64_t time_since_last_call = current_time_us - last_callback_time_us;
    // Expected time between callbacks at 48kHz with 512 frames: ~10.7ms (10700µs)
    // At 96kHz with 512 frames: ~5.3ms (5300µs)
    uint64_t expected_interval_us = ((uint64_t)nFrames * 1000000ULL) / g_sp3ctra_config.sampling_frequency;
    uint64_t freeze_threshold_us = expected_interval_us * 5; // 5x expected = problem
    
    if (time_since_last_call > freeze_threshold_us) {
      if (++freeze_warning_counter % 10 == 0) { // Rate limit to every 10th occurrence
        log_warning("AUDIO", "RT callback freeze detected: %llu µs since last call (expected ~%llu µs)",
                   time_since_last_call, expected_interval_us);
      }
    }
  }
  last_callback_time_us = current_time_us;
  
  // MINIMAL CALLBACK MODE - for debugging audio dropouts
  if (use_minimal_callback) {
    float *outLeft = outputBuffer;
    float *outRight = outputBuffer + nFrames;

    // Simple test tone generation - no complex processing
    static float phase = 0.0f;
    static const float frequency = 440.0f; // A4 note
    static const float sample_rate = g_sp3ctra_config.sampling_frequency;
    static const float phase_increment = 2.0f * M_PI * frequency / sample_rate;

    for (unsigned int i = 0; i < nFrames; i++) {
      float sample = sinf(phase) * minimal_test_volume;
      outLeft[i] = sample;
      outRight[i] = sample;

      phase += phase_increment;
      if (phase >= 2.0f * M_PI) {
        phase -= 2.0f * M_PI;
      }
    }

    return 0; // Success - no dropouts with minimal processing
  }

  // ULTRA-OPTIMIZED CALLBACK - for 96kHz performance
  // Non-interleaved stereo configuration (like RtAudio default)
  float *outLeft = outputBuffer;
  float *outRight = outputBuffer + nFrames;

  // SYNCHRONIZED BUFFER READING - Single offset for all synths to prevent desync
  // This ensures all synthesizers read from the same temporal position
  static unsigned int global_read_offset = 0;
  static int additive_read_buffer = 0;
  static int polyphonic_read_buffer = 0;
  static int photowave_read_buffer = 0;
  
  // DESYNC TRACKING: Monitor when photowave reads without additive (causes distortion)
  static uint64_t additive_missing_streak = 0;
  static uint64_t photowave_read_without_luxstral = 0;
  static uint64_t last_sync_check_time_us = 0;

  // Cache volume level to avoid repeated access
  static float cached_volume = 1.0f;
  static int cache_counter = 0;

  // Update cache automatically for smooth volume transitions
  if (++cache_counter >= 8) {  // Static value instead of dynamic macro
    cache_counter = 0;
    cached_volume = this->masterVolume;
  }
  
  // Read mix levels from global variables (controlled via MIDI)
  float cached_level_luxstral = g_synth_luxstral_mix_level;
  float cached_level_luxsynth = g_synth_luxsynth_mix_level;
  float cached_level_luxwave = g_synth_luxwave_mix_level;
  
  // Read reverb send levels from global variables (controlled via MIDI)
  float cached_reverb_send_luxstral = g_reverb_send_luxstral;
  float cached_reverb_send_luxsynth = g_reverb_send_luxsynth;
  float cached_reverb_send_luxwave = g_reverb_send_luxwave;

  unsigned int framesToRender = nFrames;

  // Process frames using multiple buffers if needed
  while (framesToRender > 0) {
    // SYNCHRONIZED: All synths use the same offset for temporal alignment
    unsigned int framesAvailable = g_sp3ctra_config.audio_buffer_size - global_read_offset;
    unsigned int chunk =
        (framesToRender < framesAvailable) ? framesToRender : framesAvailable;

    // Get source pointers directly - avoid memcpy when possible
    float *source_luxstral_left = nullptr;
    float *source_luxstral_right = nullptr;
    float *source_luxwave = nullptr;

    // SYNCHRONIZED BUFFER ACCESS: All synths read from same offset
    // This prevents temporal desynchronization and audio artifacts
    
    // BUFFER LATENCY MEASUREMENT: Measure time from buffer write to read
    static int latency_warning_counter = 0;
    
    // DESYNC DETECTION: Track when additive buffer is missing
    bool additive_ready = (buffers_L[additive_read_buffer].ready == 1 && 
                          buffers_R[additive_read_buffer].ready == 1);
    bool photowave_ready = (photowave_audio_buffers[photowave_read_buffer].ready == 1);
    
    if (!additive_ready) {
      additive_missing_streak++;
      if (photowave_ready) {
        photowave_read_without_luxstral++;
      }
    }
    
    // LuxStral synthesis (stereo)
    if (buffers_L[additive_read_buffer].ready == 1) {
      source_luxstral_left = &buffers_L[additive_read_buffer].data[global_read_offset];
      
      // Measure latency only at start of buffer read (global_read_offset == 0)
      if (global_read_offset == 0 && buffers_L[additive_read_buffer].write_timestamp_us > 0) {
        uint64_t latency_us = current_time_us - buffers_L[additive_read_buffer].write_timestamp_us;
        
        // DYNAMIC LATENCY THRESHOLD: Calculate based on buffer size
        // For double buffering, expect latency of ~2x buffer duration
        // Add 50% margin for processing overhead
        uint64_t buffer_duration_us = ((uint64_t)g_sp3ctra_config.audio_buffer_size * 1000000ULL) / g_sp3ctra_config.sampling_frequency;
        uint64_t latency_threshold_us = (buffer_duration_us * 2) + (buffer_duration_us / 2); // 2.5x buffer duration
        
        if (latency_us > latency_threshold_us) {
          if (++latency_warning_counter % 10 == 0) { // Rate limit
            log_warning("AUDIO", "High additive buffer latency: %llu µs (threshold: %llu µs for %d frames)", 
                       latency_us, latency_threshold_us, g_sp3ctra_config.audio_buffer_size);
          }
        }
      }
    }
    if (buffers_R[additive_read_buffer].ready == 1) {
      source_luxstral_right = &buffers_R[additive_read_buffer].data[global_read_offset];
    }

    // LuxSynth synthesis (stereo with spectral panning)
    float *source_fft_left = nullptr;
    float *source_fft_right = nullptr;
    if (polyphonic_audio_buffers[polyphonic_read_buffer].ready == 1) {
      source_fft_left = &polyphonic_audio_buffers[polyphonic_read_buffer]
                             .data_left[global_read_offset];
      source_fft_right = &polyphonic_audio_buffers[polyphonic_read_buffer]
                              .data_right[global_read_offset];
      
      // Measure latency only at start of buffer read
      if (global_read_offset == 0 && polyphonic_audio_buffers[polyphonic_read_buffer].write_timestamp_us > 0) {
        uint64_t latency_us = current_time_us - polyphonic_audio_buffers[polyphonic_read_buffer].write_timestamp_us;
        
        // DYNAMIC LATENCY THRESHOLD: Calculate based on buffer size
        // For double buffering, expect latency of ~2x buffer duration
        // Add 50% margin for processing overhead
        uint64_t buffer_duration_us = ((uint64_t)g_sp3ctra_config.audio_buffer_size * 1000000ULL) / g_sp3ctra_config.sampling_frequency;
        uint64_t latency_threshold_us = (buffer_duration_us * 2) + (buffer_duration_us / 2); // 2.5x buffer duration
        
        if (latency_us > latency_threshold_us) {
          if (++latency_warning_counter % 10 == 0) { // Rate limit
            log_warning("AUDIO", "High polyphonic buffer latency: %llu µs (threshold: %llu µs for %d frames)", 
                       latency_us, latency_threshold_us, g_sp3ctra_config.audio_buffer_size);
          }
        }
      }
    }

    // LuxWave synthesis (mono)
    if (photowave_audio_buffers[photowave_read_buffer].ready == 1) {
      source_luxwave = &photowave_audio_buffers[photowave_read_buffer]
                             .data[global_read_offset];
    }

  // OPTIMIZED MIXING - Direct to output with threaded reverb
  for (unsigned int i = 0; i < chunk; i++) {
      float dry_sample_left = 0.0f;
      float dry_sample_right = 0.0f;

      // CRITICAL FIX: Apply volume BEFORE routing to dry/reverb
      // This ensures volume control affects both dry signal AND reverb send
      // When volume=0%, both dry and reverb should be silent
      
      // Add LuxStral synthesis contribution - separate left and right channels
      // Volume is applied here, creating the "post-volume" signal
      float additive_with_volume_left = 0.0f;
      float additive_with_volume_right = 0.0f;
      if (source_luxstral_left) {
        additive_with_volume_left = source_luxstral_left[i] * cached_level_luxstral;
        dry_sample_left += additive_with_volume_left;
      }
      if (source_luxstral_right) {
        additive_with_volume_right = source_luxstral_right[i] * cached_level_luxstral;
        dry_sample_right += additive_with_volume_right;
      }
      
#if DEBUG_AUDIO_SIGNAL
      // DEBUG: Log signal levels every 4800 samples (~100ms at 48kHz)
      static int debug_counter = 0;
      if (++debug_counter >= 4800) {
        debug_counter = 0;
        printf("SIGNAL DEBUG: dry_L=%.6f, dry_R=%.6f, additive_level=%.3f, reverb_send=%.3f\n",
               dry_sample_left, dry_sample_right, cached_level_luxstral, cached_reverb_send_luxstral);
        if (source_luxstral_left) {
          printf("SOURCE DEBUG: additive_L=%.6f, additive_R=%.6f\n",
                 source_luxstral_left[i], source_luxstral_right ? source_luxstral_right[i] : 0.0f);
        }
      }
#endif

      // Add polyphonic contribution (true stereo with spectral panning)
      // Volume is applied here, creating the "post-volume" signal
      float polyphonic_with_volume_left = 0.0f;
      float polyphonic_with_volume_right = 0.0f;
      if (source_fft_left && source_fft_right) {
        polyphonic_with_volume_left = source_fft_left[i] * cached_level_luxsynth;
        polyphonic_with_volume_right = source_fft_right[i] * cached_level_luxsynth;
        dry_sample_left += polyphonic_with_volume_left;
        dry_sample_right += polyphonic_with_volume_right;
      }

      // Add LuxWave contribution (same for both channels)
      // CPU OPTIMIZATION: Skip photowave processing if mix level is essentially zero
      // RACE CONDITION FIX: Removed additive dependency check (source_luxstral_left)
      // LuxWave now produces buffers continuously, so no sync protection needed
      // Volume is applied here, creating the "post-volume" signal
      float photowave_with_volume = 0.0f;
      if (source_luxwave && cached_level_luxwave > 0.01f) {
        photowave_with_volume = source_luxwave[i] * cached_level_luxwave;
        dry_sample_left += photowave_with_volume;
        dry_sample_right += photowave_with_volume;
      }

      // Direct reverb processing in callback - ULTRA OPTIMIZED
      float reverb_left = 0.0f, reverb_right = 0.0f;

#if ENABLE_REVERB
      // CRITICAL FIX: Detect when ALL reverb sends go to zero and clear reverb buffers
      // This prevents "ghost reverb" from lingering when sends are cut
      static bool all_sends_zero_last_frame = false;
      bool all_sends_zero = (cached_reverb_send_luxstral <= 0.01f &&
                             cached_reverb_send_luxsynth <= 0.01f &&
                             cached_reverb_send_luxwave <= 0.01f);
      
      // If transitioning from "at least one send active" to "all sends zero"
      // then clear the reverb buffers immediately
      if (all_sends_zero && !all_sends_zero_last_frame && reverbEnabled) {
        zitaRev.clear();  // Clear the 8 delay lines
      }
      all_sends_zero_last_frame = all_sends_zero;

#if DEBUG_AUDIO_REVERB
      // DEBUG: Log reverb condition check
      static int reverb_debug_counter = 0;
      if (++reverb_debug_counter >= 4800) {
        reverb_debug_counter = 0;
        printf("REVERB CONDITION: ENABLE_REVERB=%d, reverbEnabled=%d, send_luxstral=%.3f, send_poly=%.3f, send_luxwave=%.3f, all_zero=%d\n",
               ENABLE_REVERB, reverbEnabled ? 1 : 0, cached_reverb_send_luxstral, cached_reverb_send_luxsynth, cached_reverb_send_luxwave, all_sends_zero ? 1 : 0);
      }
#endif
      
      if (reverbEnabled && !all_sends_zero) {
        float reverb_input_left = 0.0f;
        float reverb_input_right = 0.0f;

        // CRITICAL FIX: Reverb sends now use POST-VOLUME signal
        // Volume control (mix level) is applied BEFORE reverb send
        // This ensures volume=0% results in complete silence (dry + reverb)
        // The reverb_send parameter controls how much of the POST-VOLUME signal goes to reverb
        if (source_luxstral_left && cached_reverb_send_luxstral > 0.01f) {
          reverb_input_left += additive_with_volume_left * cached_reverb_send_luxstral;
        }
        if (source_luxstral_right && cached_reverb_send_luxstral > 0.01f) {
          reverb_input_right += additive_with_volume_right * cached_reverb_send_luxstral;
        }
        // CRITICAL FIX: Reverb send now uses POST-VOLUME signal for polyphonic
        // Volume control (mix level) is applied BEFORE reverb send
        // This ensures volume=0% results in complete silence (dry + reverb)
        if (source_fft_left && cached_reverb_send_luxsynth > 0.01f) {
          reverb_input_left += polyphonic_with_volume_left * cached_reverb_send_luxsynth;
        }
        if (source_fft_right && cached_reverb_send_luxsynth > 0.01f) {
          reverb_input_right += polyphonic_with_volume_right * cached_reverb_send_luxsynth;
        }

        // Add photowave signal to reverb (using post-volume signal)
        if (source_luxwave && cached_reverb_send_luxwave > 0.01f) {
          reverb_input_left += photowave_with_volume * cached_reverb_send_luxwave;
          reverb_input_right += photowave_with_volume * cached_reverb_send_luxwave;
        }

        // Single-sample reverb processing (optimized) - use average of
        // left/right for input
        float reverb_input_mono =
            (reverb_input_left + reverb_input_right) * 0.5f;
        processReverbOptimized(reverb_input_mono, reverb_input_mono,
                               reverb_left, reverb_right);
      }
#endif

      // Mix dry + reverb
      float mixed_left = dry_sample_left + reverb_left;
      float mixed_right = dry_sample_right + reverb_right;

      // Store in output buffers for EQ processing
      outLeft[i] = mixed_left;
      outRight[i] = mixed_right;
      
    }

    // Apply EQ to the chunk (after reverb, before master volume)
    if (gEqualizer && gEqualizer->isEnabled()) {
      float* eqData[2] = { outLeft, outRight };
      eq_Process(chunk, 2, eqData);
    }

    // Apply master volume and limiting to the chunk
    for (unsigned int i = 0; i < chunk; i++) {
      float final_left = outLeft[i] * cached_volume;
      float final_right = outRight[i] * cached_volume;

      // Limiting
      final_left = (final_left > 1.0f)    ? 1.0f
                   : (final_left < -1.0f) ? -1.0f
                                          : final_left;
      final_right = (final_right > 1.0f)    ? 1.0f
                    : (final_right < -1.0f) ? -1.0f
                                            : final_right;

      // Output (with optional L/R swap)
#if SWAP_LEFT_RIGHT_CHANNELS
      outLeft[i] = final_right;   // Swap: left gets right
      outRight[i] = final_left;   // Swap: right gets left
#else
      outLeft[i] = final_left;    // Normal: left gets left
      outRight[i] = final_right;  // Normal: right gets right
#endif
    }

    // Advance pointers
    outLeft += chunk;
    outRight += chunk;
    global_read_offset += chunk;
    framesToRender -= chunk;

    // SYNCHRONIZED BUFFER TRANSITIONS - All synths switch together
    // This is the key fix: all synthesizers transition at the same time
    // preventing temporal desynchronization
    if (global_read_offset >= (unsigned int)g_sp3ctra_config.audio_buffer_size) {
      // Mark all current buffers as consumed (RT-SAFE atomic operations)
      
      // LuxStral synthesis buffers
      if (buffers_L[additive_read_buffer].ready == 1) {
        __atomic_store_n(&buffers_L[additive_read_buffer].ready, 0, __ATOMIC_RELEASE);
      }
      if (buffers_R[additive_read_buffer].ready == 1) {
        __atomic_store_n(&buffers_R[additive_read_buffer].ready, 0, __ATOMIC_RELEASE);
      }
      
      // LuxSynth synthesis buffer
      if (polyphonic_audio_buffers[polyphonic_read_buffer].ready == 1) {
        __atomic_store_n(&polyphonic_audio_buffers[polyphonic_read_buffer].ready, 0, __ATOMIC_RELEASE);
      }
      
      // LuxWave synthesis buffer
      if (photowave_audio_buffers[photowave_read_buffer].ready == 1) {
        __atomic_store_n(&photowave_audio_buffers[photowave_read_buffer].ready, 0, __ATOMIC_RELEASE);
        // CRITICAL FIX: Signal the photowave thread that buffer has been consumed
        pthread_cond_signal(&photowave_audio_buffers[photowave_read_buffer].cond);
      }
      
      // Switch all buffers simultaneously
      additive_read_buffer = (additive_read_buffer == 0) ? 1 : 0;
      polyphonic_read_buffer = (polyphonic_read_buffer == 0) ? 1 : 0;
      photowave_read_buffer = (photowave_read_buffer == 0) ? 1 : 0;
      
      // Reset global offset for next buffer
      global_read_offset = 0;
    }
  }

  // CALLBACK EXECUTION TIME MEASUREMENT
  struct timeval tv_end;
  gettimeofday(&tv_end, NULL);
  uint64_t callback_end_us = (uint64_t)tv_end.tv_sec * 1000000ULL + (uint64_t)tv_end.tv_usec;
  uint64_t callback_duration_us = callback_end_us - current_time_us;
  
  // Calculate time budget: time available for processing one buffer
  // At 48kHz with 512 frames: 10666µs budget
  // At 96kHz with 512 frames: 5333µs budget
  uint64_t time_budget_us = ((uint64_t)nFrames * 1000000ULL) / g_sp3ctra_config.sampling_frequency;
  
  // LOWERED THRESHOLD: Warning if callback takes more than 10% of time budget (very sensitive)
  uint64_t warning_threshold_us = time_budget_us / 10;
  
  static int callback_slow_counter = 0;
  if (callback_duration_us > warning_threshold_us) {
    float cpu_usage_percent = (float)callback_duration_us * 100.0f / (float)time_budget_us;
    printf("[AUDIO] Callback slow: %llu µs (%.1f%% of %llu µs budget)\n",
           callback_duration_us, cpu_usage_percent, time_budget_us);
    fflush(stdout);
    callback_slow_counter++;
  }
  
  // BUFFER MISSING DETECTION: Report to RT profiler (no printf in RT callback)
  if (buffers_L[additive_read_buffer].ready != 1 || buffers_R[additive_read_buffer].ready != 1) {
    rt_profiler_report_buffer_miss_luxstral(&g_rt_profiler);
  }
  
  if (polyphonic_audio_buffers[polyphonic_read_buffer].ready != 1) {
    rt_profiler_report_buffer_miss_luxsynth(&g_rt_profiler);
  }
  
  if (photowave_audio_buffers[photowave_read_buffer].ready != 1) {
    rt_profiler_report_buffer_miss_luxwave(&g_rt_profiler);
  }
  
  // DESYNC MONITORING: Report photowave desync events periodically
  if (current_time_us - last_sync_check_time_us > 1000000ULL) { // Every 1 second
    if (photowave_read_without_luxstral > 0) {
      printf("[SYNC WARNING] LuxWave desync events: %llu (additive missing: %llu)\n",
             photowave_read_without_luxstral, additive_missing_streak);
      printf("[SYNC INFO] Sync protection active: photowave skipped when additive missing\n");
      fflush(stdout);
    }
    // Reset counters for next interval
    additive_missing_streak = 0;
    photowave_read_without_luxstral = 0;
    last_sync_check_time_us = current_time_us;
  }

  // RT Performance Profiler - End measurement
  rt_profiler_callback_end(&g_rt_profiler);

  return 0;
}

// Constructeur
AudioSystem::AudioSystem(unsigned int sampleRate, unsigned int bufferSize,
                         unsigned int channels)
    : audio(nullptr), isRunning(false), // Moved isRunning before members that
                                        // might use it implicitly or explicitly
      sampleRate(g_sp3ctra_config.sampling_frequency), bufferSize(g_sp3ctra_config.audio_buffer_size), channels(channels),
      requestedDeviceId(g_requested_audio_device_id), // Use global variable if
                                                      // set, otherwise -1
      masterVolume(1.0f), reverbBuffer(nullptr), reverbMix(DEFAULT_REVERB_MIX),
      reverbRoomSize(DEFAULT_REVERB_ROOM_SIZE),
      reverbDamping(DEFAULT_REVERB_DAMPING), reverbWidth(DEFAULT_REVERB_WIDTH),
      reverbEnabled(ENABLE_REVERB), reverbThreadRunning(false) {

  log_info("AUDIO", "Reverb enabled by default with Zita-Rev1 algorithm");

  processBuffer.resize(bufferSize * channels);

  // Initialize reverb buffer (for compatibility)
  reverbBuffer = new float[REVERB_BUFFER_SIZE];
  for (int i = 0; i < REVERB_BUFFER_SIZE; i++) {
    reverbBuffer[i] = 0.0f;
  }
  
  // CRITICAL: Initialize reverb mix with DEFAULT value from config
  reverbMix = DEFAULT_REVERB_MIX;
  log_info("AUDIO", "Reverb mix initialized to %.1f%% (%.2f)", reverbMix * 100.0f, reverbMix);

  // Configure delays for reverb (for compatibility)
  reverbDelays[0] = 1116;
  reverbDelays[1] = 1356;
  reverbDelays[2] = 1422;
  reverbDelays[3] = 1617;
  reverbDelays[4] = 1188;
  reverbDelays[5] = 1277;
  reverbDelays[6] = 1491;
  reverbDelays[7] = 1557;

  // Configure ZitaRev1 with values for long and
  // smooth reverb
  zitaRev.init(sampleRate);
  zitaRev.set_roomsize(
      0.95f); // Very large room size for long reverb
  zitaRev.set_damping(0.4f); // Reduced high frequency damping for
                             // more brightness
  zitaRev.set_width(1.0f);   // Maximum stereo width
  zitaRev.set_delay(
      0.08f);            // Larger pre-delay for clarity and separation
  zitaRev.set_mix(1.0f); // CRITICAL: 100% wet - ZitaRev outputs ONLY wet signal, no dry/wet mixing inside
  
  log_info("AUDIO", "ZitaRev1 configured: roomsize=0.95, damping=0.4, width=1.0, mix=1.0 (100%% wet - dry/wet mixing handled externally)");
}

// Destructeur
AudioSystem::~AudioSystem() {
  stop();

  // Free reverb buffer
  if (reverbBuffer) {
    delete[] reverbBuffer;
    reverbBuffer = nullptr;
  }

  if (audio) {
    if (audio->isStreamOpen()) {
      audio->closeStream();
    }
    delete audio;
  }
}

// ULTRA-OPTIMIZED reverb function for real-time callback
void AudioSystem::processReverbOptimized(float inputL, float inputR,
                                         float &outputL, float &outputR) {
#if DEBUG_AUDIO_REVERB
  // DEBUG: Log reverb function calls
  static int reverb_call_counter = 0;
  if (++reverb_call_counter >= 4800) {
    reverb_call_counter = 0;
    printf("REVERB CALLED: inputL=%.6f, inputR=%.6f, reverbEnabled=%d, reverbMix=%.3f\n",
           inputL, inputR, reverbEnabled ? 1 : 0, reverbMix);
  }
#endif
  
  // CPU OPTIMIZATION: Skip all reverb processing if mix is zero or reverb
  // disabled
  // CRITICAL FIX: Return SILENCE (0.0f), not the input signal!
  // When reverbMix = 0%, we want NO reverb in the output, not a copy of the input
  if (!reverbEnabled || reverbMix <= 0.0f) {
    outputL = 0.0f;
    outputR = 0.0f;
#if DEBUG_AUDIO_REVERB
    printf("REVERB SKIPPED: reverbEnabled=%d, reverbMix=%.3f\n", reverbEnabled ? 1 : 0, reverbMix);
#endif
    return;
  }

  // Static cache to avoid repetitive calculations
  static float cached_wet_gain = reverbMix; // Initialiser directement avec la valeur de reverb
  static float cached_dry_gain = 1.0f - reverbMix; // Compense pour avoir 100% du signal
  static int param_update_counter = 0;
  static bool reverb_initialized = false;

  // Initialize reverb once at startup
  if (!reverb_initialized) {
    // Explicitly clear internal buffers to avoid clicks
    zitaRev.clear();
    // Initialize basic parameters
    zitaRev.set_roomsize(reverbRoomSize);
    zitaRev.set_damping(reverbDamping);
    zitaRev.set_width(reverbWidth);
    // Initialiser les gains avec les bonnes valeurs
    cached_wet_gain = reverbMix;
    cached_dry_gain = 1.0f - reverbMix;
    reverb_initialized = true;
  }
  
  // Regular parameter update
  if (++param_update_counter >= 256) {
    param_update_counter = 0;
    cached_wet_gain = reverbMix;
    cached_dry_gain = 1.0f - reverbMix;

    // Update ZitaRev1 parameters less frequently
    zitaRev.set_roomsize(reverbRoomSize);
    zitaRev.set_damping(reverbDamping);
    zitaRev.set_width(reverbWidth);
  }

  // Optimized ZitaRev1 processing - single sample
  float inBufferL[1] = {inputL};
  float inBufferR[1] = {inputR};
  float outBufferL[1] = {0.0f};
  float outBufferR[1] = {0.0f};

  // Direct call to ZitaRev1 (main cost)
  zitaRev.process(inBufferL, inBufferR, outBufferL, outBufferR, 1);

  // CRITICAL FIX: Return ONLY wet signal (reverb processed)
  // Dry/wet mixing is handled in the main callback, not here
  // This prevents signal duplication when reverb_send is high
  outputL = outBufferL[0] * cached_wet_gain;
  outputR = outBufferR[0] * cached_wet_gain;
  
#if DEBUG_AUDIO_REVERB
  // DEBUG: Log reverb output values
  static int reverb_output_counter = 0;
  if (++reverb_output_counter >= 4800) {
    reverb_output_counter = 0;
    printf("REVERB OUTPUT: zita_outL=%.6f, zita_outR=%.6f, wet_gain=%.3f, dry_gain=%.3f\n",
           outBufferL[0], outBufferR[0], cached_wet_gain, cached_dry_gain);
    printf("FINAL REVERB: outputL=%.6f, outputR=%.6f (dry=%.6f + wet=%.6f)\n",
           outputL, outputR, inputL * cached_dry_gain, outBufferL[0] * cached_wet_gain);
  }
#endif
}

// === MULTI-THREADED REVERB IMPLEMENTATION ===

// Function to write to reverb input buffer
// (thread-safe)
bool AudioSystem::writeToReverbInput(float sample) {
  int currentWrite = reverbInputBuffer.write_pos.load();
  int nextWrite = (currentWrite + 1) % REVERB_THREAD_BUFFER_SIZE;

  // Check if buffer is not full
  if (nextWrite == reverbInputBuffer.read_pos.load()) {
    return false; // Buffer plein
  }

  reverbInputBuffer.data[currentWrite] = sample;
  reverbInputBuffer.write_pos.store(nextWrite);
  reverbInputBuffer.available_samples.fetch_add(1);

  return true;
}

// Function to read from reverb input buffer
// (thread-safe)
bool AudioSystem::readFromReverbInput(float &sample) {
  if (reverbInputBuffer.available_samples.load() == 0) {
    return false; // Buffer vide
  }

  int currentRead = reverbInputBuffer.read_pos.load();
  sample = reverbInputBuffer.data[currentRead];

  int nextRead = (currentRead + 1) % REVERB_THREAD_BUFFER_SIZE;
  reverbInputBuffer.read_pos.store(nextRead);
  reverbInputBuffer.available_samples.fetch_sub(1);

  return true;
}

// Function to write to reverb output buffer
// (thread-safe)
bool AudioSystem::writeToReverbOutput(float sampleL, float sampleR) {
  int currentWrite = reverbOutputBuffer.write_pos.load();
  int nextWrite = (currentWrite + 1) % REVERB_THREAD_BUFFER_SIZE;

  // Check if buffer is not full
  if (nextWrite == reverbOutputBuffer.read_pos.load()) {
    return false; // Buffer plein
  }

  reverbOutputBuffer.left[currentWrite] = sampleL;
  reverbOutputBuffer.right[currentWrite] = sampleR;
  reverbOutputBuffer.write_pos.store(nextWrite);
  reverbOutputBuffer.available_samples.fetch_add(1);

  return true;
}

// Function to read from reverb output buffer
// (thread-safe)
bool AudioSystem::readFromReverbOutput(float &sampleL, float &sampleR) {
  if (reverbOutputBuffer.available_samples.load() == 0) {
    sampleL = sampleR = 0.0f; // Silence if no data
    return false;
  }

  int currentRead = reverbOutputBuffer.read_pos.load();
  sampleL = reverbOutputBuffer.left[currentRead];
  sampleR = reverbOutputBuffer.right[currentRead];

  int nextRead = (currentRead + 1) % REVERB_THREAD_BUFFER_SIZE;
  reverbOutputBuffer.read_pos.store(nextRead);
  reverbOutputBuffer.available_samples.fetch_sub(1);

  return true;
}

// Main reverb thread function
void AudioSystem::reverbThreadFunction() {
  std::cout
      << "\033[1;33m[REVERB THREAD] Thread de réverbération démarré\033[0m"
      << std::endl;

  const int processingBlockSize = 64; // Process in blocks for efficiency
  float inputBuffer[processingBlockSize];
  float outputBufferL[processingBlockSize];
  float outputBufferR[processingBlockSize];

  while (reverbThreadRunning.load()) {
    int samplesProcessed = 0;

    // Read a block of samples from input buffer
    for (int i = 0; i < processingBlockSize; i++) {
      float sample;
      if (readFromReverbInput(sample)) {
        inputBuffer[i] = sample;
        samplesProcessed++;
      } else {
        inputBuffer[i] = 0.0f; // Silence if no data
      }
    }

    if (samplesProcessed > 0 && reverbEnabled) {
      // Process reverb in blocks for efficiency
      for (int i = 0; i < processingBlockSize; i++) {
        // Update ZitaRev1 parameters (from time to time)
        if (i == 0) { // Only at start of block
          zitaRev.set_roomsize(reverbRoomSize);
          zitaRev.set_damping(reverbDamping);
          zitaRev.set_width(reverbWidth);
        }

        // Mono to stereo processing
        float inBufferL[1] = {inputBuffer[i]};
        float inBufferR[1] = {inputBuffer[i]};
        float outBufferL_single[1] = {0.0f};
        float outBufferR_single[1] = {0.0f};

        // Process via ZitaRev1
        zitaRev.process(inBufferL, inBufferR, outBufferL_single,
                        outBufferR_single, 1);

        // Apply dry/wet mix
        float wetGain = reverbMix;
        float dryGain = 1.0f - reverbMix;

        outputBufferL[i] =
            inputBuffer[i] * dryGain + outBufferL_single[0] * wetGain;
        outputBufferR[i] =
            inputBuffer[i] * dryGain + outBufferR_single[0] * wetGain;
      }

      // Write results to output buffer
      for (int i = 0; i < processingBlockSize; i++) {
        // Try to write, ignore if output buffer is full
        writeToReverbOutput(outputBufferL[i], outputBufferR[i]);
      }
    } else {
      // No samples to process or reverb disabled, wait a bit
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  std::cout << "\033[1;33m[REVERB THREAD] Thread de réverbération arrêté\033[0m"
            << std::endl;
}

// Initialisation
bool AudioSystem::initialize() {
  // Forcer l'utilisation de l'API ALSA sur Linux
#ifdef __linux__
  log_info("AUDIO", "Attempting to initialize RtAudio with ALSA API");
  audio = new RtAudio(RtAudio::LINUX_ALSA);
#else
  audio = new RtAudio();
#endif

  // Check if RtAudio was correctly created
  if (!audio) {
    log_error("AUDIO", "Unable to create RtAudio instance");
    return false;
  }

  // Get and print available devices
  unsigned int deviceCount = 0;
  try {
    deviceCount = audio->getDeviceCount();
  } catch (const std::exception &error) {
    log_error("AUDIO", "Error getting device count: %s", error.what());
    delete audio;
    audio = nullptr;
    return false;
  }

  // Simplified device selection - use default device unless specific device requested
  unsigned int preferredDeviceId = audio->getDefaultOutputDevice();
  bool foundRequestedDevice = false;
  
  // Handle specific device requests (ID or name)
  if (requestedDeviceId >= 0 || g_requested_audio_device_name != NULL) {
    // Only enumerate devices when specifically requested
    log_info("AUDIO", "Searching for specific audio device");
    
    // Perform full enumeration only when needed
    std::vector<std::pair<unsigned int, std::string>> accessibleDevices;
    for (unsigned int i = 0; i < deviceCount; i++) {
      try {
        RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
        if (info.outputChannels > 0 && !info.name.empty()) {
          accessibleDevices.push_back(std::make_pair(i, info.name));
          
          // Check for requested device ID
          if (requestedDeviceId >= 0 && i == (unsigned int)requestedDeviceId) {
            preferredDeviceId = i;
            foundRequestedDevice = true;
            log_info("AUDIO", "Found requested device ID %u: %s", i, info.name.c_str());
          }
          
          // Check for requested device name
          if (g_requested_audio_device_name != NULL) {
            std::string deviceName = info.name;
            std::string searchName = g_requested_audio_device_name;
            std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), ::tolower);
            std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);
            
            if (deviceName.find(searchName) != std::string::npos) {
              preferredDeviceId = i;
              foundRequestedDevice = true;
              log_info("AUDIO", "Found requested device '%s': %s", 
                      g_requested_audio_device_name, info.name.c_str());
              break;
            }
          }
        }
      } catch (const std::exception &error) {
        // Silently skip problematic devices during specific search
        continue;
      }
    }
    
    // Validate that requested device was found
    if ((requestedDeviceId >= 0 || g_requested_audio_device_name != NULL) && !foundRequestedDevice) {
      log_error("AUDIO", "Requested audio device not found");
      if (requestedDeviceId >= 0) {
        log_error("AUDIO", "Device ID %d is not available", requestedDeviceId);
      }
      if (g_requested_audio_device_name != NULL) {
        log_error("AUDIO", "Device '%s' is not available", g_requested_audio_device_name);
      }
      log_error("AUDIO", "Use --list-audio-devices to see available devices");
      return false;
    }
  } else {
    // Default behavior: use default device without enumeration
    try {
      RtAudio::DeviceInfo defaultInfo = audio->getDeviceInfo(preferredDeviceId);
      log_info("AUDIO", "Using default audio device: %s", defaultInfo.name.c_str());
    } catch (const std::exception &error) {
      log_error("AUDIO", "Cannot access default audio device: %s", error.what());
      return false;
    }
  }

  // Device selection is now complete - preferredDeviceId contains the selected device
  // No additional enumeration or validation needed since it was handled above

  // Stream parameters
  RtAudio::StreamParameters params;
  params.deviceId =
      preferredDeviceId; // Use found preferredDeviceId or default
  params.nChannels = channels;
  params.firstChannel = 0;

  // Options to optimize stability on Raspberry Pi Module 5
  RtAudio::StreamOptions options;
  options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME;

  // Buffer configuration optimized for Pi Module 5
#ifdef __ARM_ARCH
  options.numberOfBuffers = 12; // Increased for ARM (Pi Module 5) stability
  options.streamName = "Sp3ctra_Pi5_Optimized";
#else
  options.numberOfBuffers = 8; // Standard for x86/x64
  options.streamName = "Sp3ctra_Standard";
#endif

  // High priority for audio thread on Pi Module 5 - reduced for better compatibility
  options.priority = 70; // Real-time priority optimized for Pi permissions

  // Check device capabilities before opening
  try {
    RtAudio::DeviceInfo deviceInfo = audio->getDeviceInfo(preferredDeviceId);
    log_info("AUDIO", "Audio device: %s (ID: %u)", deviceInfo.name.c_str(), preferredDeviceId);

    // Check if configured frequency is supported
    bool supportsConfigRate = false;
    unsigned int configRate = g_sp3ctra_config.sampling_frequency;
    for (unsigned int rate : deviceInfo.sampleRates) {
      if (rate == configRate) {
        supportsConfigRate = true;
        break;
      }
    }

    if (!supportsConfigRate) {
      log_error("AUDIO", "Device does not support %uHz", configRate);
      return false;
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "Device query failed: %s", e.what());
    return false;
  }

  // Use SAMPLING_FREQUENCY from config.h instead of hard-coding 96kHz
  unsigned int configSampleRate = g_sp3ctra_config.sampling_frequency;
  if (sampleRate != configSampleRate) {
    log_info("AUDIO", "CONFIGURATION: Change from %uHz to %uHz (defined in config.h)", 
            sampleRate, configSampleRate);
    sampleRate = configSampleRate;
  }

  // Open audio stream with low latency options
  try {
    audio->openStream(&params, nullptr, g_selected_audio_format, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    // Check actually negotiated parameters
    if (audio->isStreamOpen()) {
      unsigned int actualSampleRate = audio->getStreamSampleRate();
      
      // Check for critical mismatches only
      if (bufferSize != (unsigned int)g_sp3ctra_config.audio_buffer_size) {
        log_error("AUDIO", "Buffer size mismatch - Config: %d frames, Hardware: %u frames", 
                 g_sp3ctra_config.audio_buffer_size, bufferSize);
        log_error("AUDIO", "Change audio_buffer_size to %u in sp3ctra.ini", bufferSize);
        return false;
      }

      if (actualSampleRate != configSampleRate) {
        log_error("AUDIO", "Sample rate mismatch - Requested: %uHz, Got: %uHz", 
                 configSampleRate, actualSampleRate);
        return false;
      }

      log_info("AUDIO", "Stream opened successfully: %uHz, %u frames", 
              actualSampleRate, bufferSize);
      
      // Initialize RT profiler with actual audio parameters
      rt_profiler_init(&g_rt_profiler, actualSampleRate, bufferSize);
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "RtAudio error: %s", e.what());
    delete audio;    // Clean up RtAudio object on failure
    audio = nullptr; // Set pointer to nullptr
    return false;
  }

  return true;
}

// Start audio stream
bool AudioSystem::start() {
  if (!audio || !audio->isStreamOpen())
    return false;

  try {
    audio->startStream();
  } catch (std::exception &e) {
    log_error("AUDIO", "RtAudio start error: %s", e.what());
    return false;
  }

  isRunning = true;
  return true;
}

// Stop audio stream
void AudioSystem::stop() {
  if (audio && audio->isStreamRunning()) {
    try {
      audio->stopStream();
    } catch (std::exception &e) {
      log_error("AUDIO", "RtAudio stop error: %s", e.what());
    }
    isRunning = false;
  }
}

// Check if system is active
bool AudioSystem::isActive() const { return audio && audio->isStreamRunning(); }

// Update audio data
bool AudioSystem::setAudioData(const float *data, size_t size) {
  if (!data || size == 0)
    return false;

  // Buffer access protection
  std::lock_guard<std::mutex> lock(bufferMutex);

  // Copy data to our buffer
  size_t copySize = std::min(size, processBuffer.size());
  std::memcpy(processBuffer.data(), data, copySize * sizeof(float));

  return true;
}

// Get list of available devices
std::vector<std::string> AudioSystem::getAvailableDevices() {
  std::vector<std::string> devices;

  if (!audio)
    return devices;

  unsigned int deviceCount = audio->getDeviceCount();
  for (unsigned int i = 0; i < deviceCount; i++) {
    RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
    if (info.outputChannels > 0) {
      devices.push_back(info.name);
    }
  }

  return devices;
}

// Change output device
bool AudioSystem::setDevice(unsigned int deviceId) {
  if (!audio)
    return false;

  // Must stop and restart stream to change device
  bool wasRunning = audio->isStreamRunning();
  if (wasRunning) {
    audio->stopStream();
  }

  if (audio->isStreamOpen()) {
    audio->closeStream();
  }

  // Stream parameters
  RtAudio::StreamParameters params;
  params.deviceId = deviceId;
  params.nChannels = channels;
  params.firstChannel = 0;

  // Options to optimize stability on Raspberry Pi
  RtAudio::StreamOptions options;
  options.flags =
      RTAUDIO_NONINTERLEAVED; // Suppression de RTAUDIO_MINIMIZE_LATENCY
  options.numberOfBuffers =
      8; // Increased from 4 to 8 for better stability on Pi

  // Ouvrir le flux audio avec les options de faible latence
  try {
    audio->openStream(&params, nullptr, RTAUDIO_FORMAT_TYPE, sampleRate,
                      &bufferSize, &AudioSystem::rtCallback, this, &options);

    if (wasRunning) {
      audio->startStream();
    }
  } catch (std::exception &e) {
    log_error("AUDIO", "Device change error: %s", e.what());
    return false;
  }

  return true;
}

// Get current device
unsigned int AudioSystem::getCurrentDevice() const {
  if (!audio || !audio->isStreamOpen())
    return 0;

  // No direct method to get current device
  // Return default device as workaround
  return audio->getDefaultOutputDevice();
}

// Modifier la taille du buffer (impact sur la latence)
bool AudioSystem::setBufferSize(unsigned int size) {
  // To change buffer size, must recreate stream
  if (size == bufferSize)
    return true;

  bufferSize = size;

  // Resize processing buffer
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    processBuffer.resize(bufferSize * channels);
  }

  // If stream is open, recreate it
  if (audio && audio->isStreamOpen()) {
    return setDevice(getCurrentDevice());
  }

  return true;
}

// Get buffer size
unsigned int AudioSystem::getBufferSize() const { return bufferSize; }

// Set master volume (0.0 - 1.0)
void AudioSystem::setMasterVolume(float volume) {
  // Clamp volume between 0.0 and 1.0
  masterVolume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
}

// Get master volume
float AudioSystem::getMasterVolume() const { return masterVolume; }

// === Reverb controls ===

// Enable/disable reverb
void AudioSystem::enableReverb(bool enable) {
  reverbEnabled = enable;
  log_info("AUDIO", "REVERB: %s", enable ? "ON" : "OFF");
}

// Check if reverb is enabled
bool AudioSystem::isReverbEnabled() const { return reverbEnabled; }

// Set dry/wet mix (0.0 - 1.0)
void AudioSystem::setReverbMix(float mix) {
  if (mix < 0.0f)
    reverbMix = 0.0f;
  else if (mix > 1.0f)
    reverbMix = 1.0f;
  else
    reverbMix = mix;

  // No more log here to avoid duplicates with colored logs from
  // midi_controller.cpp
}

// Get current dry/wet mix
float AudioSystem::getReverbMix() const { return reverbMix; }

// Set room size (0.0 - 1.0)
void AudioSystem::setReverbRoomSize(float size) {
  if (size < 0.0f)
    reverbRoomSize = 0.0f;
  else if (size > 1.0f)
    reverbRoomSize = 1.0f;
  else
    reverbRoomSize = size;

  // No more log here to avoid duplicates with colored logs from
  // midi_controller.cpp
}

// Get current room size
float AudioSystem::getReverbRoomSize() const { return reverbRoomSize; }

// Set damping (0.0 - 1.0)
void AudioSystem::setReverbDamping(float damping) {
  if (damping < 0.0f)
    reverbDamping = 0.0f;
  else if (damping > 1.0f)
    reverbDamping = 1.0f;
  else
    reverbDamping = damping;

  // No more log here to avoid duplicates with colored logs from
  // midi_controller.cpp
}

// Get current damping
float AudioSystem::getReverbDamping() const { return reverbDamping; }

// Set stereo width (0.0 - 1.0)
void AudioSystem::setReverbWidth(float width) {
  if (width < 0.0f)
    reverbWidth = 0.0f;
  else if (width > 1.0f)
    reverbWidth = 1.0f;
  else
    reverbWidth = width;

  // No more log here to avoid duplicates with colored logs from
  // midi_controller.cpp
}

// Get current stereo width
float AudioSystem::getReverbWidth() const { return reverbWidth; }

// Set requested device ID for initialization
void AudioSystem::setRequestedDeviceId(int deviceId) {
  requestedDeviceId = deviceId;
  log_info("AUDIO", "Audio device ID %d requested for initialization", deviceId);
}

// C functions for compatibility with existing code
extern "C" {

// Audio buffer management functions
void resetAudioDataBufferOffset(void) {
  // This function is no longer needed with RtAudio
  // But we keep it for compatibility
}

// Audio data initialization and cleanup
void initAudioData(AudioData *audioData, UInt32 numChannels,
                   UInt32 bufferSize) {
  if (audioData) {
    audioData->numChannels = numChannels;
    audioData->bufferSize = bufferSize;
    audioData->buffers = (Float32 **)malloc(numChannels * sizeof(Float32 *));

    for (UInt32 i = 0; i < numChannels; i++) {
      audioData->buffers[i] = (Float32 *)calloc(bufferSize, sizeof(Float32));
    }
  }
}

void audio_Init(void) {
  // Initialize buffers for compatibility
  for (int i = 0; i < 2; i++) {
    pthread_mutex_init(&buffers_L[i].mutex, NULL);
    pthread_mutex_init(&buffers_R[i].mutex, NULL);
    pthread_cond_init(&buffers_L[i].cond, NULL);
    pthread_cond_init(&buffers_R[i].cond, NULL);
    
    // CRITICAL: Initialize ready state atomically for RT-safe operation
    __atomic_store_n(&buffers_L[i].ready, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&buffers_R[i].ready, 0, __ATOMIC_SEQ_CST);

    // Allocate dynamic audio buffers based on runtime configuration
    if (!buffers_L[i].data) {
      buffers_L[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      // CRITICAL: Ensure buffers are zeroed (calloc should do this, but make sure)
      memset(buffers_L[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
    if (!buffers_R[i].data) {
      buffers_R[i].data = (float *)calloc(g_sp3ctra_config.audio_buffer_size, sizeof(float));
      // CRITICAL: Ensure buffers are zeroed (calloc should do this, but make sure)
      memset(buffers_R[i].data, 0, g_sp3ctra_config.audio_buffer_size * sizeof(float));
    }
  }
  
  // CRITICAL: Initialize buffer index atomically
  __atomic_store_n(&current_buffer_index, 0, __ATOMIC_SEQ_CST);
  
  log_info("AUDIO", "RT-safe audio buffers initialized with zero content and atomic ready states");

  // Create and initialize RtAudio audio system
  if (!gAudioSystem) {
    gAudioSystem = new AudioSystem();
    if (gAudioSystem) {
      gAudioSystem->initialize();
    }
  }

  // Initialize 3-band equalizer
  if (!gEqualizer) {
    float sampleRate = (gAudioSystem) ? g_sp3ctra_config.sampling_frequency : 44100.0f;
    eq_Init(sampleRate);
    log_info("AUDIO", "Three-band equalizer initialized");
  }
}

void cleanupAudioData(AudioData *audioData) {
  if (audioData && audioData->buffers) {
    for (UInt32 i = 0; i < audioData->numChannels; i++) {
      if (audioData->buffers[i]) {
        free(audioData->buffers[i]);
      }
    }
    free(audioData->buffers);
    audioData->buffers = nullptr;
  }
}

void audio_Cleanup() {
  // Cleanup mutexes and conditions
  for (int i = 0; i < 2; i++) {
    // Free dynamically allocated audio buffers
    if (buffers_L[i].data) {
      free(buffers_L[i].data);
      buffers_L[i].data = nullptr;
    }
    if (buffers_R[i].data) {
      free(buffers_R[i].data);
      buffers_R[i].data = nullptr;
    }
    pthread_mutex_destroy(&buffers_L[i].mutex);
    pthread_mutex_destroy(&buffers_R[i].mutex);
    pthread_cond_destroy(&buffers_L[i].cond);
    pthread_cond_destroy(&buffers_R[i].cond);
  }

  // Cleanup RtAudio system
  if (gAudioSystem) {
    delete gAudioSystem;
    gAudioSystem = nullptr;
  }

  // Cleanup equalizer
  if (gEqualizer) {
    eq_Cleanup();
  }
}

// Audio control functions
int startAudioUnit() {
  if (gAudioSystem) {
    return gAudioSystem->start() ? 0 : -1;
  }
  return -1;
}

void stopAudioUnit() {
  if (gAudioSystem) {
    gAudioSystem->stop();
  }
}

// List available audio devices with complete enumeration
void printAudioDevices() {
  if (!gAudioSystem || !gAudioSystem->getAudioDevice()) {
    printf("Système audio non initialisé\n");
    return;
  }

  RtAudio* audio = gAudioSystem->getAudioDevice();
  unsigned int deviceCount = 0;
  unsigned int defaultDevice = 0;
  
  // Get device count with error handling
  try {
    deviceCount = audio->getDeviceCount();
    defaultDevice = audio->getDefaultOutputDevice();
  } catch (const std::exception &error) {
    printf("❌ Error getting device count: %s\n", error.what());
    return;
  }

  printf("Available output devices:\n");
  printf("🔧 Complete device enumeration (verbose mode)...\n");
  printf("🔍 RtAudio reports %d total devices\n", deviceCount);
  
  std::vector<std::pair<unsigned int, std::string>> accessibleDevices;
  int failedDevices = 0;
  int emptyDevices = 0;
  
  // Enhanced enumeration with better error handling for macOS
  // First, always try to show the default device (which we know works)
  printf("🎯 Default device (ID %d):\n", defaultDevice);
  try {
    RtAudio::DeviceInfo defaultInfo = audio->getDeviceInfo(defaultDevice);
    if (defaultInfo.outputChannels > 0 && !defaultInfo.name.empty()) {
      accessibleDevices.push_back(std::make_pair(defaultDevice, defaultInfo.name));
      
      printf("📋 Device ID %d: %s (Default Output) [%d channels]\n", 
             defaultDevice, defaultInfo.name.c_str(), defaultInfo.outputChannels);
      
      // Show supported sample rates
      printf("    Sample rates: ");
      if (!defaultInfo.sampleRates.empty()) {
        for (size_t j = 0; j < defaultInfo.sampleRates.size(); j++) {
          printf("%dHz", defaultInfo.sampleRates[j]);
          if (j < defaultInfo.sampleRates.size() - 1) printf(", ");
        }
      } else {
        printf("None reported");
      }
      printf("\n");
      
      // Show supported formats
      printf("    Formats: ");
      bool hasFormats = false;
      if (defaultInfo.nativeFormats & RTAUDIO_SINT16) { printf("INT16 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_SINT24) { printf("INT24 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_SINT32) { printf("INT32 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_FLOAT32) { printf("FLOAT32 "); hasFormats = true; }
      if (defaultInfo.nativeFormats & RTAUDIO_FLOAT64) { printf("FLOAT64 "); hasFormats = true; }
      if (!hasFormats) printf("None reported");
      printf("\n");
    }
  } catch (const std::exception &error) {
    printf("❌ Default device ID %d: Query failed (%s)\n", defaultDevice, error.what());
  }
  
  printf("\n🔍 Scanning all reported device IDs (0-%d):\n", deviceCount - 1);
  
  // Then scan through the reported device range
  for (unsigned int i = 0; i < deviceCount; i++) {
    // Skip default device if we already processed it
    if (i == defaultDevice) {
      printf("ℹ️  Device ID %d: (Already shown as default device)\n", i);
      continue;
    }
    
    try {
      RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
      
      // Check if device has valid output channels
      if (info.outputChannels > 0) {
        // Check if device name is valid (not empty)
        if (!info.name.empty()) {
          accessibleDevices.push_back(std::make_pair(i, info.name));
          
          printf("📋 Device ID %d: %s [%d channels]\n", 
                 i, info.name.c_str(), info.outputChannels);
          
          // Show supported sample rates (with bounds checking)
          printf("    Sample rates: ");
          if (!info.sampleRates.empty()) {
            for (size_t j = 0; j < info.sampleRates.size(); j++) {
              printf("%dHz", info.sampleRates[j]);
              if (j < info.sampleRates.size() - 1) printf(", ");
            }
          } else {
            printf("None reported");
          }
          printf("\n");
          
          // Show supported formats
          printf("    Formats: ");
          bool hasFormats = false;
          if (info.nativeFormats & RTAUDIO_SINT16) { printf("INT16 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_SINT24) { printf("INT24 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_SINT32) { printf("INT32 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_FLOAT32) { printf("FLOAT32 "); hasFormats = true; }
          if (info.nativeFormats & RTAUDIO_FLOAT64) { printf("FLOAT64 "); hasFormats = true; }
          if (!hasFormats) printf("None reported");
          printf("\n");
          
          // Additional diagnostic info for macOS
          printf("    Input channels: %d, Duplex channels: %d\n", 
                 info.inputChannels, info.duplexChannels);
          if (info.isDefaultOutput) printf("    ✅ Marked as default output\n");
          if (info.isDefaultInput) printf("    ✅ Marked as default input\n");
          
        } else {
          emptyDevices++;
          printf("⚠️  Device ID %d: Has %d output channels but empty name\n", 
                 i, info.outputChannels);
        }
      } else {
        // Device exists but has no output channels - show minimal info
        if (!info.name.empty()) {
          printf("ℹ️  Device ID %d: %s [Input only - %d input channels]\n", 
                 i, info.name.c_str(), info.inputChannels);
        } else {
          printf("ℹ️  Device ID %d: Unnamed device [Input only - %d input channels]\n", 
                 i, info.inputChannels);
        }
      }
    } catch (const std::exception &error) {
      failedDevices++;
      printf("❌ Device ID %d: Query failed (%s)\n", i, error.what());
      
      // On macOS, this is often due to device enumeration issues
      // Continue with next device instead of stopping
      continue;
    }
  }
  
  printf("\n🎵 Summary: %zu accessible devices, %d failed queries, %d empty names\n", 
         accessibleDevices.size(), failedDevices, emptyDevices);
  
  if (accessibleDevices.empty()) {
    printf("❌ No accessible audio output devices found!\n");
    printf("💡 Troubleshooting for macOS:\n");
    printf("   1. Check System Preferences > Sound > Output\n");
    printf("   2. Try disconnecting/reconnecting USB audio devices\n");
    printf("   3. Restart Audio MIDI Setup application\n");
    printf("   4. Check if other audio applications are blocking access\n");
    printf("   5. Try running: sudo killall coreaudiod\n");
  } else {
    printf("✅ Use --audio-device=<ID> to select a specific device\n");
    printf("✅ Default device ID %d will be used if none specified\n", defaultDevice);
    
    // Show quick device list for easy reference
    printf("\n📋 Quick device reference:\n");
    for (const auto& device : accessibleDevices) {
      printf("   ID %d: %s%s\n", device.first, device.second.c_str(),
             (device.first == defaultDevice) ? " (default)" : "");
    }
  }
}

// Set active audio device
int setAudioDevice(unsigned int deviceId) {
  if (gAudioSystem) {
    // Set the requested device ID for next initialization
    gAudioSystem->setRequestedDeviceId((int)deviceId);
    return gAudioSystem->setDevice(deviceId) ? 1 : 0;
  }
  return 0;
}

void setRequestedAudioDevice(int deviceId) {
  if (gAudioSystem) {
    gAudioSystem->setRequestedDeviceId(deviceId);
  } else {
    // Store the device ID for when audio system gets created
    g_requested_audio_device_id = deviceId;
  }
}

void setRequestedAudioDeviceName(const char* deviceName) {
  // Store the device name for when audio system gets created
  if (g_requested_audio_device_name) {
    free(g_requested_audio_device_name);
  }
  g_requested_audio_device_name = strdup(deviceName);
  printf("Audio device name '%s' requested for initialization\n", deviceName);
}

// Control minimal callback mode for debugging audio dropouts
void setMinimalCallbackMode(int enabled) {
  use_minimal_callback = (enabled != 0);
  printf("🔧 Audio callback mode: %s\n",
         enabled ? "MINIMAL (440Hz test tone)" : "FULL (synth processing)");
}

void setMinimalTestVolume(float volume) {
  minimal_test_volume = (volume < 0.0f)   ? 0.0f
                        : (volume > 1.0f) ? 1.0f
                                          : volume;
  printf("🔊 Minimal test volume set to: %.2f\n", minimal_test_volume);
}

void setSynthLuxStralMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_luxstral_mix_level = level;
}

void setSynthLuxSynthMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_luxsynth_mix_level = level;
}

float getSynthLuxStralMixLevel(void) {
  return g_synth_luxstral_mix_level;
}

float getSynthLuxSynthMixLevel(void) {
  return g_synth_luxsynth_mix_level;
}

void setSynthLuxWaveMixLevel(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_synth_luxwave_mix_level = level;
}

float getSynthLuxWaveMixLevel(void) {
  return g_synth_luxwave_mix_level;
}

void setReverbSendLuxStral(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_reverb_send_luxstral = level;
}

float getReverbSendLuxStral(void) {
  return g_reverb_send_luxstral;
}

void setReverbSendLuxSynth(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_reverb_send_luxsynth = level;
}

float getReverbSendLuxSynth(void) {
  return g_reverb_send_luxsynth;
}

void setReverbSendLuxWave(float level) {
  // Clamp to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  
  // Volatile write (thread-safe for float on modern architectures)
  g_reverb_send_luxwave = level;
}

float getReverbSendLuxWave(void) {
  return g_reverb_send_luxwave;
}

} // extern "C"
