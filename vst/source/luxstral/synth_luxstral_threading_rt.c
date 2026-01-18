/*
 * synth_luxstral_threading_rt.c
 *
 * Real-time deterministic threading extensions
 * Implements barrier synchronization and RT priorities
 *
 * Author: zhonx
 */

#include "vst_adapters_c.h"
#include "synth_luxstral_threading.h"
#include <errno.h>
#include <string.h>

#ifdef __linux__
#include <sched.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif

/* macOS barrier implementation (pthread_barrier not available) */
#ifndef __linux__

// Define PTHREAD_BARRIER_SERIAL_THREAD for macOS compatibility
#ifndef PTHREAD_BARRIER_SERIAL_THREAD
#define PTHREAD_BARRIER_SERIAL_THREAD -1
#endif

int barrier_init(barrier_t *barrier, int count) {
  if (count <= 0) {
    return EINVAL;
  }
  
  barrier->count = count;
  barrier->waiting = 0;
  barrier->generation = 0;
  
  if (pthread_mutex_init(&barrier->mutex, NULL) != 0) {
    return errno;
  }
  
  if (pthread_cond_init(&barrier->cond, NULL) != 0) {
    pthread_mutex_destroy(&barrier->mutex);
    return errno;
  }
  
  return 0;
}

int barrier_wait(barrier_t *barrier) {
  // 🔧 CRITICAL FIX: Check exit flag before waiting
  extern _Atomic int synth_workers_must_exit;
  extern _Atomic int synth_pool_shutdown;
  
  if (synth_workers_must_exit || synth_pool_shutdown) {
    return -1;  // Early exit - thread should terminate
  }
  
  pthread_mutex_lock(&barrier->mutex);
  
  // Check again under lock
  if (synth_workers_must_exit || synth_pool_shutdown) {
    pthread_mutex_unlock(&barrier->mutex);
    return -1;
  }
  
  int gen = barrier->generation;
  barrier->waiting++;
  
  if (barrier->waiting >= barrier->count) {
    // Last thread to arrive - wake everyone up
    barrier->waiting = 0;
    barrier->generation++;
    pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->mutex);
    return PTHREAD_BARRIER_SERIAL_THREAD;  // Special return for last thread
  }
  
  // Wait for all threads to arrive, but check exit flags on each wakeup
  while (gen == barrier->generation) {
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
    
    // 🔧 CRITICAL: Check exit flags after wakeup from broadcast
    if (synth_workers_must_exit || synth_pool_shutdown) {
      barrier->waiting--;  // Remove ourselves from waiting count
      pthread_mutex_unlock(&barrier->mutex);
      return -1;  // Early exit
    }
  }
  
  pthread_mutex_unlock(&barrier->mutex);
  return 0;
}

int barrier_destroy(barrier_t *barrier) {
  pthread_mutex_destroy(&barrier->mutex);
  pthread_cond_destroy(&barrier->cond);
  return 0;
}

#endif /* !__linux__ */

/**
 * @brief  Initialize barrier synchronization system
 * @param  num_threads Number of threads (workers + main thread)
 * @retval 0 on success, -1 on error
 */
int synth_init_barriers(int num_threads) {
#ifdef __linux__
  if (pthread_barrier_init(&g_worker_start_barrier, NULL, num_threads) != 0) {
    log_error("SYNTH_RT", "Failed to initialize start barrier");
    return -1;
  }
  if (pthread_barrier_init(&g_worker_end_barrier, NULL, num_threads) != 0) {
    log_error("SYNTH_RT", "Failed to initialize end barrier");
    pthread_barrier_destroy(&g_worker_start_barrier);
    return -1;
  }
#else
  if (barrier_init(&g_worker_start_barrier, num_threads) != 0) {
    log_error("SYNTH_RT", "Failed to initialize start barrier");
    return -1;
  }
  if (barrier_init(&g_worker_end_barrier, num_threads) != 0) {
    log_error("SYNTH_RT", "Failed to initialize end barrier");
    barrier_destroy(&g_worker_start_barrier);
    return -1;
  }
#endif
  
  log_info("SYNTH_RT", "Barrier synchronization initialized for %d threads", num_threads);
  return 0;
}

/**
 * @brief  Cleanup barrier synchronization system
 * @retval None
 */
void synth_cleanup_barriers(void) {
#ifdef __linux__
  pthread_barrier_destroy(&g_worker_start_barrier);
  pthread_barrier_destroy(&g_worker_end_barrier);
#else
  barrier_destroy(&g_worker_start_barrier);
  barrier_destroy(&g_worker_end_barrier);
#endif
  log_info("SYNTH_RT", "Barrier synchronization cleaned up");
}

/**
 * @brief  Set real-time priority for a thread
 * @param  thread Thread handle
 * @param  priority Priority level (1-99, higher = more priority)
 * @retval 0 on success, -1 on error
 */
int synth_set_rt_priority(pthread_t thread, int priority) {
#ifdef __linux__
  struct sched_param param;
  param.sched_priority = priority;
  
  int result = pthread_setschedparam(thread, SCHED_FIFO, &param);
  if (result != 0) {
    log_warning("SYNTH_RT", "Failed to set RT priority %d: %s (error %d)", 
                priority, strerror(result), result);
    log_warning("SYNTH_RT", "Note: RT priorities require CAP_SYS_NICE capability or rtprio limits");
    return -1;
  }
  
  log_info("SYNTH_RT", "Set RT priority %d (SCHED_FIFO)", priority);
  return 0;
  
#elif defined(__APPLE__)
  // macOS: Use Mach time-constraint policy for RT threads
  // This requires elevated privileges (sudo) but fails gracefully without them
  (void)priority; // Unused on macOS - uses time-constraint policy instead
  
  // Get the Mach thread from pthread
  mach_port_t mach_thread = pthread_mach_thread_np(thread);
  
  // Calculate time constraints based on audio buffer parameters
  // Assuming 48kHz sample rate, 128 frame buffer = 2.666ms period
  const uint64_t AUDIO_PERIOD_NS = 2666667;  // 2.666ms in nanoseconds
  
  // Convert nanoseconds to Mach absolute time units
  mach_timebase_info_data_t timebase;
  mach_timebase_info(&timebase);
  
  // Convert to Mach time units (depends on CPU frequency)
  uint32_t period_mach = (uint32_t)((AUDIO_PERIOD_NS * timebase.denom) / timebase.numer);
  
  // Set time constraints:
  // - period: how often the thread needs to run (our buffer duration)
  // - computation: max time the thread can use per period (60% of budget)
  // - constraint: deadline for completion (90% of budget)
  // - preemptible: allow interruption by higher priority threads
  thread_time_constraint_policy_data_t policy;
  policy.period      = period_mach;                    // 2.666ms
  policy.computation = (uint32_t)(period_mach * 0.6);  // 1.6ms max computation
  policy.constraint  = (uint32_t)(period_mach * 0.9);  // 2.4ms deadline
  policy.preemptible = TRUE;                           // Allow preemption
  
  kern_return_t result = thread_policy_set(
      mach_thread,
      THREAD_TIME_CONSTRAINT_POLICY,
      (thread_policy_t)&policy,
      THREAD_TIME_CONSTRAINT_POLICY_COUNT
  );
  
  if (result != KERN_SUCCESS) {
    // Graceful failure - app continues without RT priorities
    log_warning("SYNTH_RT", "Failed to set RT time-constraint policy (error %d)", result);
    log_info("SYNTH_RT", "RT priorities require elevated privileges (run with sudo)");
    log_info("SYNTH_RT", "Continuing without RT priorities - performance may vary");
    return -1;
  }
  
  log_info("SYNTH_RT", "✓ RT time-constraint policy enabled (period=%.2fms, computation=%.2fms, constraint=%.2fms)",
           AUDIO_PERIOD_NS / 1000000.0,
           (AUDIO_PERIOD_NS * 0.6) / 1000000.0,
           (AUDIO_PERIOD_NS * 0.9) / 1000000.0);
  return 0;
  
#else
  log_warning("SYNTH_RT", "RT priorities not supported on this platform");
  return -1;
#endif
}

/**
 * @brief  Wrapper for barrier wait (cross-platform)
 * @param  barrier Barrier to wait on
 * @retval 0 on success, PTHREAD_BARRIER_SERIAL_THREAD for last thread
 */
int synth_barrier_wait(void *barrier) {
#ifdef __linux__
  return pthread_barrier_wait((pthread_barrier_t*)barrier);
#else
  return barrier_wait((barrier_t*)barrier);
#endif
}
