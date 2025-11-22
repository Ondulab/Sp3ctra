/*
 * synth_additive_threading.h
 *
 * Thread pool management for additive synthesis
 * Contains persistent thread pool and parallel processing functionality
 *
 * Author: zhonx
 */

#ifndef __SYNTH_ADDITIVE_THREADING_H__
#define __SYNTH_ADDITIVE_THREADING_H__

/* Includes ------------------------------------------------------------------*/
#include "../../core/config.h"
#include "../../config/config_synth_additive.h"
#include "../../config/config_instrument.h"  // For CIS_MAX_PIXELS_NB
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/time.h>

/* Forward declarations ------------------------------------------------------*/
struct DoubleBuffer;

/* Exported types ------------------------------------------------------------*/

/**
 * @brief  Structure for persistent thread pool worker optimized for synthesis
 */
typedef struct synth_thread_worker_s {
  int thread_id;      // Thread ID (0, 1, 2)
  int start_note;     // Start note for this thread
  int end_note;       // End note for this thread
  float *imageData;   // Input image data (shared, normalized float [0, 1])

  // Local output buffers per thread - Float32 (legacy)
  float *thread_additiveBuffer;
  float *thread_sumVolumeBuffer;
  float *thread_maxVolumeBuffer;
  
  // Stereo buffers for direct L/R accumulation (always present) - Float32
  // In mono mode: L = R = duplicated signal
  // In stereo mode: L and R with per-oscillator panning
  float *thread_additiveBuffer_L;
  float *thread_additiveBuffer_R;


  // Local work buffers (avoids VLA on stack) - Float32 (dynamically allocated)
  int32_t *imageBuffer_q31; // Dynamically allocated based on notes_per_thread
  float *imageBuffer_f32;   // Dynamically allocated based on notes_per_thread
  float *waveBuffer;
  float *volumeBuffer;

  // Temporary stereo work buffers (persistently allocated to avoid VLAs)
  float *temp_waveBuffer_L;
  float *temp_waveBuffer_R;
  

  // Pre-computed waves[] data (read-only)
  int32_t *precomputed_new_idx; // size: (notes_per_thread * g_sp3ctra_config.audio_buffer_size)
  float *precomputed_wave_data; // size: (notes_per_thread * g_sp3ctra_config.audio_buffer_size)
  float *precomputed_volume;    // Dynamically allocated based on notes_per_thread
  
  // Pre-computed pan positions and gains for each note (dynamically allocated)
  float *precomputed_pan_position;
  float *precomputed_left_gain;
  float *precomputed_right_gain;

  // Persistent last applied gains for per-buffer ramping (zipper-noise mitigation)
  float *last_left_gain;
  float *last_right_gain;

  // Debug capture: per-note per-sample volumes (current and target) for this buffer
  float *captured_current_volume; // size: (notes_per_thread * g_sp3ctra_config.audio_buffer_size)
  float *captured_target_volume; // size: (notes_per_thread * g_sp3ctra_config.audio_buffer_size)
  size_t capture_capacity_elements; // number of elements allocated across capture buffers; 0 when disabled

  // Per-worker timing instrumentation (captured inside worker thread)
  struct timeval worker_start_time;
  struct timeval worker_end_time;
  uint64_t worker_time_sum_us;
  uint64_t worker_time_max_us;
  int worker_timing_sample_count;

  // Synchronization (kept for shutdown signaling only)
  pthread_mutex_t work_mutex;
  pthread_cond_t work_cond;

} synth_thread_worker_t;

/* Barrier synchronization for deterministic execution (cross-platform) */
#ifdef __linux__
extern pthread_barrier_t g_worker_start_barrier;
extern pthread_barrier_t g_worker_end_barrier;
#else
// macOS doesn't have pthread_barrier, use custom implementation
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int count;
  int waiting;
  int generation;
} barrier_t;

extern barrier_t g_worker_start_barrier;
extern barrier_t g_worker_end_barrier;

int barrier_init(barrier_t *barrier, int count);
int barrier_wait(barrier_t *barrier);
int barrier_destroy(barrier_t *barrier);
#endif

extern volatile int g_use_barriers;  // Enable/disable barrier mode

/* Exported function prototypes ----------------------------------------------*/

/* Thread pool management */
int synth_init_thread_pool(void);
int synth_start_worker_threads(void);
void synth_shutdown_thread_pool(void);

/* RT deterministic threading (Phase 1 & 2) */
int synth_init_barriers(int num_threads);
void synth_cleanup_barriers(void);
int synth_set_rt_priority(pthread_t thread, int priority);
int synth_barrier_wait(void *barrier);

/* Thread processing functions */
void *synth_persistent_worker_thread(void *arg);
void synth_process_worker_range(synth_thread_worker_t *worker);
void synth_precompute_wave_data(float *imageData, struct DoubleBuffer *db);

/* Thread pool access for synthesis core */
#define MAX_WORKERS 8  // Maximum number of worker threads supported
extern synth_thread_worker_t *thread_pool;  // Dynamically allocated based on num_workers
extern pthread_t *worker_threads;           // Dynamically allocated based on num_workers
extern int num_workers;                     // Actual number of workers (from config)
extern volatile int synth_pool_initialized;
extern volatile int synth_pool_shutdown;

/* RT-safe double buffering system */
typedef struct {
  // Double buffers for RT-safe access
  float *buffers[2]; // [0] = current RT reads, [1] = workers write
  volatile int ready_buffer; // Which buffer is ready for RT (atomic read)
  volatile int worker_buffer; // Which buffer workers are writing to
  pthread_mutex_t swap_mutex; // Protects buffer swapping (non-RT thread only)
} rt_safe_buffer_t;

extern rt_safe_buffer_t g_rt_additive_buffer;
extern rt_safe_buffer_t g_rt_stereo_L_buffer;  
extern rt_safe_buffer_t g_rt_stereo_R_buffer;

/* RT-safe buffer management */
int init_rt_safe_buffers(void);
void cleanup_rt_safe_buffers(void);
void rt_safe_swap_buffers(void); // Called by workers when done (non-RT)

#endif /* __SYNTH_ADDITIVE_THREADING_H__ */
