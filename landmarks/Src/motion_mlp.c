/**
 * MotionMLP CPU inference. Keep in lockstep with
 * motion_train/motion_mlp/infer.py and motion_train/normalize.py.
 *
 * Weights live in NOR (motion_mlp_data.hex @ MOTION_MLP_NOR_ADDR), not in Appli.
 */

#include "motion_mlp.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(motion_mlp_blob_header_t) == MOTION_MLP_HEADER_SIZE,
               "MotionMLP NOR header size");
_Static_assert(offsetof(motion_mlp_blob_header_t, names) == 64,
               "MotionMLP NOR names offset");

void motion_mlp_params_default(motion_mlp_params_t *p)
{
  p->window_frames = MOTION_MLP_WINDOW_FRAMES;
  p->min_softmax = 0.50f;
  p->min_conf = 0.5f;
  p->min_pixel_scale = 8.0f;
  p->max_norm_abs = 8.0f;
  p->cooldown_ms = 1500;
  p->min_scale = 0.01f;
  p->wrist_index = 0;
  p->scale_index = 9;
  p->landmark_decimals = 1;
}

void motion_mlp_reset(motion_mlp_detector_t *det)
{
  det->len = 0;
  det->start = 0;
  det->cooldown_active = 0;
  det->cooldown_until_ms = 0;
  det->last_name = NULL;
  det->last_prob = 0.0f;
}

static uint32_t payload_bytes_expected(void)
{
  return (uint32_t)(MOTION_MLP_HIDDEN0 * MOTION_MLP_INPUT_DIM + MOTION_MLP_HIDDEN0 +
                    MOTION_MLP_HIDDEN1 * MOTION_MLP_HIDDEN0 + MOTION_MLP_HIDDEN1 +
                    MOTION_MLP_NUM_CLASSES * MOTION_MLP_HIDDEN1 + MOTION_MLP_NUM_CLASSES) *
         (uint32_t)sizeof(float);
}

static int bind_blob(motion_mlp_detector_t *det, const void *blob)
{
  const motion_mlp_blob_header_t *h = (const motion_mlp_blob_header_t *)blob;
  const uint8_t *p;
  int i;

  det->model.ready = 0;
  if (h == NULL)
    return -1;
  if (h->magic != MOTION_MLP_BLOB_MAGIC || h->version != MOTION_MLP_BLOB_VERSION)
    return -2;
  if (h->window_frames != MOTION_MLP_WINDOW_FRAMES ||
      h->landmarks != MOTION_MLP_LANDMARKS ||
      h->feat_dim != MOTION_MLP_FEAT_DIM ||
      h->input_dim != MOTION_MLP_INPUT_DIM ||
      h->hidden0 != MOTION_MLP_HIDDEN0 ||
      h->hidden1 != MOTION_MLP_HIDDEN1 ||
      h->num_classes != MOTION_MLP_NUM_CLASSES ||
      h->name_len != MOTION_MLP_NAME_LEN)
    return -3;
  if (h->payload_bytes != payload_bytes_expected())
    return -4;

  p = (const uint8_t *)blob + MOTION_MLP_HEADER_SIZE;
  det->model.w0 = (const float *)(const void *)p;
  p += (size_t)MOTION_MLP_HIDDEN0 * MOTION_MLP_INPUT_DIM * sizeof(float);
  det->model.b0 = (const float *)(const void *)p;
  p += (size_t)MOTION_MLP_HIDDEN0 * sizeof(float);
  det->model.w1 = (const float *)(const void *)p;
  p += (size_t)MOTION_MLP_HIDDEN1 * MOTION_MLP_HIDDEN0 * sizeof(float);
  det->model.b1 = (const float *)(const void *)p;
  p += (size_t)MOTION_MLP_HIDDEN1 * sizeof(float);
  det->model.w2 = (const float *)(const void *)p;
  p += (size_t)MOTION_MLP_NUM_CLASSES * MOTION_MLP_HIDDEN1 * sizeof(float);
  det->model.b2 = (const float *)(const void *)p;

  for (i = 0; i < MOTION_MLP_NUM_CLASSES; i++)
    det->model.names[i] = h->names[i];

  det->model.ready = 1;
  return 0;
}

void motion_mlp_init(motion_mlp_detector_t *det, const motion_mlp_params_t *p)
{
  memset(det, 0, sizeof(*det));
  if (p)
    det->params = *p;
  else
    motion_mlp_params_default(&det->params);
  if (det->params.window_frames < 1)
    det->params.window_frames = 1;
  if (det->params.window_frames > MOTION_MLP_WINDOW_FRAMES)
    det->params.window_frames = MOTION_MLP_WINDOW_FRAMES;
  motion_mlp_reset(det);
  (void)bind_blob(det, (const void *)(uintptr_t)MOTION_MLP_NOR_ADDR);
}

int motion_mlp_model_ready(const motion_mlp_detector_t *det)
{
  return det->model.ready;
}

void motion_mlp_start_cooldown(motion_mlp_detector_t *det, int64_t now_ms)
{
  det->cooldown_active = 1;
  det->cooldown_until_ms = now_ms + det->params.cooldown_ms;
}

static int in_cooldown(const motion_mlp_detector_t *det, int64_t now_ms)
{
  return det->cooldown_active && now_ms < det->cooldown_until_ms;
}

static float round_dec(float v, int decimals)
{
  float s;
  if (decimals <= 0)
    return v;
  s = powf(10.0f, (float)decimals);
  return roundf(v * s) / s;
}

static int normalize_frame(const motion_mlp_detector_t *det,
                           const float xy[MOTION_MLP_LANDMARKS][2],
                           float out[MOTION_MLP_FEAT_DIM])
{
  float wx, wy, ax, ay, dx, dy, scale;
  float max_abs = 0.0f;
  int i;
  int dec = det->params.landmark_decimals;

  wx = round_dec(xy[det->params.wrist_index][0], dec);
  wy = round_dec(xy[det->params.wrist_index][1], dec);
  ax = round_dec(xy[det->params.scale_index][0], dec);
  ay = round_dec(xy[det->params.scale_index][1], dec);
  dx = ax - wx;
  dy = ay - wy;
  scale = sqrtf(dx * dx + dy * dy);
  if (scale < det->params.min_pixel_scale)
    return 0;
  if (scale < det->params.min_scale)
    scale = det->params.min_scale;

  for (i = 0; i < MOTION_MLP_LANDMARKS; i++) {
    float x = round_dec(xy[i][0], dec);
    float y = round_dec(xy[i][1], dec);
    float nx = (x - wx) / scale;
    float ny = (y - wy) / scale;
    if (!isfinite(nx) || !isfinite(ny))
      return 0;
    if (fabsf(nx) > max_abs)
      max_abs = fabsf(nx);
    if (fabsf(ny) > max_abs)
      max_abs = fabsf(ny);
    out[2 * i + 0] = nx;
    out[2 * i + 1] = ny;
  }
  if (max_abs > det->params.max_norm_abs)
    return 0;
  return 1;
}

static void buf_push(motion_mlp_detector_t *det, const float feat[MOTION_MLP_FEAT_DIM])
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
  memcpy(det->feats[idx], feat, sizeof(float) * MOTION_MLP_FEAT_DIM);
}

static void pack_input(const motion_mlp_detector_t *det, float *out)
{
  int cap = det->params.window_frames;
  int i;
  for (i = 0; i < cap; i++) {
    int idx = (det->start + i) % cap;
    memcpy(out + i * MOTION_MLP_FEAT_DIM, det->feats[idx],
           sizeof(float) * MOTION_MLP_FEAT_DIM);
  }
}

/* y = W x + b ; W is row-major [out, in] matching PyTorch. */
static void linear(const float *w, const float *b, const float *x, float *y, int out_n, int in_n)
{
  int o, i;
  for (o = 0; o < out_n; o++) {
    float s = b[o];
    const float *row = w + o * in_n;
    for (i = 0; i < in_n; i++)
      s += row[i] * x[i];
    y[o] = s;
  }
}

static void relu_inplace(float *v, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    if (v[i] < 0.0f)
      v[i] = 0.0f;
  }
}

static int argmax_softmax(const float *logits, int n, float *out_prob)
{
  float maxv = logits[0];
  float sum = 0.0f;
  float probs[MOTION_MLP_NUM_CLASSES];
  int i, best = 0;

  for (i = 1; i < n; i++) {
    if (logits[i] > maxv)
      maxv = logits[i];
  }
  for (i = 0; i < n; i++) {
    probs[i] = expf(logits[i] - maxv);
    sum += probs[i];
  }
  for (i = 0; i < n; i++)
    probs[i] /= sum;
  for (i = 1; i < n; i++) {
    if (probs[i] > probs[best])
      best = i;
  }
  *out_prob = probs[best];
  return best;
}

const char *motion_mlp_update(motion_mlp_detector_t *det, int hand, float conf,
                              const float xy[MOTION_MLP_LANDMARKS][2], int64_t now_ms)
{
  float feat[MOTION_MLP_FEAT_DIM];
  float input[MOTION_MLP_INPUT_DIM];
  float h0[MOTION_MLP_HIDDEN0];
  float h1[MOTION_MLP_HIDDEN1];
  float logits[MOTION_MLP_NUM_CLASSES];
  float prob;
  int idx;
  const char *name;

  if (!det->model.ready || !hand || xy == NULL || conf < det->params.min_conf) {
    det->len = 0;
    det->start = 0;
    det->last_name = NULL;
    det->last_prob = 0.0f;
    return NULL;
  }

  if (!normalize_frame(det, xy, feat))
    return NULL;

  buf_push(det, feat);

  if (det->len < det->params.window_frames) {
    det->last_name = NULL;
    det->last_prob = 0.0f;
    return NULL;
  }

  pack_input(det, input);
  linear(det->model.w0, det->model.b0, input, h0, MOTION_MLP_HIDDEN0, MOTION_MLP_INPUT_DIM);
  relu_inplace(h0, MOTION_MLP_HIDDEN0);
  linear(det->model.w1, det->model.b1, h0, h1, MOTION_MLP_HIDDEN1, MOTION_MLP_HIDDEN0);
  relu_inplace(h1, MOTION_MLP_HIDDEN1);
  linear(det->model.w2, det->model.b2, h1, logits, MOTION_MLP_NUM_CLASSES, MOTION_MLP_HIDDEN1);

  idx = argmax_softmax(logits, MOTION_MLP_NUM_CLASSES, &prob);
  name = det->model.names[idx];
  det->last_name = name;
  det->last_prob = prob;

  if (in_cooldown(det, now_ms))
    return NULL;
  if (strcmp(name, "none") == 0 || prob < det->params.min_softmax)
    return NULL;

  motion_mlp_start_cooldown(det, now_ms);
  return name;
}
