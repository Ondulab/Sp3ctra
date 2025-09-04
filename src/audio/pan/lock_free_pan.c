/*
 * lock_free_pan.c
 * Implementation of lock-free double buffering for stereo panning
 */

#include "lock_free_pan.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Global instance */
LockFreePanGains g_lock_free_pan_gains;

/* Static write buffers tracking */
static int current_write_buffer = 0; /* 0 = A, 1 = B */

/**
 * @brief Initialize the lock-free pan gains system
 * Sets up double buffers and atomic pointers
 */
void lock_free_pan_init(void) {
    /* Initialize all buffers with center pan (0.707 for equal power) */
    const float center_gain = 0.707f; /* -3dB for center position */
    
    for (uint32_t i = 0; i < NUMBER_OF_NOTES; i++) {
        /* Buffer A initialization */
        g_lock_free_pan_gains.left_gain_buffer_A[i] = center_gain;
        g_lock_free_pan_gains.right_gain_buffer_A[i] = center_gain;
        g_lock_free_pan_gains.pan_position_buffer_A[i] = 0.0f; /* Center */
        
        /* Buffer B initialization */
        g_lock_free_pan_gains.left_gain_buffer_B[i] = center_gain;
        g_lock_free_pan_gains.right_gain_buffer_B[i] = center_gain;
        g_lock_free_pan_gains.pan_position_buffer_B[i] = 0.0f; /* Center */
    }
    
    /* Initialize atomic pointers - Buffer A for reading initially */
    atomic_store_explicit(&g_lock_free_pan_gains.read_left_ptr, 
                         g_lock_free_pan_gains.left_gain_buffer_A,
                         memory_order_release);
    atomic_store_explicit(&g_lock_free_pan_gains.read_right_ptr, 
                         g_lock_free_pan_gains.right_gain_buffer_A,
                         memory_order_release);
    atomic_store_explicit(&g_lock_free_pan_gains.read_pan_ptr, 
                         g_lock_free_pan_gains.pan_position_buffer_A,
                         memory_order_release);
    
    /* Initialize counters */
    atomic_store_explicit(&g_lock_free_pan_gains.version, 0, memory_order_relaxed);
    atomic_store_explicit(&g_lock_free_pan_gains.update_count, 0, memory_order_relaxed);
    atomic_store_explicit(&g_lock_free_pan_gains.read_count, 0, memory_order_relaxed);
    
    /* Start with buffer B for writing */
    current_write_buffer = 1;
    
    printf("🔧 LOCK_FREE_PAN: Initialized with %d notes, center pan\n", NUMBER_OF_NOTES);
}

/**
 * @brief Cleanup resources (currently no dynamic allocation)
 */
void lock_free_pan_cleanup(void) {
    /* No dynamic memory to free in current implementation */
    printf("🔧 LOCK_FREE_PAN: Cleanup complete\n");
}

/**
 * @brief Update pan gains from UDP thread (non-blocking write)
 * Writes to the inactive buffer then atomically swaps pointers
 * 
 * @param new_left_gains Array of left channel gains
 * @param new_right_gains Array of right channel gains  
 * @param new_pan_positions Array of pan positions (-1 to +1)
 * @param num_notes Number of notes to update
 */
void lock_free_pan_update(const float* new_left_gains, 
                          const float* new_right_gains,
                          const float* new_pan_positions,
                          uint32_t num_notes) {
    
    /* Validate input */
    if (!new_left_gains || !new_right_gains || !new_pan_positions || num_notes == 0) {
        return;
    }
    
    /* Clamp to maximum notes */
    if (num_notes > NUMBER_OF_NOTES) {
        num_notes = NUMBER_OF_NOTES;
    }
    
    /* Select write buffers based on current state */
    float* write_left;
    float* write_right;
    float* write_pan;
    
    if (current_write_buffer == 0) {
        /* Writing to buffer A */
        write_left = g_lock_free_pan_gains.left_gain_buffer_A;
        write_right = g_lock_free_pan_gains.right_gain_buffer_A;
        write_pan = g_lock_free_pan_gains.pan_position_buffer_A;
    } else {
        /* Writing to buffer B */
        write_left = g_lock_free_pan_gains.left_gain_buffer_B;
        write_right = g_lock_free_pan_gains.right_gain_buffer_B;
        write_pan = g_lock_free_pan_gains.pan_position_buffer_B;
    }
    
    /* Copy new values to write buffer */
    memcpy(write_left, new_left_gains, num_notes * sizeof(float));
    memcpy(write_right, new_right_gains, num_notes * sizeof(float));
    memcpy(write_pan, new_pan_positions, num_notes * sizeof(float));
    
    /* Memory barrier to ensure writes are complete before swap */
    atomic_thread_fence(memory_order_release);
    
    /* Atomic pointer swap - this is the magic moment! */
    atomic_store_explicit(&g_lock_free_pan_gains.read_left_ptr, 
                         write_left,
                         memory_order_release);
    atomic_store_explicit(&g_lock_free_pan_gains.read_right_ptr, 
                         write_right,
                         memory_order_release);
    atomic_store_explicit(&g_lock_free_pan_gains.read_pan_ptr, 
                         write_pan,
                         memory_order_release);
    
    /* Toggle write buffer for next update */
    current_write_buffer = 1 - current_write_buffer;
    
    /* Update version and statistics */
    atomic_fetch_add_explicit(&g_lock_free_pan_gains.version, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_lock_free_pan_gains.update_count, 1, memory_order_relaxed);
    
    /* Debug output (limited frequency) */
    static uint32_t debug_counter = 0;
    if (++debug_counter % 100 == 0) {
        printf("🔄 LOCK_FREE_PAN: Update #%llu, version %u, buffer %c\n",
               (unsigned long long)atomic_load(&g_lock_free_pan_gains.update_count),
               atomic_load(&g_lock_free_pan_gains.version),
               current_write_buffer ? 'B' : 'A');
    }
}

/**
 * @brief Get statistics for performance monitoring
 * 
 * @param update_count Output: number of updates performed
 * @param read_count Output: number of reads performed
 */
void lock_free_pan_get_stats(uint64_t* update_count, uint64_t* read_count) {
    if (update_count) {
        *update_count = atomic_load_explicit(&g_lock_free_pan_gains.update_count, 
                                            memory_order_relaxed);
    }
    if (read_count) {
        *read_count = atomic_load_explicit(&g_lock_free_pan_gains.read_count, 
                                          memory_order_relaxed);
    }
}
