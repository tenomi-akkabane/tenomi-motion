/**
 * Production motion hub. Keep in lockstep with motion_train/detectors/hub.py.
 */

#include "motion_hub.h"

void motion_hub_init(motion_hub_t *hub, const stop_params_t *stop_p,
                     const motion_mlp_params_t *mlp_p)
{
  stop_init(&hub->stop, stop_p);
  motion_mlp_init(&hub->mlp, mlp_p);
}

void motion_hub_reset(motion_hub_t *hub)
{
  stop_reset(&hub->stop);
  motion_mlp_reset(&hub->mlp);
}

const char *motion_hub_update(motion_hub_t *hub, int hand, float conf,
                              const float xy[21][2], int64_t now_ms)
{
  const char *name;

  if (stop_update(&hub->stop, hand, conf, xy, now_ms)) {
    motion_mlp_start_cooldown(&hub->mlp, now_ms);
    return "stop";
  }
  name = motion_mlp_update(&hub->mlp, hand, conf, xy, now_ms);
  return name;
}
