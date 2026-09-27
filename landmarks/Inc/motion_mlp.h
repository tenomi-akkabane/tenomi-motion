/**
 * MotionMLP streaming detector (CPU float32).
 * Canonical: motion_train/motion_mlp/infer.py + normalize.py
 */

#ifndef MOTION_MLP_H
#define MOTION_MLP_H

#include <stdint.h>

#include "motion_mlp_weights.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int window_frames;
  float min_softmax;
  float min_conf;
  float min_pixel_scale;
  float max_norm_abs;
  int cooldown_ms;
  float min_scale;
  int wrist_index;
  int scale_index;
  int landmark_decimals; /* 1 = round to 0.1 px like DK JSON */
} motion_mlp_params_t;

typedef struct {
  const float *w0;
  const float *b0;
  const float *w1;
  const float *b1;
  const float *w2;
  const float *b2;
  const char *names[MOTION_MLP_NUM_CLASSES];
  int ready;
} motion_mlp_model_t;

typedef struct {
  motion_mlp_params_t params;
  motion_mlp_model_t model;
  float feats[MOTION_MLP_WINDOW_FRAMES][MOTION_MLP_FEAT_DIM];
  int len;
  int start;
  int64_t cooldown_until_ms;
  int cooldown_active;
  /* Last softmax winner (including "none"); NULL until the 8-frame window is full. */
  const char *last_name;
  float last_prob;
} motion_mlp_detector_t;

void motion_mlp_params_default(motion_mlp_params_t *p);
void motion_mlp_init(motion_mlp_detector_t *det, const motion_mlp_params_t *p);
void motion_mlp_reset(motion_mlp_detector_t *det);
void motion_mlp_start_cooldown(motion_mlp_detector_t *det, int64_t now_ms);
int motion_mlp_model_ready(const motion_mlp_detector_t *det);

/**
 * @return class name pointer on fire, or NULL.
 *         Never returns "none".
 */
const char *motion_mlp_update(motion_mlp_detector_t *det, int hand, float conf,
                              const float xy[MOTION_MLP_LANDMARKS][2], int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_MLP_H */
