/**
 * Production stop rule. Keep in lockstep with motion_train/detectors/stop.py
 * and motion_train/features.py (extract_hand_features).
 */

#include "stop.h"

#include <math.h>
#include <string.h>

static const int TIP_INDICES[4] = {8, 12, 16, 20};

void stop_params_default(stop_params_t *p)
{
  p->window_frames = 20;
  p->min_conf = 0.5f;
  p->min_pixel_scale = 8.0f;
  p->max_norm_abs = 8.0f;
  p->min_open_palm = 1.6f;
  p->max_y_amp = 0.25f;
  p->max_x_amp = 0.25f;
  p->min_hold_ms = 400;
  p->cooldown_ms = 1500;
  p->min_scale = 0.01f;
}

void stop_reset(stop_detector_t *det)
{
  det->len = 0;
  det->start = 0;
  det->cooldown_active = 0;
  det->cooldown_until_ms = 0;
  det->hold_active = 0;
  det->hold_start_ms = 0;
}

void stop_init(stop_detector_t *det, const stop_params_t *p)
{
  memset(det, 0, sizeof(*det));
  if (p)
    det->params = *p;
  else
    stop_params_default(&det->params);
  if (det->params.window_frames < 3)
    det->params.window_frames = 3;
  if (det->params.window_frames > STOP_WINDOW_MAX)
    det->params.window_frames = STOP_WINDOW_MAX;
  stop_reset(det);
}

void stop_start_cooldown(stop_detector_t *det, int64_t now_ms)
{
  det->cooldown_active = 1;
  det->cooldown_until_ms = now_ms + det->params.cooldown_ms;
}

static int in_cooldown(const stop_detector_t *det, int64_t now_ms)
{
  return det->cooldown_active && now_ms < det->cooldown_until_ms;
}

static void buf_push(stop_detector_t *det, float y, float x, float open_palm, int64_t t_ms)
{
  int cap = det->params.window_frames;
  int idx;

  if (det->len < cap) {
    idx = (det->start + det->len) % cap;
    det->len += 1;
  } else {
    idx = det->start;
    det->start = (det->start + 1) % cap;
  }
  det->y[idx] = y;
  det->x[idx] = x;
  det->open_palm[idx] = open_palm;
  det->t_ms[idx] = t_ms;
}

static float buf_at(const float *arr, const stop_detector_t *det, int i)
{
  int cap = det->params.window_frames;
  return arr[(det->start + i) % cap];
}

static int extract_features(const stop_detector_t *det,
                            const float xy[STOP_NUM_LANDMARKS][2],
                            float *out_y, float *out_x, float *out_open)
{
  float wx, wy, ax, ay, dx, dy, pixel_scale, scale;
  float ix, iy, index_x, index_y;
  float tip_sum = 0.0f;
  float max_abs = 0.0f;
  int i;

  for (i = 0; i < STOP_NUM_LANDMARKS; i++) {
    float axv = fabsf(xy[i][0]);
    float ayv = fabsf(xy[i][1]);
    if (!isfinite(xy[i][0]) || !isfinite(xy[i][1]))
      return 0;
    if (axv > max_abs)
      max_abs = axv;
    if (ayv > max_abs)
      max_abs = ayv;
  }
  if (max_abs > 2000.0f)
    return 0;

  wx = xy[STOP_WRIST_INDEX][0];
  wy = xy[STOP_WRIST_INDEX][1];
  ax = xy[STOP_SCALE_INDEX][0];
  ay = xy[STOP_SCALE_INDEX][1];
  dx = ax - wx;
  dy = ay - wy;
  pixel_scale = sqrtf(dx * dx + dy * dy);
  if (pixel_scale < det->params.min_pixel_scale)
    return 0;

  scale = pixel_scale;
  if (scale < det->params.min_scale)
    scale = det->params.min_scale;

  ix = xy[STOP_INDEX_TIP][0];
  iy = xy[STOP_INDEX_TIP][1];
  index_x = (ix - wx) / scale;
  index_y = (iy - wy) / scale;
  if (fabsf(index_y) > det->params.max_norm_abs || fabsf(index_x) > det->params.max_norm_abs)
    return 0;

  for (i = 0; i < 4; i++) {
    int tip = TIP_INDICES[i];
    float tdx = xy[tip][0] - wx;
    float tdy = xy[tip][1] - wy;
    tip_sum += sqrtf(tdx * tdx + tdy * tdy);
  }

  *out_y = index_y;
  *out_x = index_x;
  *out_open = (tip_sum / 4.0f) / scale;
  return 1;
}

static int still_open(const stop_detector_t *det)
{
  float y_min, y_max, x_min, x_max, open_sum;
  float y_amp, x_amp, open_mean;
  int i;

  if (det->len < 5)
    return 0;

  y_min = y_max = buf_at(det->y, det, 0);
  x_min = x_max = buf_at(det->x, det, 0);
  open_sum = buf_at(det->open_palm, det, 0);
  for (i = 1; i < det->len; i++) {
    float yv = buf_at(det->y, det, i);
    float xv = buf_at(det->x, det, i);
    float ov = buf_at(det->open_palm, det, i);
    if (yv < y_min)
      y_min = yv;
    if (yv > y_max)
      y_max = yv;
    if (xv < x_min)
      x_min = xv;
    if (xv > x_max)
      x_max = xv;
    open_sum += ov;
  }
  y_amp = y_max - y_min;
  x_amp = x_max - x_min;
  open_mean = open_sum / (float)det->len;
  return open_mean >= det->params.min_open_palm &&
         y_amp <= det->params.max_y_amp &&
         x_amp <= det->params.max_x_amp;
}

int stop_update(stop_detector_t *det, int hand, float conf,
                const float xy[STOP_NUM_LANDMARKS][2], int64_t now_ms)
{
  float y, x, open_palm;

  if (!hand || xy == NULL || conf < det->params.min_conf) {
    det->len = 0;
    det->start = 0;
    det->hold_active = 0;
    return 0;
  }

  if (!extract_features(det, xy, &y, &x, &open_palm))
    return 0;

  buf_push(det, y, x, open_palm, now_ms);

  if (in_cooldown(det, now_ms))
    return 0;
  if (!still_open(det)) {
    det->hold_active = 0;
    return 0;
  }
  if (!det->hold_active) {
    det->hold_active = 1;
    det->hold_start_ms = now_ms;
    return 0;
  }
  if (now_ms - det->hold_start_ms < det->params.min_hold_ms)
    return 0;

  stop_start_cooldown(det, now_ms);
  det->hold_active = 0;
  return 1;
}
