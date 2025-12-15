/* display_buffer.c */

#include "display_buffer.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/**************************************************************************************
 * Helper Functions
 **************************************************************************************/

// Get current time in microseconds
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// Allocate memory for a scan line's data
static bool allocate_scan_line_data(ScanLine *line, int width) {
    line->r_data = (uint8_t*)malloc(width * sizeof(uint8_t));
    line->g_data = (uint8_t*)malloc(width * sizeof(uint8_t));
    line->b_data = (uint8_t*)malloc(width * sizeof(uint8_t));
    
    if (!line->r_data || !line->g_data || !line->b_data) {
        free(line->r_data);
        free(line->g_data);
        free(line->b_data);
        line->r_data = NULL;
        line->g_data = NULL;
        line->b_data = NULL;
        return false;
    }
    
    line->width = width;
    line->timestamp = 0;
    line->alpha = 1.0f;
    
    return true;
}

// Free memory for a scan line's data
static void free_scan_line_data(ScanLine *line) {
    if (line) {
        free(line->r_data);
        free(line->g_data);
        free(line->b_data);
        line->r_data = NULL;
        line->g_data = NULL;
        line->b_data = NULL;
    }
}

/**************************************************************************************
 * Public Functions
 **************************************************************************************/

DisplayBuffer* display_buffer_create(int capacity, int line_width) {
    if (capacity <= 0 || line_width <= 0) {
        log_error("DISPLAY_BUFFER", "Invalid parameters: capacity=%d, line_width=%d", 
                 capacity, line_width);
        return NULL;
    }
    
    DisplayBuffer *buffer = (DisplayBuffer*)calloc(1, sizeof(DisplayBuffer));
    if (!buffer) {
        log_error("DISPLAY_BUFFER", "Failed to allocate DisplayBuffer");
        return NULL;
    }
    
    buffer->lines = (ScanLine*)calloc(capacity, sizeof(ScanLine));
    if (!buffer->lines) {
        log_error("DISPLAY_BUFFER", "Failed to allocate scan lines array");
        free(buffer);
        return NULL;
    }
    
    // Allocate data for each scan line
    for (int i = 0; i < capacity; i++) {
        if (!allocate_scan_line_data(&buffer->lines[i], line_width)) {
            log_error("DISPLAY_BUFFER", "Failed to allocate scan line data at index %d", i);
            // Free previously allocated lines
            for (int j = 0; j < i; j++) {
                free_scan_line_data(&buffer->lines[j]);
            }
            free(buffer->lines);
            free(buffer);
            return NULL;
        }
    }
    
    buffer->capacity = capacity;
    buffer->line_width = line_width;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    
    log_info("DISPLAY_BUFFER", "Created buffer: capacity=%d, line_width=%d", 
             capacity, line_width);
    
    return buffer;
}

void display_buffer_destroy(DisplayBuffer *buffer) {
    if (!buffer) {
        return;
    }
    
    if (buffer->lines) {
        for (int i = 0; i < buffer->capacity; i++) {
            free_scan_line_data(&buffer->lines[i]);
        }
        free(buffer->lines);
    }
    
    free(buffer);
    log_info("DISPLAY_BUFFER", "Buffer destroyed");
}

bool display_buffer_add_line(DisplayBuffer *buffer,
                             const uint8_t *r_data,
                             const uint8_t *g_data,
                             const uint8_t *b_data) {
    if (!buffer || !r_data || !g_data || !b_data) {
        return false;
    }
    
    // Get the line at head position
    ScanLine *line = &buffer->lines[buffer->head];
    
    // Copy data
    memcpy(line->r_data, r_data, buffer->line_width * sizeof(uint8_t));
    memcpy(line->g_data, g_data, buffer->line_width * sizeof(uint8_t));
    memcpy(line->b_data, b_data, buffer->line_width * sizeof(uint8_t));
    
    // Set metadata
    line->timestamp = get_time_us();
    line->alpha = 1.0f;
    
    // Update head position
    buffer->head = (buffer->head + 1) % buffer->capacity;
    
    // Update count and tail
    if (buffer->count < buffer->capacity) {
        buffer->count++;
    } else {
        // Buffer is full, move tail forward (overwrite oldest)
        buffer->tail = (buffer->tail + 1) % buffer->capacity;
    }
    
    return true;
}

const ScanLine* display_buffer_get_line(const DisplayBuffer *buffer, int index) {
    if (!buffer || index < 0 || index >= buffer->count) {
        return NULL;
    }
    
    // Calculate actual position in circular buffer
    int pos = (buffer->tail + index) % buffer->capacity;
    return &buffer->lines[pos];
}

void display_buffer_update_alpha(DisplayBuffer *buffer,
                                float persistence_seconds,
                                float fade_strength,
                                float dt) {
    if (!buffer || buffer->count == 0) {
        return;
    }
    
    // If persistence is 0, all lines stay at full alpha
    if (persistence_seconds <= 0.0f) {
        for (int i = 0; i < buffer->count; i++) {
            int pos = (buffer->tail + i) % buffer->capacity;
            buffer->lines[pos].alpha = 1.0f;
        }
        return;
    }
    
    uint64_t current_time = get_time_us();
    uint64_t persistence_us = (uint64_t)(persistence_seconds * 1000000.0f);
    
    // Update alpha for each line based on age
    for (int i = 0; i < buffer->count; i++) {
        int pos = (buffer->tail + i) % buffer->capacity;
        ScanLine *line = &buffer->lines[pos];
        
        uint64_t age_us = current_time - line->timestamp;
        
        if (age_us >= persistence_us) {
            // Line has expired
            line->alpha = 0.0f;
        } else {
            // Calculate alpha based on age and fade strength
            float age_ratio = (float)age_us / (float)persistence_us;
            
            if (fade_strength > 0.0f) {
                // Apply fade: alpha decreases as line ages
                line->alpha = 1.0f - (age_ratio * fade_strength);
                if (line->alpha < 0.0f) line->alpha = 0.0f;
            } else {
                // No fade: full alpha until expiration
                line->alpha = 1.0f;
            }
        }
    }
}

void display_buffer_clear(DisplayBuffer *buffer) {
    if (!buffer) {
        return;
    }
    
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    
    log_info("DISPLAY_BUFFER", "Buffer cleared");
}

bool display_buffer_resize(DisplayBuffer *buffer, int new_capacity) {
    if (!buffer || new_capacity <= 0) {
        return false;
    }
    
    if (new_capacity == buffer->capacity) {
        return true; // No change needed
    }
    
    log_info("DISPLAY_BUFFER", "Resizing buffer from %d to %d lines", 
             buffer->capacity, new_capacity);
    
    // Allocate new lines array
    ScanLine *new_lines = (ScanLine*)calloc(new_capacity, sizeof(ScanLine));
    if (!new_lines) {
        log_error("DISPLAY_BUFFER", "Failed to allocate new lines array");
        return false;
    }
    
    // Allocate data for each new scan line
    for (int i = 0; i < new_capacity; i++) {
        if (!allocate_scan_line_data(&new_lines[i], buffer->line_width)) {
            log_error("DISPLAY_BUFFER", "Failed to allocate new scan line data");
            // Free previously allocated lines
            for (int j = 0; j < i; j++) {
                free_scan_line_data(&new_lines[j]);
            }
            free(new_lines);
            return false;
        }
    }
    
    // Copy existing lines to new buffer (keep most recent lines if shrinking)
    int lines_to_copy = (buffer->count < new_capacity) ? buffer->count : new_capacity;
    int start_index = (buffer->count > new_capacity) ? (buffer->count - new_capacity) : 0;
    
    for (int i = 0; i < lines_to_copy; i++) {
        const ScanLine *old_line = display_buffer_get_line(buffer, start_index + i);
        if (old_line) {
            memcpy(new_lines[i].r_data, old_line->r_data, buffer->line_width);
            memcpy(new_lines[i].g_data, old_line->g_data, buffer->line_width);
            memcpy(new_lines[i].b_data, old_line->b_data, buffer->line_width);
            new_lines[i].timestamp = old_line->timestamp;
            new_lines[i].alpha = old_line->alpha;
        }
    }
    
    // Free old lines
    for (int i = 0; i < buffer->capacity; i++) {
        free_scan_line_data(&buffer->lines[i]);
    }
    free(buffer->lines);
    
    // Update buffer
    buffer->lines = new_lines;
    buffer->capacity = new_capacity;
    buffer->count = lines_to_copy;
    buffer->head = lines_to_copy % new_capacity;
    buffer->tail = 0;
    
    log_info("DISPLAY_BUFFER", "Buffer resized successfully");
    
    return true;
}
