/*
 * rt_profiler.c
 *
 * Real-time performance profiler implementation
 *
 * Author: zhonx
 * Created: 2025-11-21
 */

#include "rt_profiler.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>

/* Helper function to calculate time difference in microseconds */
static inline uint64_t timeval_diff_us(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000000ULL + 
           (end->tv_usec - start->tv_usec);
}

void rt_profiler_init(RTProfiler *profiler, int sample_rate, int buffer_size) {
    memset(profiler, 0, sizeof(RTProfiler));
    
    profiler->sample_rate = sample_rate;
    profiler->buffer_size = buffer_size;
    profiler->enabled = 1;  /* Enabled by default */
    
    /* Calculate callback budget: (buffer_size / sample_rate) * 1,000,000 µs */
    profiler->callback_budget_us = ((uint64_t)buffer_size * 1000000ULL) / sample_rate;
    
    log_info("RT_PROFILER", "Initialized: %d Hz, %d frames, budget=%llu µs",
             sample_rate, buffer_size, profiler->callback_budget_us);
}

void rt_profiler_set_enabled(RTProfiler *profiler, int enabled) {
    profiler->enabled = enabled;
    if (enabled) {
        log_info("RT_PROFILER", "Profiling enabled");
    } else {
        log_info("RT_PROFILER", "Profiling disabled");
    }
}

void rt_profiler_callback_start(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    
    gettimeofday(&profiler->callback_start_time, NULL);
}

void rt_profiler_callback_end(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    
    uint64_t elapsed_us = timeval_diff_us(&profiler->callback_start_time, &end_time);
    
    profiler->callback_count++;
    profiler->total_callback_time_us += elapsed_us;
    
    if (elapsed_us > profiler->max_callback_time_us) {
        profiler->max_callback_time_us = elapsed_us;
    }
    
    /* Report stats periodically */
    if (profiler->callback_count % RT_PROFILER_REPORT_INTERVAL_FRAMES == 0) {
        rt_profiler_print_stats(profiler);
    }
    
    /* Warn on critical latency */
    float percent = (elapsed_us * 100.0f) / profiler->callback_budget_us;
    if (percent > RT_PROFILER_CRITICAL_LATENCY_PERCENT) {
        log_warning("RT_PROFILER", "CRITICAL latency: %llu µs (%.1f%% of budget)",
                    elapsed_us, percent);
    }
}

void rt_profiler_report_underrun(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    
    uint64_t count = atomic_fetch_add(&profiler->underrun_count, 1) + 1;
    
    /* Log every underrun (they should be rare) */
    log_error("RT_PROFILER", "UNDERRUN #%llu detected!", count);
}

void rt_profiler_report_buffer_miss_additive(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    atomic_fetch_add(&profiler->buffer_miss_additive, 1);
}

void rt_profiler_report_buffer_miss_polyphonic(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    atomic_fetch_add(&profiler->buffer_miss_polyphonic, 1);
}

void rt_profiler_report_buffer_miss_photowave(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    atomic_fetch_add(&profiler->buffer_miss_photowave, 1);
}

void rt_profiler_mutex_lock_start(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    
    profiler->mutex_lock_attempts++;
}

void rt_profiler_mutex_lock_end(RTProfiler *profiler, uint64_t wait_time_us) {
    if (!profiler->enabled) return;
    
    profiler->mutex_total_wait_us += wait_time_us;
    
    if (wait_time_us > profiler->mutex_max_wait_us) {
        profiler->mutex_max_wait_us = wait_time_us;
    }
    
    /* Warn on long waits */
    if (wait_time_us > RT_PROFILER_CRITICAL_MUTEX_WAIT_US) {
        log_warning("RT_PROFILER", "CRITICAL mutex wait: %llu µs", wait_time_us);
    } else if (wait_time_us > RT_PROFILER_WARN_MUTEX_WAIT_US) {
        log_warning("RT_PROFILER", "Long mutex wait: %llu µs", wait_time_us);
    }
}

void rt_profiler_mutex_contention(RTProfiler *profiler) {
    if (!profiler->enabled) return;
    
    profiler->mutex_contentions++;
    
    /* Log contentions (should be rare in well-designed RT code) */
    if (profiler->mutex_contentions % 100 == 1) {  /* Log first and every 100th */
        log_warning("RT_PROFILER", "Mutex contention #%llu", profiler->mutex_contentions);
    }
}

void rt_profiler_print_stats(RTProfiler *profiler) {
    if (!profiler->enabled || profiler->callback_count == 0) return;
    
    uint64_t avg_callback_us = profiler->total_callback_time_us / profiler->callback_count;
    float cpu_percent = rt_profiler_get_cpu_percent(profiler);
    
    uint64_t underruns = atomic_load(&profiler->underrun_count);
    uint64_t miss_add = atomic_load(&profiler->buffer_miss_additive);
    uint64_t miss_poly = atomic_load(&profiler->buffer_miss_polyphonic);
    uint64_t miss_photo = atomic_load(&profiler->buffer_miss_photowave);
    uint64_t miss_total = miss_add + miss_poly + miss_photo;
    
    log_info("RT_PROFILER", "=== Performance Stats (after %llu callbacks) ===", 
             profiler->callback_count);
    log_info("RT_PROFILER", "  Callback: avg=%llu µs, max=%llu µs, budget=%llu µs",
             avg_callback_us, profiler->max_callback_time_us, profiler->callback_budget_us);
    log_info("RT_PROFILER", "  CPU usage: %.1f%% of available time", cpu_percent);
    log_info("RT_PROFILER", "  Underruns: %llu total", underruns);
    
    /* Buffer miss stats */
    if (miss_total > 0) {
        float miss_rate_add = (miss_add * 100.0f) / profiler->callback_count;
        float miss_rate_poly = (miss_poly * 100.0f) / profiler->callback_count;
        float miss_rate_photo = (miss_photo * 100.0f) / profiler->callback_count;
        
        log_info("RT_PROFILER", "  Buffer miss: %llu total (%.2f%%)", miss_total,
                 (miss_total * 100.0f) / profiler->callback_count);
        log_info("RT_PROFILER", "    - Additive: %llu (%.2f%%)", miss_add, miss_rate_add);
        log_info("RT_PROFILER", "    - Polyphonic: %llu (%.2f%%)", miss_poly, miss_rate_poly);
        log_info("RT_PROFILER", "    - Photowave: %llu (%.2f%%)", miss_photo, miss_rate_photo);
    } else {
        log_info("RT_PROFILER", "  Buffer miss: 0 (0.00%%)");
    }
    
    /* Mutex stats */
    if (profiler->mutex_lock_attempts > 0) {
        uint64_t avg_mutex_wait = profiler->mutex_total_wait_us / profiler->mutex_lock_attempts;
        float contention_rate = (profiler->mutex_contentions * 100.0f) / profiler->mutex_lock_attempts;
        
        log_info("RT_PROFILER", "  Mutex: %llu locks, %.2f%% contention, avg wait=%llu µs, max=%llu µs",
                 profiler->mutex_lock_attempts, contention_rate, 
                 avg_mutex_wait, profiler->mutex_max_wait_us);
    }
    
    /* Health check */
    if (!rt_profiler_is_healthy(profiler)) {
        log_warning("RT_PROFILER", "⚠️  PERFORMANCE ISSUES DETECTED!");
    } else {
        log_info("RT_PROFILER", "✅ Performance is healthy");
    }
}

void rt_profiler_reset(RTProfiler *profiler) {
    uint64_t underruns = atomic_load(&profiler->underrun_count);
    
    profiler->callback_count = 0;
    profiler->total_callback_time_us = 0;
    profiler->max_callback_time_us = 0;
    atomic_store(&profiler->underrun_count, 0);
    atomic_store(&profiler->buffer_miss_additive, 0);
    atomic_store(&profiler->buffer_miss_polyphonic, 0);
    atomic_store(&profiler->buffer_miss_photowave, 0);
    profiler->mutex_lock_attempts = 0;
    profiler->mutex_contentions = 0;
    profiler->mutex_total_wait_us = 0;
    profiler->mutex_max_wait_us = 0;
    
    log_info("RT_PROFILER", "Stats reset (had %llu underruns)", underruns);
}

float rt_profiler_get_cpu_percent(RTProfiler *profiler) {
    if (profiler->callback_count == 0 || profiler->callback_budget_us == 0) {
        return 0.0f;
    }
    
    uint64_t avg_callback_us = profiler->total_callback_time_us / profiler->callback_count;
    return (avg_callback_us * 100.0f) / profiler->callback_budget_us;
}

int rt_profiler_is_healthy(RTProfiler *profiler) {
    if (!profiler->enabled || profiler->callback_count == 0) {
        return 1;  /* Assume healthy if not profiling */
    }
    
    /* Check CPU usage */
    float cpu_percent = rt_profiler_get_cpu_percent(profiler);
    if (cpu_percent > RT_PROFILER_CRITICAL_LATENCY_PERCENT) {
        return 0;  /* CPU usage too high */
    }
    
    /* Check underruns */
    uint64_t underruns = atomic_load(&profiler->underrun_count);
    if (underruns > 0) {
        return 0;  /* Any underruns = not healthy */
    }
    
    /* Check buffer miss rate */
    uint64_t miss_add = atomic_load(&profiler->buffer_miss_additive);
    uint64_t miss_poly = atomic_load(&profiler->buffer_miss_polyphonic);
    uint64_t miss_photo = atomic_load(&profiler->buffer_miss_photowave);
    uint64_t miss_total = miss_add + miss_poly + miss_photo;
    
    if (miss_total > 0) {
        float miss_rate = (miss_total * 100.0f) / profiler->callback_count;
        if (miss_rate > 2.0f) {  /* > 2% buffer miss rate */
            return 0;
        }
    }
    
    /* Check mutex contention */
    if (profiler->mutex_lock_attempts > 0) {
        float contention_rate = (profiler->mutex_contentions * 100.0f) / profiler->mutex_lock_attempts;
        if (contention_rate > 5.0f) {  /* > 5% contention */
            return 0;
        }
        
        uint64_t avg_mutex_wait = profiler->mutex_total_wait_us / profiler->mutex_lock_attempts;
        if (avg_mutex_wait > RT_PROFILER_WARN_MUTEX_WAIT_US) {
            return 0;  /* Average wait too long */
        }
    }
    
    return 1;  /* All checks passed */
}
