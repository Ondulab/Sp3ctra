#ifndef IMU_GESTURE_H
#define IMU_GESTURE_H

#include "../core/context.h"
#include <stdint.h>

/* Gesture state machine */
typedef enum {
    GESTURE_IDLE = 0,         /* No active gesture */
    GESTURE_ACTIVE = 1        /* Gesture in progress */
} GesturePhase;

/* IMU Gesture State - State machine for complete gesture detection */
typedef struct {
    /* Cursor position (0.0-1.0 normalized) */
    float cursor_x;           /* Horizontal position */
    float cursor_y;           /* Vertical position (fixed at 0.5) */
    float rotation_z;         /* Rotation Z in radians (unused for now) */
    
    /* Calibration state */
    int is_calibrated;        /* 0 = not calibrated, 1 = calibrated */
    float offset_accel_x;     /* Calibration offset for accel X */
    
    /* Gesture state machine */
    GesturePhase phase;       /* Current gesture phase */
    int gesture_direction;    /* Gesture direction: -1=left, 0=none, 1=right */
    int idle_frames;          /* Counter for consecutive idle frames */
    
    /* Configuration */
    float sensitivity_x;      /* X axis sensitivity multiplier */
    float dead_zone;          /* Dead zone threshold (m/s²) */
    
    /* Statistics */
    uint64_t update_count;    /* Number of updates processed */
    float last_dt;            /* Last delta time (seconds) */
} IMUGestureState;

/* Initialize IMU gesture system */
void imu_gesture_init(IMUGestureState *state);

/* Update gesture state from IMU data (call on UDP packet reception) */
void imu_gesture_update(IMUGestureState *state, Context *ctx, float dt);

/* Calibrate IMU (set current position as zero reference) */
void imu_gesture_calibrate(IMUGestureState *state, Context *ctx);

/* Reset cursor to center */
void imu_gesture_reset(IMUGestureState *state);

/* Render cursor for test mode (SFML) - REMOVED: SFML no longer supported */

/* Get cursor position in pixel coordinates */
void imu_gesture_get_pixel_coords(IMUGestureState *state, 
                                   unsigned int window_width,
                                   unsigned int window_height,
                                   int *out_x, int *out_y);

#endif /* IMU_GESTURE_H */
