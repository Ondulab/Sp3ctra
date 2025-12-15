/* display_buffer.h */

#ifndef __DISPLAY_BUFFER_H__
#define __DISPLAY_BUFFER_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**************************************************************************************
 * Display Buffer - Circular buffer for scan line history
 **************************************************************************************/

// Single scan line with metadata
typedef struct {
    uint8_t *r_data;      // Red channel data
    uint8_t *g_data;      // Green channel data
    uint8_t *b_data;      // Blue channel data
    uint64_t timestamp;   // Creation time (microseconds)
    float alpha;          // Current opacity (for persistence/fade)
    int width;            // Line width in pixels
} ScanLine;

// Circular buffer for scan lines
typedef struct {
    ScanLine *lines;      // Array of scan lines
    int capacity;         // Maximum number of lines (history_buffer_size)
    int head;             // Write position (next line to write)
    int tail;             // Read position (oldest line)
    int count;            // Current number of lines in buffer
    int line_width;       // Width of each line in pixels
} DisplayBuffer;

/**************************************************************************************
 * Function Prototypes
 **************************************************************************************/

/**
 * Create a new display buffer
 * @param capacity Maximum number of lines to store
 * @param line_width Width of each line in pixels
 * @return Pointer to new DisplayBuffer, or NULL on failure
 */
DisplayBuffer* display_buffer_create(int capacity, int line_width);

/**
 * Destroy a display buffer and free all memory
 * @param buffer Buffer to destroy
 */
void display_buffer_destroy(DisplayBuffer *buffer);

/**
 * Add a new scan line to the buffer
 * @param buffer Display buffer
 * @param r_data Red channel data (will be copied)
 * @param g_data Green channel data (will be copied)
 * @param b_data Blue channel data (will be copied)
 * @return true on success, false on failure
 */
bool display_buffer_add_line(DisplayBuffer *buffer, 
                             const uint8_t *r_data,
                             const uint8_t *g_data,
                             const uint8_t *b_data);

/**
 * Get a scan line at a specific index (0 = oldest, count-1 = newest)
 * @param buffer Display buffer
 * @param index Index of line to retrieve
 * @return Pointer to ScanLine, or NULL if index out of range
 */
const ScanLine* display_buffer_get_line(const DisplayBuffer *buffer, int index);

/**
 * Update alpha values for all lines based on persistence settings
 * @param buffer Display buffer
 * @param persistence_seconds Line lifetime in seconds (0 = infinite)
 * @param fade_strength Fade effect strength (0.0-1.0)
 * @param dt Delta time since last update (seconds)
 */
void display_buffer_update_alpha(DisplayBuffer *buffer,
                                float persistence_seconds,
                                float fade_strength,
                                float dt);

/**
 * Clear all lines from the buffer
 * @param buffer Display buffer
 */
void display_buffer_clear(DisplayBuffer *buffer);

/**
 * Resize the buffer capacity
 * @param buffer Display buffer
 * @param new_capacity New maximum number of lines
 * @return true on success, false on failure
 */
bool display_buffer_resize(DisplayBuffer *buffer, int new_capacity);

/**
 * Get current number of lines in buffer
 * @param buffer Display buffer
 * @return Number of lines currently stored
 */
static inline int display_buffer_get_count(const DisplayBuffer *buffer) {
    return buffer ? buffer->count : 0;
}

/**
 * Get buffer capacity
 * @param buffer Display buffer
 * @return Maximum number of lines that can be stored
 */
static inline int display_buffer_get_capacity(const DisplayBuffer *buffer) {
    return buffer ? buffer->capacity : 0;
}

/**
 * Check if buffer is empty
 * @param buffer Display buffer
 * @return true if empty, false otherwise
 */
static inline bool display_buffer_is_empty(const DisplayBuffer *buffer) {
    return buffer ? (buffer->count == 0) : true;
}

/**
 * Check if buffer is full
 * @param buffer Display buffer
 * @return true if full, false otherwise
 */
static inline bool display_buffer_is_full(const DisplayBuffer *buffer) {
    return buffer ? (buffer->count >= buffer->capacity) : false;
}

#endif // __DISPLAY_BUFFER_H__
