/*
 * synth_additive_threading_rt.c
 *
 * Real-time deterministic threading extensions
 * Implements barrier synchronization and RT priorities
 *
 * Author: zhonx
 */

#include "synth_additive_threading.h"
#include "../../utils/logger.h"
#include <errno.h>
#include <string.h>

#ifdef __linux__
#include <sched.h>
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
  pthread_mutex_lock(&barrier->mutex);
  
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
  
  // Wait for all threads to arrive
  while (gen == barrier->generation) {
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
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
#else
  // macOS: Use time-constraint policy for RT threads
  // This requires elevated privileges
  log_warning("SYNTH_RT", "RT priorities not fully supported on macOS");
  log_info("SYNTH_RT", "Consider using sudo or adjusting thread QoS");
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
