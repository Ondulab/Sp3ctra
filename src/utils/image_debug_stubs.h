/* image_debug_stubs.h - Stubs for removed image_debug functions */
/* These functions were removed as part of core audio cleanup */
/* Stubs prevent compilation errors in synthesis code that still references them */

#ifndef IMAGE_DEBUG_STUBS_H
#define IMAGE_DEBUG_STUBS_H

#include <stdint.h>

// Stub functions - do nothing (image debug feature removed)
static inline void image_debug_init(void) {
  // No-op
}

static inline void image_debug_mark_new_image_boundary(void) {
  // No-op
}

static inline int image_debug_is_oscillator_capture_enabled(void) {
  return 0;  // Always disabled
}

static inline void image_debug_capture_volume_sample_fast(int note, float cur, float tgt) {
  (void)note;
  (void)cur;
  (void)tgt;
  // No-op
}

static inline void image_debug_capture_raw_scanner_line(uint8_t *r, uint8_t *g, uint8_t *b) {
  (void)r;
  (void)g;
  (void)b;
  // No-op
}

#endif /* IMAGE_DEBUG_STUBS_H */
