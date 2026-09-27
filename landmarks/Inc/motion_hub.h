/**
 * Production motion hub: stop rule first, then MotionMLP.
 * Canonical: motion_train/detectors/hub.py (MotionHub)
 */

#ifndef MOTION_HUB_H
#define MOTION_HUB_H

#include <stdint.h>

#include "motion_mlp.h"
#include "stop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  stop_detector_t stop;
  motion_mlp_detector_t mlp;
} motion_hub_t;

void motion_hub_init(motion_hub_t *hub, const stop_params_t *stop_p,
                     const motion_mlp_params_t *mlp_p);
void motion_hub_reset(motion_hub_t *hub);

/**
 * @return event name ("stop", "come_here", …) or NULL.
 */
const char *motion_hub_update(motion_hub_t *hub, int hand, float conf,
                              const float xy[21][2], int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_HUB_H */
