/*
 * lock_free_pan.h
 * Lock-free double buffering for stereo panning gains
 * Ensures thread-safe access without blocking audio threads
 */

#ifndef LOCK_FREE_PAN_H
#define LOCK_FREE_PAN_H

#include <stdatomic.h>
#include <stdint.h>
#include "config.h"
#include "../../synthesis/additive/synth_additive.h"  // For MAX_NUMBER_OF_NOTES

/* Lock-free pan gains structure using atomic pointers for zero-contention access */
typedef struct {
    /* Double buffers for panning gains (dynamically allocated) */
    float *left_gain_buffer_A;
    float *right_gain_buffer_A;
    float *pan_position_buffer_A;
    
    float *left_gain_buffer_B;
    float *right_gain_buffer_B;
    float *pan_position_buffer_B;
    
    /* Allocated buffer size */
    uint32_t buffer_size;
    
    /* Atomic pointers for lock-free swapping */
    _Atomic(float*) read_left_ptr;
    _Atomic(float*) read_right_ptr;
    _Atomic(float*) read_pan_ptr;
    
    /* Version counter for debugging and monitoring updates */
    _Atomic(uint32_t) version;
    
    /* Statistics for performance monitoring */
    _Atomic(uint64_t) update_count;
    _Atomic(uint64_t) read_count;
    
} LockFreePanGains;

/* Global instance (defined in .c file) */
extern LockFreePanGains g_lock_free_pan_gains;

/* Initialize the lock-free pan gains system */
void lock_free_pan_init(void);

/* Cleanup resources */
void lock_free_pan_cleanup(void);

/* Update pan gains from UDP thread (non-blocking write) */
void lock_free_pan_update(const float* new_left_gains, 
                          const float* new_right_gains,
                          const float* new_pan_positions,
                          uint32_t num_notes);

/* Read pan gains from audio thread (lock-free read) */
static inline void lock_free_pan_read(uint32_t note_index, 
                                      float* left_gain, 
                                      float* right_gain,
                                      float* pan_position) {
    /* Atomic load with acquire semantics for consistency */
    float* left_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_left_ptr, 
                                           memory_order_acquire);
    float* right_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_right_ptr, 
                                            memory_order_acquire);
    float* pan_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_pan_ptr, 
                                          memory_order_acquire);
    
    /* Direct array access - no locks, no waiting */
    *left_gain = left_ptr[note_index];
    *right_gain = right_ptr[note_index];
    if (pan_position) {
        *pan_position = pan_ptr[note_index];
    }
    
    /* Update read counter for monitoring */
    atomic_fetch_add_explicit(&g_lock_free_pan_gains.read_count, 1, 
                             memory_order_relaxed);
}

/* Batch read for thread pool pre-computation */
static inline void lock_free_pan_read_range(uint32_t start_note, 
                                           uint32_t end_note,
                                           float* left_gains_out,
                                           float* right_gains_out,
                                           float* pan_positions_out) {
    /* Single atomic load for consistency across the range */
    float* left_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_left_ptr, 
                                           memory_order_acquire);
    float* right_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_right_ptr, 
                                            memory_order_acquire);
    float* pan_ptr = atomic_load_explicit(&g_lock_free_pan_gains.read_pan_ptr, 
                                          memory_order_acquire);
    
    /* Batch copy for efficiency */
    for (uint32_t i = start_note; i < end_note; i++) {
        uint32_t local_idx = i - start_note;
        left_gains_out[local_idx] = left_ptr[i];
        right_gains_out[local_idx] = right_ptr[i];
        if (pan_positions_out) {
            pan_positions_out[local_idx] = pan_ptr[i];
        }
    }
}

/* Get current version for debugging */
static inline uint32_t lock_free_pan_get_version(void) {
    return atomic_load_explicit(&g_lock_free_pan_gains.version, 
                               memory_order_relaxed);
}

/* Get statistics for performance monitoring */
void lock_free_pan_get_stats(uint64_t* update_count, uint64_t* read_count);

#endif /* LOCK_FREE_PAN_H */
