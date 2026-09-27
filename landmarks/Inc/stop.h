/**
 * Production stop rule (still open palm). Matches motion_train/detectors/stop.py.
 */

#ifndef STOP_H
#define STOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOP_NUM_LANDMARKS  21
#define STOP_WINDOW_MAX     32
#define STOP_INDEX_TIP      8
#define STOP_WRIST_INDEX    0
#define STOP_SCALE_INDEX    9

typedef struct {
  int window_frames;
  float min_conf;
  float min_pixel_scale;
  float max_norm_abs;
  float min_open_palm;
  float max_y_amp;
  float max_x_amp;
  int min_hold_ms;
  int cooldown_ms;
  float min_scale;
} stop_params_t;

typedef struct {
  stop_params_t params;
  float y[STOP_WINDOW_MAX];
  float x[STOP_WINDOW_MAX];
  float open_palm[STOP_WINDOW_MAX];
  int64_t t_ms[STOP_WINDOW_MAX];
  int len;
  int start;
  int64_t cooldown_until_ms;
  int cooldown_active;
  int64_t hold_start_ms;
  int hold_active;
} stop_detector_t;

void stop_params_default(stop_params_t *p);
void stop_init(stop_detector_t *det, const stop_params_t *p);
void stop_reset(stop_detector_t *det);
void stop_start_cooldown(stop_detector_t *det, int64_t now_ms);

/**
 * @return 1 when stop fires (one-shot + cooldown)
 */
int stop_update(stop_detector_t *det, int hand, float conf,
                const float xy[STOP_NUM_LANDMARKS][2], int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* STOP_H */
