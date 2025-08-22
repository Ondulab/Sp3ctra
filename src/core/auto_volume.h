#ifndef AUTO_VOLUME_H
#define AUTO_VOLUME_H

#include "context.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AutoVolume AutoVolume;

/* Global instance pointer (optional) */
extern AutoVolume *gAutoVolumeInstance;

/* Create/destroy */
AutoVolume *auto_volume_create(Context *ctx);
void auto_volume_destroy(AutoVolume *av);

/* Step the auto-volume controller. dt_ms = elapsed milliseconds since last
 * call. */
void auto_volume_step(AutoVolume *av, unsigned int dt_ms);

#ifdef __cplusplus
}
#endif

#endif /* AUTO_VOLUME_H */
