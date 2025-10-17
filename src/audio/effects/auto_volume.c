/* auto_volume.c
 *
 * Auto-volume controller driven by IMU X axis values stored in Context.
 *
 * Comments and logs are in English to follow project conventions.
 */

#include "auto_volume.h"
#include "audio_c_interface.h" /* C interface for audio operations */
#include "config_loader.h"     /* Runtime configuration */
#include "config_synth_additive.h" /* For IMU_ACTIVE_THRESHOLD_X */
#include "../../synthesis/additive/synth_additive.h"    /* For synth_get_last_contrast_factor() */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* AutoVolume structure definition */
struct AutoVolume {
  Context *ctx;              /* Reference to global context */
  float auto_volume_current; /* Current volume level (0.0 to 1.0) */
  uint64_t last_call_ms;     /* Last time step was called */
};

AutoVolume *gAutoVolumeInstance = NULL;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

AutoVolume *auto_volume_create(Context *ctx) {
  if (!ctx)
    return NULL;
  AutoVolume *av = (AutoVolume *)calloc(1, sizeof(AutoVolume));
  if (!av)
    return NULL;
  av->ctx = ctx;
  av->auto_volume_current = 1.0f; // Always maximum volume when active
  av->last_call_ms = now_ms();
  gAutoVolumeInstance = av;
  /* Mirror initial state in Context for observability (protected by mutex) */
  pthread_mutex_lock(&ctx->imu_mutex);
  ctx->auto_volume_current = av->auto_volume_current;
  ctx->auto_volume_target = 1.0f; // Always maximum volume when active
  ctx->auto_last_activity_time = 0;
  ctx->auto_is_active = 1;
  pthread_mutex_unlock(&ctx->imu_mutex);
  return av;
}

void auto_volume_destroy(AutoVolume *av) {
  if (!av)
    return;
  if (gAutoVolumeInstance == av)
    gAutoVolumeInstance = NULL;
  free(av);
}

/* Step the controller. dt_ms is elapsed milliseconds since last call. */
void auto_volume_step(AutoVolume *av, unsigned int dt_ms) {
  if (!av || !av->ctx)
    return;

  /* Check if auto-volume is enabled in configuration */
  if (!g_sp3ctra_config.auto_volume_enabled) {
    return; // Auto-volume is disabled, do nothing
  }

  Context *ctx = av->ctx;

  /* Read IMU state under mutex quickly */
  float imu_x = 0.0f;
  int has = 0;

  pthread_mutex_lock(&ctx->imu_mutex);
  imu_x = ctx->imu_x_filtered;
  has = ctx->imu_has_value;
  pthread_mutex_unlock(&ctx->imu_mutex);

  /* Determine active state:
     - active if we have a filtered value above threshold
     - inactive only if below threshold for more than timeout seconds
  */
  int active = 1; // Default to active
  time_t last_activity_time = 0;

  // Get the last activity time from context
  pthread_mutex_lock(&ctx->imu_mutex);
  last_activity_time = ctx->auto_last_activity_time;
  pthread_mutex_unlock(&ctx->imu_mutex);

  /* CONTRAST CHANGE DETECTION: Real motion vs pure vibrations
   * Real motion = IMU movement + image contrast changes
   * Pure vibrations = IMU movement + stable contrast (fixed image)
   */
  float base_threshold = 0.010f / g_sp3ctra_config.imu_sensitivity;
  
  // Get current audio intensity via contrast factor (thread-safe)
  static float last_contrast = 0.0f;
  float contrast = synth_get_last_contrast_factor();
  float contrast_change = fabsf(contrast - last_contrast);
  last_contrast = contrast;
  
  // Detect activity: IMU above threshold
  int imu_active = (has && fabsf(imu_x) >= base_threshold);
  
  // Validate activity: check if contrast changed (image moving) OR sound is weak
  int activity_validated = 0;
  if (imu_active) {
    if (contrast < 0.3f) {
      // Sound is weak/moderate: trust IMU alone
      activity_validated = 1;
    } else if (contrast_change > g_sp3ctra_config.contrast_change_threshold) {
      // Sound is loud BUT contrast changed: real motion detected
      activity_validated = 1;
    }
    // else: IMU triggered but no contrast change + loud sound = vibration rejected
  }

  if (activity_validated) {
    // Real activity detected
    active = 1;
    // Update last activity time in context
    pthread_mutex_lock(&ctx->imu_mutex);
    ctx->auto_last_activity_time = time(NULL);
    pthread_mutex_unlock(&ctx->imu_mutex);
  } else if (has) {
    // Below threshold, check how long we've been inactive
    time_t current_time = time(NULL);
    double seconds_since_activity = difftime(current_time, last_activity_time);

    if (last_activity_time == 0) {
      // No previous activity recorded, consider as active initially
      active = 1;
      pthread_mutex_lock(&ctx->imu_mutex);
      ctx->auto_last_activity_time = current_time;
      pthread_mutex_unlock(&ctx->imu_mutex);
    } else if (seconds_since_activity > g_sp3ctra_config.imu_inactivity_timeout_s) {
      // Been inactive for more than timeout, set to inactive
      active = 0;
    } else {
      // Still within timeout period, remain active
      active = 1;
    }
  } else {
    // No IMU data at all, remain active (safe default)
    active = 1;
  }

#if AUTO_VOLUME_DISABLE_WITH_MIDI
  /* If a MIDI controller is connected and policy disables auto-volume,
     skip auto-volume adjustments. Note: For now, we skip this check
     as it requires C++ interface. Can be implemented later if needed. */
  /* TODO: Add C interface for MIDI controller status */
#endif

  float target = active ? 1.0f : g_sp3ctra_config.auto_volume_inactive_level;

  /* Exponential smoothing towards target using time constant tau =
   * AUTO_VOLUME_FADE_MS */
  float tau = (float)g_sp3ctra_config.auto_volume_fade_ms;
  float alpha;
  if (dt_ms == 0)
    alpha = 1.0f;
  else
    alpha = 1.0f - expf(-(float)dt_ms / fmaxf(1.0f, tau));

  av->auto_volume_current += (target - av->auto_volume_current) * alpha;

  /* Apply to audio system (non-blocking setter). If audio system is not yet
     initialized, value is still mirrored in Context. */
  if (audio_is_initialized()) {
    audio_set_master_volume(av->auto_volume_current);
  }

  /* Mirror state back into Context under mutex for observability */
  pthread_mutex_lock(&ctx->imu_mutex);
  ctx->auto_volume_current = av->auto_volume_current;
  ctx->auto_volume_target = target;
  ctx->auto_is_active = active;
  pthread_mutex_unlock(&ctx->imu_mutex);

#ifdef DEBUG_AUTO_VOLUME
  printf("[AUTO_VOL] imu_x=%.6f has=%d active=%d target=%.3f current=%.3f "
         "dt=%ums\n",
         imu_x, has, active, target, av->auto_volume_current, dt_ms);
#endif
}
