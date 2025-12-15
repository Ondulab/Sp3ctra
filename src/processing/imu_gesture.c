//
//  imu_gesture.c
//  IMU Gesture Processing - Direct acceleration-based cursor control
//
//  Simplified approach: cursor moves proportionally to instantaneous acceleration
//  No acceleration = no movement (like a joystick)
//

#include "imu_gesture.h"
#include "../utils/logger.h"
#include <math.h>
#include <string.h>

#ifndef NO_SFML
#include <SFML/Graphics.h>
#endif

/* Default configuration values */
#define DEFAULT_SENSITIVITY_X 0.20f   /* X axis sensitivity (increased for better response) */
#define DEFAULT_DEAD_ZONE 0.15f       /* Dead zone threshold (m/s²) */
#define IDLE_FRAMES_THRESHOLD 45      /* Number of idle frames to end gesture (~0.7s at 60Hz) */

/* Physical constants */
#define GRAVITY 9.81f                 /* Earth gravity (m/s²) */
#define DEG_TO_RAD (M_PI / 180.0f)
#define RAD_TO_DEG (180.0f / M_PI)

/* Initialize IMU gesture system */
void imu_gesture_init(IMUGestureState *state) {
    if (!state) return;
    
    memset(state, 0, sizeof(IMUGestureState));
    
    /* Set cursor to center */
    state->cursor_x = 0.5f;
    state->cursor_y = 0.5f;
    state->rotation_z = 0.0f;
    
    /* Default configuration */
    state->sensitivity_x = DEFAULT_SENSITIVITY_X;
    state->dead_zone = DEFAULT_DEAD_ZONE;
    
    /* Initialize gesture state machine */
    state->phase = GESTURE_IDLE;
    state->gesture_direction = 0;
    state->idle_frames = 0;
    
    state->is_calibrated = 0;
    
    log_info("IMU_GESTURE", "Initialized - State machine gesture detection");
    log_info("IMU_GESTURE", "Config: sensitivity=%.3f, dead_zone=%.2f m/s²", 
             state->sensitivity_x, state->dead_zone);
    log_info("IMU_GESTURE", "Mode: Only initial acceleration moves cursor");
}

/* Calibrate IMU (set current acceleration as zero reference) */
void imu_gesture_calibrate(IMUGestureState *state, Context *ctx) {
    if (!state || !ctx) return;
    
    pthread_mutex_lock(&ctx->imu_mutex);
    
    /* Store current X acceleration as offset (bias removal) */
    state->offset_accel_x = ctx->imu_raw_x;
    
    /* Reset cursor to center */
    state->cursor_x = 0.5f;
    state->cursor_y = 0.5f;
    state->rotation_z = 0.0f;
    
    state->is_calibrated = 1;
    
    pthread_mutex_unlock(&ctx->imu_mutex);
    
    log_info("IMU_GESTURE", "Calibrated - Offset X: %.2f m/s²", state->offset_accel_x);
}

/* Reset cursor to center */
void imu_gesture_reset(IMUGestureState *state) {
    if (!state) return;
    
    state->cursor_x = 0.5f;
    state->cursor_y = 0.5f;
    state->rotation_z = 0.0f;
    
    log_info("IMU_GESTURE", "Reset cursor to center");
}

/* Apply dead zone to value */
static inline float apply_dead_zone(float value, float threshold) {
    if (fabsf(value) < threshold) {
        return 0.0f;
    }
    return value;
}

/* Clamp value to range [min, max] */
static inline float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* Update gesture state from IMU data */
void imu_gesture_update(IMUGestureState *state, Context *ctx, float dt) {
    if (!state || !ctx || dt <= 0.0f) return;
    
    pthread_mutex_lock(&ctx->imu_mutex);
    
    /* Check if IMU data is available */
    if (!ctx->imu_has_value) {
        pthread_mutex_unlock(&ctx->imu_mutex);
        return;
    }
    
    /* Auto-calibrate on first update */
    if (!state->is_calibrated) {
        pthread_mutex_unlock(&ctx->imu_mutex);
        imu_gesture_calibrate(state, ctx);
        pthread_mutex_lock(&ctx->imu_mutex);
    }
    
    /* Read raw acceleration from sensor (already in m/s²) */
    /* AXIS CORRECTION: IMU is mounted UPSIDE DOWN */
    /* Invert X axis to correct orientation */
    float accel_x_raw = -ctx->imu_raw_x;
    
    pthread_mutex_unlock(&ctx->imu_mutex);
    
    /* === STATE MACHINE GESTURE DETECTION === */
    
    /* Remove calibration offset (DC bias) */
    float accel_x = accel_x_raw - state->offset_accel_x;
    
    /* Apply dead zone to filter out noise and micro-movements */
    accel_x = apply_dead_zone(accel_x, state->dead_zone);
    
    /* Determine current acceleration direction */
    int accel_sign = 0;
    if (accel_x > 0.0f) accel_sign = 1;       /* Positive = right */
    else if (accel_x < 0.0f) accel_sign = -1; /* Negative = left */
    
    int should_move = 0;
    
    /* State machine logic */
    switch (state->phase) {
        case GESTURE_IDLE:
            /* Waiting for new gesture to start */
            if (accel_sign != 0) {
                /* Acceleration detected → start new gesture */
                state->phase = GESTURE_ACTIVE;
                state->gesture_direction = accel_sign;
                state->idle_frames = 0;
                should_move = 1;  /* Move on initial acceleration */
            }
            break;
            
        case GESTURE_ACTIVE:
            /* Gesture in progress - direction is LOCKED */
            if (accel_sign == 0) {
                /* No acceleration → count idle frames */
                state->idle_frames++;
                if (state->idle_frames >= IDLE_FRAMES_THRESHOLD) {
                    /* End of gesture → return to idle */
                    state->phase = GESTURE_IDLE;
                    state->gesture_direction = 0;
                    state->idle_frames = 0;
                }
            } else if (accel_sign == state->gesture_direction) {
                /* Acceleration in SAME direction as gesture → move cursor */
                state->idle_frames = 0;
                should_move = 1;
            } else {
                /* Acceleration in OPPOSITE direction → deceleration phase */
                /* CRITICAL: Reset idle counter - we're still in dynamic motion! */
                /* The deceleration is part of the gesture, not the end of it */
                state->idle_frames = 0;
                /* Do NOT move cursor, do NOT change direction, STAY in ACTIVE */
            }
            break;
    }
    
    /* Move cursor only during valid acceleration phase */
    if (should_move) {
        state->cursor_x += accel_x * state->sensitivity_x * dt;
    }
    
    /* Clamp cursor position to [0.0, 1.0] */
    state->cursor_x = clamp(state->cursor_x, 0.0f, 1.0f);
    
    /* Y axis: keep centered (not used) */
    state->cursor_y = 0.5f;
    
    /* Update statistics */
    state->update_count++;
    state->last_dt = dt;
    
    /* Logging every 60 updates (~1 second at 60fps) */
    if (state->update_count % 60 == 0) {
        const char *phase_str = (state->phase == GESTURE_IDLE) ? "IDLE" : "ACTIVE";
        const char *dir_str = "NONE";
        if (state->gesture_direction == 1) dir_str = "RIGHT";
        else if (state->gesture_direction == -1) dir_str = "LEFT";
        
        log_info("IMU_GESTURE", "=== STATE MACHINE GESTURE DETECTION ===");
        log_info("IMU_GESTURE", "Accel X RAW: %.2f m/s²", accel_x_raw);
        log_info("IMU_GESTURE", "Accel X (bias removed): %.2f m/s²", accel_x);
        log_info("IMU_GESTURE", "Phase: %s, Direction: %s, Idle frames: %d", 
                 phase_str, dir_str, state->idle_frames);
        log_info("IMU_GESTURE", "Moving: %s", should_move ? "YES" : "NO");
        log_info("IMU_GESTURE", "Cursor X: %.3f (Y fixed at 0.5)", state->cursor_x);
    }
}

/* Get cursor position in pixel coordinates */
void imu_gesture_get_pixel_coords(IMUGestureState *state, 
                                   unsigned int window_width,
                                   unsigned int window_height,
                                   int *out_x, int *out_y) {
    if (!state || !out_x || !out_y) return;
    
    *out_x = (int)(state->cursor_x * window_width);
    *out_y = (int)(state->cursor_y * window_height);
    
    /* Clamp to window bounds */
    if (*out_x < 0) *out_x = 0;
    if (*out_x >= (int)window_width) *out_x = window_width - 1;
    if (*out_y < 0) *out_y = 0;
    if (*out_y >= (int)window_height) *out_y = window_height - 1;
}

#ifndef NO_SFML
/* Render cursor for test mode */
void imu_gesture_render_cursor(sfRenderWindow *window, IMUGestureState *state) {
    if (!window || !state) return;
    
    sfVector2u window_size = sfRenderWindow_getSize(window);
    
    /* Get pixel coordinates */
    int cursor_x, cursor_y;
    imu_gesture_get_pixel_coords(state, window_size.x, window_size.y, &cursor_x, &cursor_y);
    
    /* Draw crosshair */
    sfVertex crosshair_h[2] = {
        {{cursor_x - 20.0f, cursor_y}, sfRed, {0, 0}},
        {{cursor_x + 20.0f, cursor_y}, sfRed, {0, 0}}
    };
    sfVertex crosshair_v[2] = {
        {{cursor_x, cursor_y - 20.0f}, sfRed, {0, 0}},
        {{cursor_x, cursor_y + 20.0f}, sfRed, {0, 0}}
    };
    
    sfRenderWindow_drawPrimitives(window, crosshair_h, 2, sfLines, NULL);
    sfRenderWindow_drawPrimitives(window, crosshair_v, 2, sfLines, NULL);
    
    /* Draw circle at cursor */
    sfCircleShape *circle = sfCircleShape_create();
    sfCircleShape_setRadius(circle, 15.0f);
    sfCircleShape_setFillColor(circle, (sfColor){255, 0, 0, 100});
    sfCircleShape_setOutlineColor(circle, sfRed);
    sfCircleShape_setOutlineThickness(circle, 3.0f);
    sfCircleShape_setOrigin(circle, (sfVector2f){15.0f, 15.0f});
    sfCircleShape_setPosition(circle, (sfVector2f){cursor_x, cursor_y});
    
    sfRenderWindow_drawCircleShape(window, circle, NULL);
    
    sfCircleShape_destroy(circle);
}
#endif /* NO_SFML */
