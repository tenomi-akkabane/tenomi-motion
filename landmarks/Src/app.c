/**
  ******************************************************************************
  * @file    app.c
  * @author  MDG Application Team
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "app.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_cam.h"
#include "app_config.h"
#include "mve_resize.h"
#include "app_postprocess.h"
#include "isp_api.h"
#include "ld.h"
#include "stai.h"
#include "stai_palm_detector.h"
#include "stai_hand_landmark.h"
#include "cmw_camera.h"
#include "scrl.h"
#ifdef STM32N6570_DK_REV
#include "stm32n6570_discovery.h"
#else
#include "stm32n6xx_nucleo.h"
#endif
#include "stm32_lcd.h"
#include "stm32_lcd_ex.h"
#include "stm32n6xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "utils.h"

#ifdef STM32N6570_DK_REV
extern LTDC_HandleTypeDef hlcd_ltdc;
#endif

#ifndef TENOMI_C5_VIDEO_ONLY
#define TENOMI_C5_VIDEO_ONLY 0
#endif

/* M4_PD_ONLY: palm detection only (skip hand-landmark). Does NOT force VIDEO_ONLY. */
#ifndef TENOMI_M4_PD_ONLY
#define TENOMI_M4_PD_ONLY 0
#endif
#ifndef TENOMI_M4_REAL_NN
#define TENOMI_M4_REAL_NN 0
#endif
#ifndef TENOMI_BUILD_ATON_OSAL
#define TENOMI_BUILD_ATON_OSAL 0
#endif

#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "v1.0.0"
#endif

#if defined(TENOMI_C5_VIDEO_ONLY) || defined(TENOMI_M4_PD_ONLY) || defined(TENOMI_M4_REAL_NN)
#include "hl_xspi.h"
#include "hl_log.h"
/* Quiet UART for JSON (M6); set TENOMI_VERBOSE_LOG=1 in hl_log.h to restore chatter */
#define TENOMI_APP_LOG(msg) do { } while (0)
#else
#define TENOMI_APP_LOG(msg) do { printf("%s", (msg)); } while (0)
#endif

#ifndef FREERTOS_PRIORITY
#define FREERTOS_PRIORITY(p) ((UBaseType_t)((int)tskIDLE_PRIORITY + configMAX_PRIORITIES / 2 + (p)))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if HAS_ROTATION_SUPPORT == 1
#include "nema_core.h"
#include "nema_error.h"
void nema_enable_tiling(int);
#endif

#define LCD_FG_WIDTH LCD_BG_WIDTH
#define LCD_FG_HEIGHT LCD_BG_HEIGHT

#define CACHE_OP(__op__) do { \
  if (is_cache_enable()) { \
    __op__; \
  } \
} while (0)

#define DBG_INFO 0
#define USE_FILTERED_TS 1

/* Step2/M6: send landmark JSON to PC via ST-LINK virtual COM (115200) */
#ifndef APP_JSON_ENABLE
#define APP_JSON_ENABLE 1
#endif
#ifndef APP_JSON_INTERVAL_MS
#define APP_JSON_INTERVAL_MS 100
#endif
#ifndef APP_MOTION_ENABLE
#define APP_MOTION_ENABLE 1
#endif

/* Blocking USART1 TX (printf → tm_snd_dat) must not run on nn or dp. */
#if APP_JSON_ENABLE || APP_MOTION_ENABLE
#define APP_UART_TX_ENABLE 1
#else
#define APP_UART_TX_ENABLE 0
#endif

#if APP_MOTION_ENABLE
#include "motion_hub.h"
#if LD_LANDMARK_NB != MOTION_MLP_LANDMARKS
#error "motion hub expects 21 landmarks (same as JSON lm[])"
#endif
#endif

#if APP_MOTION_ENABLE
#define NN_THREAD_STACK_MULT 4
#else
#define NN_THREAD_STACK_MULT 2
#endif

#define BQUEUE_MAX_BUFFERS 2
#define CPU_LOAD_HISTORY_DEPTH 8

#define DISPLAY_BUFFER_NB (DISPLAY_DELAY + 2)

/* palm detector */
#define PD_MAX_HAND_NB 1

#if HAS_ROTATION_SUPPORT == 1
typedef float app_v3_t[3];
#endif

typedef struct {
  float cx;
  float cy;
  float w;
  float h;
  float rotation;
} roi_t;

#define UTIL_LCD_COLOR_TRANSPARENT 0

#ifdef STM32N6570_DK_REV
#define LCD_FONT Font20
#define DISK_RADIUS 2
#else
#define LCD_FONT Font12
#define DISK_RADIUS 1
#endif

typedef struct
{
  uint32_t X0;
  uint32_t Y0;
  uint32_t XSize;
  uint32_t YSize;
} Rectangle_TypeDef;

typedef struct {
  SemaphoreHandle_t free;
  StaticSemaphore_t free_buffer;
  SemaphoreHandle_t ready;
  StaticSemaphore_t ready_buffer;
  int buffer_nb;
  uint8_t *buffers[BQUEUE_MAX_BUFFERS];
  int free_idx;
  int ready_idx;
  /* ISR-safe free slots (μT-Kernel forbids tk_wai_sem from Cube IRQ). */
  volatile int free_count;
} bqueue_t;

typedef struct {
  uint64_t current_total;
  uint64_t current_thread_total;
  uint64_t prev_total;
  uint64_t prev_thread_total;
  struct {
    uint64_t total;
    uint64_t thread;
    uint32_t tick;
  } history[CPU_LOAD_HISTORY_DEPTH];
} cpuload_info_t;

typedef struct {
  int is_valid;
  pd_pp_box_t pd_hands;
  roi_t roi;
  ld_point_t ld_landmarks[LD_LANDMARK_NB];
} hand_info_t;

typedef struct {
  float nn_period_ms;
  uint32_t pd_ms;
  uint32_t hl_ms;
  uint32_t pp_ms;
  uint32_t disp_ms;
  int is_ld_displayed;
  int is_pd_displayed;
  int pd_hand_nb;
  float pd_max_prob;
  hand_info_t hands[PD_MAX_HAND_NB];
#if APP_MOTION_ENABLE
  const char *motion_name;
  uint32_t motion_tick;
  char motion_lcd[24];
#endif
} display_info_t;

typedef struct {
  SemaphoreHandle_t update;
  StaticSemaphore_t update_buffer;
  SemaphoreHandle_t lock;
  StaticSemaphore_t lock_buffer;
  display_info_t info;
} display_t;

typedef struct {
  uint32_t nn_in_len;
  float *prob_out;
  uint32_t prob_out_len;
  float *boxes_out;
  uint32_t boxes_out_len;
  pd_model_pp_static_param_t static_param;
  pd_pp_out_t pd_out;
} pd_model_info_t;

typedef struct {
  uint8_t *nn_in;
  uint32_t nn_in_len;
  float *prob_out;
  uint32_t prob_out_len;
  float *landmarks_out;
  uint32_t landmarks_out_len;
} hl_model_info_t;

typedef struct {
  Button_TypeDef button_id;
  int prev_state;
  void (*on_click_handler)(void *cb_args);
  void *cb_args;
} button_t;

/* Globals */
/* Lcd Background area */
static Rectangle_TypeDef lcd_bg_area = {
  .X0 = 0,
  .Y0 = 0,
  .XSize = LCD_BG_WIDTH,
  .YSize = LCD_BG_HEIGHT,
};
/* Lcd Foreground area */
static Rectangle_TypeDef lcd_fg_area = {
  .X0 = 0,
  .Y0 = 0,
  .XSize = LCD_FG_WIDTH,
  .YSize = LCD_FG_HEIGHT,
};
/* Lcd Background Buffer */
static uint8_t lcd_bg_buffer[DISPLAY_BUFFER_NB][LCD_BG_WIDTH * LCD_BG_HEIGHT * DISPLAY_BPP] ALIGN_32 IN_PSRAM;
static int lcd_bg_buffer_disp_idx = 1;
static int lcd_bg_buffer_capt_idx = 0;
/* Lcd Foreground Buffer */
static uint8_t lcd_fg_buffer[2][LCD_FG_WIDTH * LCD_FG_HEIGHT* 2] ALIGN_32 IN_PSRAM;
static int lcd_fg_buffer_rd_idx;
#ifdef STM32N6570_DK_REV
static int dp_fg_reload_pending;
#endif
static display_t disp = {
  .info.is_ld_displayed = 1,
  /* TAMP (B4): PD boxes + UART 21-point JSON. Default off so tracking FPS
   * is not eaten by USART. Turning PD on also forces LCD landmarks on. */
  .info.is_pd_displayed = 0,
};
static cpuload_info_t cpu_load;
/* screen buffer */
static uint8_t screen_buffer[LCD_BG_WIDTH * LCD_BG_HEIGHT * 2] ALIGN_32 IN_PSRAM;

/* model */
 /* palm detector */
__attribute__ ((aligned (32)))
static uint8_t network_palm_detector_ctx[STAI_PALM_DETECTOR_CONTEXT_SIZE];
static roi_t rois[PD_MAX_HAND_NB];
 /* hand landmark */
__attribute__ ((aligned (32)))
static uint8_t network_hand_landmark_ctx[STAI_HAND_LANDMARK_CONTEXT_SIZE];
static ld_point_t ld_landmarks[PD_MAX_HAND_NB][LD_LANDMARK_NB];
static uint32_t frame_event_nb;
static volatile uint32_t frame_event_nb_for_resize;
volatile uint32_t g_tenomi_pipe2_frame_nb;
volatile uint32_t g_tenomi_pipe2_drop_nb;
volatile uint32_t g_tenomi_npu_sig_cnt;

 /* nn input buffers */
static uint8_t nn_input_buffers[2][STAI_PALM_DETECTOR_IN_1_WIDTH * STAI_PALM_DETECTOR_IN_1_HEIGHT * STAI_PALM_DETECTOR_IN_1_CHANNEL] ALIGN_32 IN_PSRAM;
static bqueue_t nn_input_queue;

 /* rtos */
static StaticTask_t nn_thread;
static StackType_t nn_thread_stack[NN_THREAD_STACK_MULT * configMINIMAL_STACK_SIZE];
static StaticTask_t dp_thread;
static StackType_t dp_thread_stack[2 *configMINIMAL_STACK_SIZE];
static StaticTask_t isp_thread;
static StackType_t isp_thread_stack[2 *configMINIMAL_STACK_SIZE];
static SemaphoreHandle_t isp_sem;
static StaticSemaphore_t isp_sem_buffer;
#if APP_UART_TX_ENABLE
#define APP_UART_JSON_MAX 1536
#define APP_UART_LINE_MAX 80
static StaticTask_t uart_thread;
static StackType_t uart_thread_stack[2 * configMINIMAL_STACK_SIZE];
static struct {
  SemaphoreHandle_t lock;
  StaticSemaphore_t lock_buffer;
  SemaphoreHandle_t wake;
  StaticSemaphore_t wake_buffer;
  char json[APP_UART_JSON_MAX];
  int json_len;
  char line[APP_UART_LINE_MAX];
  int line_len;
} uart_tx;
#endif

#if HAS_ROTATION_SUPPORT == 0
/* Software resize buffer */
static void *pScratch[STAI_HAND_LANDMARK_IN_1_WIDTH * 2 * sizeof(uint16_t) + STAI_HAND_LANDMARK_IN_1_WIDTH * 1 * sizeof(float16_t)];
#else
static GFXMMU_HandleTypeDef hgfxmmu;
static nema_cmdlist_t cl;
#endif

static int is_cache_enable()
{
#if defined(USE_DCACHE)
  return 1;
#else
  return 0;
#endif
}

static float pd_normalize_angle(float angle)
{
  return angle - 2 * M_PI * floorf((angle - (-M_PI)) / (2 * M_PI));
}

/* Without rotation support allow limited amount of angles */
#if HAS_ROTATION_SUPPORT == 0
static float pd_cook_rotation(float angle)
{
  if (angle >= (3 * M_PI) / 4)
    angle = M_PI;
  else if (angle >= (1 * M_PI) / 4)
    angle = M_PI / 2;
  else if (angle >= -(1 * M_PI) / 4)
    angle = 0;
  else if (angle >= -(3 * M_PI) / 4)
    angle = -M_PI / 2;
  else
    angle = -M_PI;

  return angle;
}
#else
static float pd_cook_rotation(float angle)
{
  return angle;
}
#endif

static float pd_compute_rotation(pd_pp_box_t *box)
{
  float x0, y0, x1, y1;
  float rotation;

  x0 = box->pKps[0].x;
  y0 = box->pKps[0].y;
  x1 = box->pKps[2].x;
  y1 = box->pKps[2].y;

  rotation = M_PI * 0.5 - atan2f(-(y1 - y0), x1 - x0);

  return pd_cook_rotation(pd_normalize_angle(rotation));
}

static void cvt_pd_coord_to_screen_coord(pd_pp_box_t *box)
{
  int i;

  /* This is not a typo. Since screen aspect ratio was conserved. We really want to use LCD_BG_WIDTH for
   * y positions.
   */

  box->x_center *= LCD_BG_WIDTH;
  box->y_center *= LCD_BG_WIDTH;
  box->width *= LCD_BG_WIDTH;
  box->height *= LCD_BG_WIDTH;
  for (i = 0; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++) {
    box->pKps[i].x *= LCD_BG_WIDTH;
    box->pKps[i].y *= LCD_BG_WIDTH;
  }
}

static void roi_shift_and_scale(roi_t *roi, float shift_x, float shift_y, float scale_x, float scale_y)
{
  float long_side;
  float sx, sy;

  sx = (roi->w * shift_x * cos(roi->rotation) - roi->h * shift_y * sin(roi->rotation));
  sy = (roi->w * shift_x * sin(roi->rotation) + roi->h * shift_y * cos(roi->rotation));

  roi->cx += sx;
  roi->cy += sy;

  long_side = MAX(roi->w, roi->h);
  roi->w = long_side;
  roi->h = long_side;

  roi->w *= scale_x;
  roi->h *= scale_y;
}

static void pd_box_to_roi(pd_pp_box_t *box,  roi_t *roi)
{
  const float shift_x = 0;
  const float shift_y = -0.5;
  const float scale = 2.6;

  roi->cx = box->x_center;
  roi->cy = box->y_center;
  roi->w = box->width;
  roi->h = box->height;
  roi->rotation = pd_compute_rotation(box);

  roi_shift_and_scale(roi, shift_x, shift_y, scale, scale);

#if HAS_ROTATION_SUPPORT == 0
  /* In that case we can cancel rotation. This ensure corners are corrected oriented */
  roi->rotation = 0;
#endif
}

static void copy_pd_box(pd_pp_box_t *dst, pd_pp_box_t *src)
{
  int i;

  dst->prob = src->prob;
  dst->x_center = src->x_center;
  dst->y_center = src->y_center;
  dst->width = src->width;
  dst->height = src->height;
  for (i = 0 ; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++)
    dst->pKps[i] = src->pKps[i];
}

static void button_init(button_t *b, Button_TypeDef id, void (*on_click_handler)(void *), void *cb_args)
{
  int ret;

  ret = BSP_PB_Init(id, BUTTON_MODE_GPIO);
  assert(ret == BSP_ERROR_NONE);

  b->button_id = id;
  b->on_click_handler = on_click_handler;
  b->prev_state = 0;
  b->cb_args = cb_args;
}

static void button_process(button_t *b)
{
  int state = BSP_PB_GetState(b->button_id);

  if (state != b->prev_state && state && b->on_click_handler)
    b->on_click_handler(b->cb_args);

  b->prev_state = state;
}

static void cpuload_init(cpuload_info_t *cpu_load)
{
  memset(cpu_load, 0, sizeof(cpuload_info_t));
}

static void cpuload_update(cpuload_info_t *cpu_load)
{
  int i;

  cpu_load->history[1] = cpu_load->history[0];
  cpu_load->history[0].total = portGET_RUN_TIME_COUNTER_VALUE();
  cpu_load->history[0].thread = cpu_load->history[0].total - ulTaskGetIdleRunTimeCounter();
  cpu_load->history[0].tick = HAL_GetTick();

  if (cpu_load->history[1].tick - cpu_load->history[2].tick < 1000)
    return ;

  for (i = 0; i < CPU_LOAD_HISTORY_DEPTH - 2; i++)
    cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i] = cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i - 1];
}

static void cpuload_get_info(cpuload_info_t *cpu_load, float *cpu_load_last, float *cpu_load_last_second,
                             float *cpu_load_last_five_seconds)
{
  if (cpu_load_last)
    *cpu_load_last = 100.0 * (cpu_load->history[0].thread - cpu_load->history[1].thread) /
                     (cpu_load->history[0].total - cpu_load->history[1].total);
  if (cpu_load_last_second)
    *cpu_load_last_second = 100.0 * (cpu_load->history[2].thread - cpu_load->history[3].thread) /
                     (cpu_load->history[2].total - cpu_load->history[3].total);
  if (cpu_load_last_five_seconds)
    *cpu_load_last_five_seconds = 100.0 * (cpu_load->history[2].thread - cpu_load->history[7].thread) /
                     (cpu_load->history[2].total - cpu_load->history[7].total);
}

static int bqueue_init(bqueue_t *bq, int buffer_nb, uint8_t **buffers)
{
  int i;

  if (buffer_nb > BQUEUE_MAX_BUFFERS)
    return -1;

  /* free_count is the source of truth; free sem is wake-only (initial 0). */
  bq->free = xSemaphoreCreateCountingStatic(buffer_nb, 0, &bq->free_buffer);
  if (!bq->free)
    goto free_sem_error;
  bq->ready = xSemaphoreCreateCountingStatic(buffer_nb, 0, &bq->ready_buffer);
  if (!bq->ready)
    goto ready_sem_error;

  bq->buffer_nb = buffer_nb;
  for (i = 0; i < buffer_nb; i++) {
    assert(buffers[i]);
    bq->buffers[i] = buffers[i];
  }
  bq->free_idx = 0;
  bq->ready_idx = 0;
  bq->free_count = buffer_nb;

  return 0;

ready_sem_error:
  vSemaphoreDelete(bq->free);
free_sem_error:
  return -1;
}

static uint8_t *bqueue_get_free(bqueue_t *bq, int is_blocking)
{
  uint8_t *res;
  uint32_t primask;

  for (;;) {
    /* Never tk_wai_sem from DCMIPP IRQ (E_CTX / wrong ctxtsk). */
    primask = __get_PRIMASK();
    __disable_irq();
    if (bq->free_count > 0) {
      bq->free_count--;
      res = bq->buffers[bq->free_idx];
      bq->free_idx = (bq->free_idx + 1) % bq->buffer_nb;
      __set_PRIMASK(primask);
      return res;
    }
    __set_PRIMASK(primask);

    if (!is_blocking || xPortIsInsideInterrupt())
      return NULL;

    /* Wait for put_free; then re-check free_count. */
    if (xSemaphoreTake(bq->free, portMAX_DELAY) != pdTRUE)
      return NULL;
  }
}

static void bqueue_put_free(bqueue_t *bq)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  bq->free_count++;
  __set_PRIMASK(primask);

  /* Wake any blocking get_free; ignore overflow if nobody is waiting. */
  (void)xSemaphoreGive(bq->free);
}

static uint8_t *bqueue_get_ready(bqueue_t *bq)
{
  uint8_t *res;
  int ret;

  ret = xSemaphoreTake(bq->ready, portMAX_DELAY);
  assert(ret == pdTRUE);

  res = bq->buffers[bq->ready_idx];
  bq->ready_idx = (bq->ready_idx + 1) % bq->buffer_nb;

  return res;
}

static void bqueue_put_ready(bqueue_t *bq)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  int ret;

  if (xPortIsInsideInterrupt()) {
    ret = xSemaphoreGiveFromISR(bq->ready, &xHigherPriorityTaskWoken);
    assert(ret == pdTRUE);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    ret = xSemaphoreGive(bq->ready);
    assert(ret == pdTRUE);
  }
}

static void reload_bg_layer(int next_disp_idx)
{
  int ret;

  ret = SCRL_SetAddress_NoReload(lcd_bg_buffer[next_disp_idx], SCRL_LAYER_0);
  assert(ret == 0);
  ret = SCRL_ReloadLayer(SCRL_LAYER_0);
  assert(ret == 0);

  ret = SRCL_Update();
  assert(ret == 0);
}

static void app_main_pipe_frame_event()
{
#if TENOMI_C5_VIDEO_ONLY
  /* True ping-pong (2 buffers): display the frame that just completed, then
   * point DCMIPP at the other buffer. Skip the first couple of frames — ISP
   * demosaic/AE are still settling and look soft or noisy. */
  int done_idx = lcd_bg_buffer_capt_idx;
  int next_capt = (done_idx + 1) % 2;
  int ret;

  frame_event_nb++;
  if (frame_event_nb > 2U) {
    reload_bg_layer(done_idx);
    lcd_bg_buffer_disp_idx = done_idx;
  }

  ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                         DCMIPP_MEMORY_ADDRESS_0, (uint32_t)lcd_bg_buffer[next_capt]);
  assert(ret == HAL_OK);
  lcd_bg_buffer_capt_idx = next_capt;
#else
  int next_disp_idx = (lcd_bg_buffer_disp_idx + 1) % DISPLAY_BUFFER_NB;
  int next_capt_idx = (lcd_bg_buffer_capt_idx + 1) % DISPLAY_BUFFER_NB;
  int ret;

  ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                         DCMIPP_MEMORY_ADDRESS_0, (uint32_t) lcd_bg_buffer[next_capt_idx]);
  assert(ret == HAL_OK);

  reload_bg_layer(next_disp_idx);
  lcd_bg_buffer_disp_idx = next_disp_idx;
  lcd_bg_buffer_capt_idx = next_capt_idx;

  frame_event_nb++;
#endif
}


static void app_ancillary_pipe_frame_event()
{
  uint8_t *next_buffer;
  int ret;

  g_tenomi_pipe2_frame_nb++;
  next_buffer = bqueue_get_free(&nn_input_queue, 0);
  if (next_buffer) {
    ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE2,
                                           DCMIPP_MEMORY_ADDRESS_0, (uint32_t) next_buffer);
    assert(ret == HAL_OK);
    /* minus 1 since app_main_pipe_frame_event occur before app_ancillary_pipe_frame_event() */
    frame_event_nb_for_resize = frame_event_nb - 1;
    bqueue_put_ready(&nn_input_queue);
  } else {
    g_tenomi_pipe2_drop_nb++;
  }
}

static void app_main_pipe_vsync_event()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  int ret;

  ret = xSemaphoreGiveFromISR(isp_sem, &xHigherPriorityTaskWoken);
  if (ret == pdTRUE)
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static int clamp_point(int *x, int *y)
{
  int xi = *x;
  int yi = *y;

  if (*x < 0)
    *x = 0;
  if (*y < 0)
    *y = 0;
  if (*x >= lcd_bg_area.XSize)
    *x = lcd_bg_area.XSize - 1;
  if (*y >= lcd_bg_area.YSize)
    *y = lcd_bg_area.YSize - 1;

  return (xi != *x) || (yi != *y);
}

static int clamp_point_with_margin(int *x, int *y, int margin)
{
  int xi = *x;
  int yi = *y;

  if (*x < margin)
    *x = margin;
  if (*y < margin)
    *y = margin;
  if (*x >= lcd_bg_area.XSize - margin)
    *x = lcd_bg_area.XSize - margin - 1;
  if (*y >= lcd_bg_area.YSize - margin)
    *y = lcd_bg_area.YSize - margin - 1;

  return (xi != *x) || (yi != *y);
}

static void display_pd_hand(pd_pp_box_t *hand)
{
  int xc, yc;
  int x0, y0;
  int x1, y1;
  int w, h;
  int i;

  /* display box around palm */
  xc = (int)hand->x_center;
  yc = (int)hand->y_center;
  w = (int)hand->width;
  h = (int)hand->height;
  x0 = xc - (w + 1) / 2;
  y0 = yc - (h + 1) / 2;
  x1 = xc + (w + 1) / 2;
  y1 = yc + (h + 1) / 2;
  clamp_point(&x0, &y0);
  clamp_point(&x1, &y1);
  UTIL_LCD_DrawRect(x0, y0, x1 - x0, y1 - y0, UTIL_LCD_COLOR_GREEN);

  /* display palm key points */
  for (i = 0; i < 7; i++) {
    uint32_t color = (i != 0 && i != 2) ? UTIL_LCD_COLOR_RED : UTIL_LCD_COLOR_BLUE;

    x0 = (int)hand->pKps[i].x;
    y0 = (int)hand->pKps[i].y;
    clamp_point(&x0, &y0);
    UTIL_LCD_FillCircle(x0, y0, 2, color);
  }
}

static void rotate_point(float pt[2], float rotation)
{
  float x = pt[0];
  float y = pt[1];

  pt[0] = cos(rotation) * x - sin(rotation) * y;
  pt[1] = sin(rotation) * x + cos(rotation) * y;
}

static void roi_to_corners(roi_t *roi, float corners[4][2])
{
  const float corners_init[4][2] = {
    {-roi->w / 2, -roi->h / 2},
    { roi->w / 2, -roi->h / 2},
    { roi->w / 2,  roi->h / 2},
    {-roi->w / 2,  roi->h / 2},
  };
  int i;

  memcpy(corners, corners_init, sizeof(corners_init));
  /* rotate */
  for (i = 0; i < 4; i++)
    rotate_point(corners[i], roi->rotation);

  /* shift */
  for (i = 0; i < 4; i++) {
    corners[i][0] += roi->cx;
    corners[i][1] += roi->cy;
  }
}

static int clamp_corners(float corners_in[4][2], int corners_out[4][2])
{
  int is_clamp = 0;
  int i;

  for (i = 0; i < 4; i++) {
    corners_out[i][0] = (int)corners_in[i][0];
    corners_out[i][1] = (int)corners_in[i][1];
    is_clamp |= clamp_point(&corners_out[i][0], &corners_out[i][1]);
  }

  return is_clamp;
}

static void display_roi(roi_t *roi)
{
  float corners_f[4][2];
  int corners[4][2];
  int is_clamp;
  int i;

  /* compute box corners */
  roi_to_corners(roi, corners_f);

  /* clamp */
  is_clamp = clamp_corners(corners_f, corners);
  if (is_clamp)
    return ;

  /* display */
  for (i = 0; i < 4; i++)
    UTIL_LCD_DrawLine(corners[i][0], corners[i][1], corners[(i + 1) % 4][0], corners[(i + 1) % 4][1],
                      UTIL_LCD_COLOR_RED);
}

static void decode_ld_landmark(roi_t *roi, ld_point_t *lm, ld_point_t *decoded)
{
  float rotation = roi->rotation;
  float w = roi->w;
  float h = roi->h;

  decoded->x = roi->cx + (lm->x - 0.5) * w * cos(rotation) - (lm->y - 0.5) * h * sin(rotation);
  decoded->y = roi->cy + (lm->x - 0.5) * w * sin(rotation) + (lm->y - 0.5) * h * cos(rotation);
}

#if APP_UART_TX_ENABLE
static void app_uart_post(char *dst, int *len_p, int cap, const char *src, int n)
{
  int ret;

  if (src == NULL || n <= 0 || dst == NULL || len_p == NULL || cap <= 0)
    return;
  if (n > cap)
    n = cap;
  ret = xSemaphoreTake(uart_tx.lock, portMAX_DELAY);
  assert(ret == pdTRUE);
  memcpy(dst, src, (size_t)n);
  *len_p = n;
  ret = xSemaphoreGive(uart_tx.lock);
  assert(ret == pdTRUE);
  (void)xSemaphoreGive(uart_tx.wake);
}

static void app_uart_post_json(const char *src, int n)
{
  app_uart_post(uart_tx.json, &uart_tx.json_len, APP_UART_JSON_MAX, src, n);
}

static void app_uart_post_line(const char *src, int n)
{
  app_uart_post(uart_tx.line, &uart_tx.line_len, APP_UART_LINE_MAX, src, n);
}

static void uart_thread_fct(void *arg)
{
  char json_local[APP_UART_JSON_MAX];
  char line_local[APP_UART_LINE_MAX];
  int json_n;
  int line_n;
  int ret;

  (void)arg;
  for (;;) {
    ret = xSemaphoreTake(uart_tx.wake, portMAX_DELAY);
    assert(ret == pdTRUE);
    for (;;) {
      json_n = 0;
      line_n = 0;
      ret = xSemaphoreTake(uart_tx.lock, portMAX_DELAY);
      assert(ret == pdTRUE);
      if (uart_tx.json_len > 0) {
        json_n = uart_tx.json_len;
        if (json_n > APP_UART_JSON_MAX)
          json_n = APP_UART_JSON_MAX;
        memcpy(json_local, uart_tx.json, (size_t)json_n);
        uart_tx.json_len = 0;
      }
      if (uart_tx.line_len > 0) {
        line_n = uart_tx.line_len;
        if (line_n > APP_UART_LINE_MAX)
          line_n = APP_UART_LINE_MAX;
        memcpy(line_local, uart_tx.line, (size_t)line_n);
        uart_tx.line_len = 0;
      }
      ret = xSemaphoreGive(uart_tx.lock);
      assert(ret == pdTRUE);

      if (json_n <= 0 && line_n <= 0)
        break;
      /* Same order as before: JSON line, then motion:/json: status. */
      if (json_n > 0) {
        (void)fwrite(json_local, 1, (size_t)json_n, stdout);
        (void)fflush(stdout);
      }
      if (line_n > 0) {
        (void)fwrite(line_local, 1, (size_t)line_n, stdout);
        (void)fflush(stdout);
      }
      if (xSemaphoreTake(uart_tx.wake, 0) != pdTRUE)
        break;
    }
  }
}
#endif

#if APP_MOTION_ENABLE
static motion_hub_t s_motion_hub;
static const char *s_motion_last_name;
static uint32_t s_motion_last_tick;
static char s_motion_lcd[24] = "idle";

static void app_motion_init(void)
{
  static int inited;

  if (inited)
    return;
  motion_hub_init(&s_motion_hub, NULL, NULL);
  if (!motion_mlp_model_ready(&s_motion_hub.mlp)) {
    HL_ERR((UB *)"hand_landmark: ERROR MotionMLP NOR bind failed @0x70A00000\n");
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "no wgt");
  } else {
    HL_LOG((UB *)"hand_landmark: MotionMLP NOR OK @0x70A00000\n");
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "idle");
  }
  inited = 1;
}

static void app_motion_refresh_lcd(int hand_detected, uint32_t now)
{
  if (s_motion_last_name && (now - s_motion_last_tick) < 2000U) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "*%s*", s_motion_last_name);
  } else if (!motion_mlp_model_ready(&s_motion_hub.mlp)) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "no wgt");
  } else if (!hand_detected) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "idle");
  } else if (s_motion_hub.stop.hold_active) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "hold");
  } else if (s_motion_hub.mlp.len < s_motion_hub.mlp.params.window_frames) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "win %d/%d",
             s_motion_hub.mlp.len, s_motion_hub.mlp.params.window_frames);
  } else if (s_motion_hub.mlp.last_name) {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "%s %.2f",
             s_motion_hub.mlp.last_name, (double)s_motion_hub.mlp.last_prob);
  } else {
    snprintf(s_motion_lcd, sizeof s_motion_lcd, "hand");
  }
}

static void app_motion_emit(int hand_detected, float confidence, uint32_t now,
                            roi_t *roi, ld_point_t lm[LD_LANDMARK_NB])
{
  float xy[LD_LANDMARK_NB][2];
  const char *name;
  int i;

  app_motion_init();
  if (hand_detected && roi != NULL && lm != NULL) {
    for (i = 0; i < LD_LANDMARK_NB; i++) {
      ld_point_t decoded;

      decode_ld_landmark(roi, &lm[i], &decoded);
      xy[i][0] = decoded.x;
      xy[i][1] = decoded.y;
    }
    name = motion_hub_update(&s_motion_hub, 1, confidence, xy, (int64_t)now);
  } else {
    name = motion_hub_update(&s_motion_hub, 0, 0.0f, NULL, (int64_t)now);
  }
  if (name) {
    char line[APP_UART_LINE_MAX];
    int n;

    s_motion_last_name = name;
    s_motion_last_tick = now;
    n = snprintf(line, sizeof line, "motion: %s\n", name);
    if (n > 0)
      app_uart_post_line(line, n < (int)sizeof line ? n : (int)sizeof line - 1);
  }
  app_motion_refresh_lcd(hand_detected, now);
}
#endif

#if APP_JSON_ENABLE || APP_MOTION_ENABLE
static void app_send_landmarks_json(int hand_detected, float confidence, uint32_t pd_ms,
                                    uint32_t hl_ms, float nn_period_ms, roi_t *roi,
                                    ld_point_t lm[LD_LANDMARK_NB], int json_uart)
{
  static uint32_t last_send_ms;
  uint32_t now = HAL_GetTick();
#if APP_JSON_ENABLE
  char buf[APP_UART_JSON_MAX];
  int pos;
  int i;
#endif

  if ((now - last_send_ms) < APP_JSON_INTERVAL_MS)
    return;
  last_send_ms = now;

#if APP_JSON_ENABLE
  /* json_uart follows TAMP / is_pd_displayed. motion: is independent.
   * Format here (CPU); blocking USART send runs on the uart task. */
  if (json_uart && !hand_detected) {
    pos = snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"hand\":false}\n",
                   (unsigned long)now);
    if (pos > 0)
      app_uart_post_json(buf, pos < (int)sizeof(buf) ? pos : (int)sizeof(buf) - 1);
  } else if (json_uart) {
    pos = snprintf(buf, sizeof(buf),
                   "{\"ts\":%lu,\"hand\":true,\"conf\":%.3f,\"pd_ms\":%lu,\"hl_ms\":%lu,"
                   "\"fps\":%.1f,\"lm\":[",
                   (unsigned long)now, (double)confidence, (unsigned long)pd_ms,
                   (unsigned long)hl_ms, (double)(1000.0f / nn_period_ms));

    if (pos > 0 && pos < (int)sizeof(buf) - 4) {
      int json_ok = 1;

      for (i = 0; i < LD_LANDMARK_NB; i++) {
        ld_point_t decoded;
        int n;

        decode_ld_landmark(roi, &lm[i], &decoded);
        n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s[%.1f,%.1f]",
                     i ? "," : "", (double)decoded.x, (double)decoded.y);
        if (n <= 0 || (pos + n) >= (int)sizeof(buf) - 4) {
          json_ok = 0;
          break;
        }
        pos += n;
      }
      if (json_ok && (pos + 3) < (int)sizeof(buf)) {
        buf[pos++] = ']';
        buf[pos++] = '}';
        buf[pos++] = '\n';
        app_uart_post_json(buf, pos);
      }
    }
  }
#else
  (void)pd_ms;
  (void)hl_ms;
  (void)nn_period_ms;
  (void)json_uart;
#endif

#if APP_MOTION_ENABLE
  app_motion_emit(hand_detected, confidence, now, roi, lm);
#endif
}
#endif

static void display_ld_hand(hand_info_t *hand)
{
  const int disk_radius = DISK_RADIUS;
  roi_t *roi = &hand->roi;
  int x[LD_LANDMARK_NB];
  int y[LD_LANDMARK_NB];
  int is_clamped[LD_LANDMARK_NB];
  ld_point_t decoded;
  int i;

  for (i = 0; i < LD_LANDMARK_NB; i++) {
    decode_ld_landmark(roi, &hand->ld_landmarks[i], &decoded);
    x[i] = (int)decoded.x;
    y[i] = (int)decoded.y;
    is_clamped[i] = clamp_point_with_margin(&x[i], &y[i], disk_radius);
  }

  for (i = 0; i < LD_LANDMARK_NB; i++) {
    if (is_clamped[i])
      continue;
    UTIL_LCD_FillCircle(x[i], y[i], disk_radius, UTIL_LCD_COLOR_YELLOW);
  }

  for (i = 0; i < LD_BINDING_NB; i++) {
    if (is_clamped[ld_bindings_idx[i][0]] || is_clamped[ld_bindings_idx[i][1]])
      continue;
    UTIL_LCD_DrawLine(x[ld_bindings_idx[i][0]], y[ld_bindings_idx[i][0]],
                      x[ld_bindings_idx[i][1]], y[ld_bindings_idx[i][1]],
                      UTIL_LCD_COLOR_BLACK);
  }
}

void display_hand(display_info_t *info, hand_info_t *hand)
{
  if (info->is_pd_displayed) {
    display_pd_hand(&hand->pd_hands);
    display_roi(&hand->roi);
  }
  if (info->is_ld_displayed)
    display_ld_hand(hand);
}

static void Display_NetworkOutput(display_info_t *info)
{
  int line_nb = 0;
  float nn_fps;
  int i;
#if !TENOMI_BUILD_ATON_OSAL
  float cpu_load_one_second;
#endif

  /* clear previous ui */
  UTIL_LCD_FillRect(lcd_fg_area.X0, lcd_fg_area.Y0, lcd_fg_area.XSize, lcd_fg_area.YSize, 0x00000000); /* Clear previous boxes */

  /* draw metrics */
  nn_fps = 1000.0 / info->nn_period_ms;
#if TENOMI_BUILD_ATON_OSAL
  /* Idle-time stats are not available on μT-Kernel 3; show OS + FW version instead of Cpu load/%. */
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "uT-kernel3");
  line_nb += 1;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "   %s", APP_VERSION_STRING);
  line_nb += 2;
#else
  cpuload_update(&cpu_load);
  cpuload_get_info(&cpu_load, NULL, &cpu_load_one_second, NULL);
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb),  RIGHT_MODE, "Cpu load");
  line_nb += 1;
  if (cpu_load_one_second != cpu_load_one_second)
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "   --");
  else
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "   %.1f%%", cpu_load_one_second);
  line_nb += 2;
#endif
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Inferences");
  line_nb += 1;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, " pd %2ums", info->pd_ms);
  line_nb += 1;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, " hl %2ums", info->hl_ms);
  line_nb += 2;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "  %.1f FPS", nn_fps);
  line_nb += 1;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE,
                      info->is_pd_displayed ? " json on" : " json off");
  line_nb += 2;
#if APP_MOTION_ENABLE
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Motion");
  line_nb += 1;
  UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, " %s",
                      info->motion_lcd[0] ? info->motion_lcd : "idle");
  line_nb += 2;
#endif
  if (DBG_INFO) {
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Display");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "   %ums", info->disp_ms);
    line_nb += 1;
  }

  /* display palm detector output */
  for (i = 0; i < info->pd_hand_nb; i++) {
    if (info->hands[i].is_valid)
      display_hand(info, &info->hands[i]);
  }

  if (DBG_INFO)
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb),  RIGHT_MODE, "pd : %5.1f %%", info->pd_max_prob * 100);
}

static void Run_Inference_Palm_Detector(stai_network *network_instance) {
  stai_return_code ret;

  do {
    ret = stai_palm_detector_run(network_instance, STAI_MODE_ASYNC);
    if (ret == STAI_RUNNING_WFE) {
      LL_ATON_OSAL_WFE();
    }
  } while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

  /* Async path ends with STAI_DONE (0x12). */
  (void)ret;

  ret = stai_ext_palm_detector_new_inference(network_instance);
  assert(ret == STAI_SUCCESS);
}

static void Run_Inference_Hand_Landmark(stai_network *network_instance) {
  stai_return_code ret;

  do {
    ret = stai_hand_landmark_run(network_instance, STAI_MODE_ASYNC);
    if (ret == STAI_RUNNING_WFE) {
      LL_ATON_OSAL_WFE();
    }
  } while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

  (void)ret;

  ret = stai_ext_hand_landmark_new_inference(network_instance);
  assert(ret == STAI_SUCCESS);
}

static void palm_detector_init(pd_model_info_t *info)
{
  stai_ptr nn_out[STAI_PALM_DETECTOR_OUT_NUM] = {0};
  stai_network_info pd_info;
  stai_size out_num;
  int ret;

  ret = stai_palm_detector_init(network_palm_detector_ctx);
  assert(ret == STAI_SUCCESS);

  /* model info */
  info->nn_in_len = STAI_PALM_DETECTOR_IN_1_SIZE_BYTES;

  info->prob_out_len = STAI_PALM_DETECTOR_OUT_1_SIZE_BYTES;
  assert(info->prob_out_len == AI_PD_MODEL_PP_TOTAL_DETECTIONS * sizeof(float));
  info->boxes_out_len = STAI_PALM_DETECTOR_OUT_2_SIZE_BYTES;
  assert(info->boxes_out_len == AI_PD_MODEL_PP_TOTAL_DETECTIONS * sizeof(float) * 18);
  ret = stai_palm_detector_get_outputs(network_palm_detector_ctx, (stai_ptr *)nn_out, &out_num);
  assert(ret == STAI_SUCCESS);

  info->prob_out = (float *)nn_out[0];
  info->boxes_out = (float *)nn_out[1];

  /* post processor info */
  ret = stai_palm_detector_get_info(network_palm_detector_ctx, &pd_info);
  assert(ret == STAI_SUCCESS);
  ret = app_postprocess_init(&info->static_param, &pd_info);
  assert(ret == AI_PD_POSTPROCESS_ERROR_NO);
}

static int palm_detector_run(uint8_t *buffer, pd_model_info_t *info, uint32_t *pd_exec_time)
{
  uint32_t start_ts;
  int hand_nb;
  int ret;
  int i;

  start_ts = HAL_GetTick();
  /* Note that we don't need to clean/invalidate those input buffers since they are only accessed in hardware */
  ret = stai_palm_detector_set_inputs(network_palm_detector_ctx, &buffer, STAI_PALM_DETECTOR_IN_NUM);
  assert(ret == STAI_SUCCESS);

  Run_Inference_Palm_Detector(network_palm_detector_ctx);

  ret = app_postprocess_run((void * []){info->prob_out, info->boxes_out}, 2, &info->pd_out, &info->static_param);
  assert(ret == AI_PD_POSTPROCESS_ERROR_NO);
  hand_nb = MIN(info->pd_out.box_nb, PD_MAX_HAND_NB);

  for (i = 0; i < hand_nb; i++) {
    cvt_pd_coord_to_screen_coord(&info->pd_out.pOutData[i]);
    pd_box_to_roi(&info->pd_out.pOutData[i], &rois[i]);
  }

  /* Discard nn_out region (used by pp_outputs variables) to avoid Dcache evictions during nn inference */
  CACHE_OP(SCB_InvalidateDCache_by_Addr(info->prob_out, info->prob_out_len));
  CACHE_OP(SCB_InvalidateDCache_by_Addr(info->boxes_out, info->boxes_out_len));

  *pd_exec_time = HAL_GetTick() - start_ts;

  return hand_nb;
}

static void hand_landmark_init(hl_model_info_t *info)
{
  stai_ptr nn_out[STAI_HAND_LANDMARK_OUT_NUM] = {0};
  stai_size in_num;
  stai_size out_num;
  int ret;

  ret = stai_hand_landmark_init(network_hand_landmark_ctx);
  assert(ret == STAI_SUCCESS);

  info->nn_in_len = STAI_HAND_LANDMARK_IN_1_SIZE_BYTES;
  ret = stai_hand_landmark_get_inputs(network_hand_landmark_ctx, &info->nn_in, &in_num);
  assert(ret == STAI_SUCCESS);

  info->prob_out_len = STAI_HAND_LANDMARK_OUT_3_SIZE_BYTES;
  assert(info->prob_out_len == sizeof(float));
  info->landmarks_out_len = STAI_HAND_LANDMARK_OUT_4_SIZE_BYTES;
  assert(info->landmarks_out_len == sizeof(float) * 63);
  ret = stai_hand_landmark_get_outputs(network_hand_landmark_ctx, nn_out, &out_num);
  assert(ret == STAI_SUCCESS);

  info->prob_out = (float *)nn_out[2];
  info->landmarks_out = (float *)nn_out[3];
}

#if HAS_ROTATION_SUPPORT == 0
static int hand_landmark_prepare_input(uint8_t *buffer, roi_t *roi, hl_model_info_t *info)
{
  float corners_f[4][2];
  int corners[4][2];
  uint8_t* out_data;
  size_t height_out;
  uint8_t *in_data;
  size_t height_in;
  size_t width_out;
  size_t width_in;
  int is_clamped;

  /* defaults when no clamping occurs */
  out_data = info->nn_in;
  width_out = LD_WIDTH;
  height_out = LD_HEIGHT;

  roi_to_corners(roi, corners_f);
  is_clamped = clamp_corners(corners_f, corners);

  /* If clamp perform a partial resize */
  if (is_clamped) {
    int offset_x;
    int offset_y;

    /* clear target memory since resize will partially write it */
    memset(info->nn_in, 0, info->nn_in_len);

    /* compute start address of output buffer */
    if (corners[0][0] == (int)corners_f[0][0])
      offset_x = 0;
    else
      offset_x = (int)roundf(((corners[0][0] - corners_f[0][0]) * LD_WIDTH) / (corners_f[2][0] - corners_f[0][0]));
    if (corners[0][1] == (int)corners_f[0][1])
      offset_y = 0;
    else
      offset_y = (int)roundf(((corners[0][1] - corners_f[0][1]) * LD_HEIGHT) / (corners_f[2][1] - corners_f[0][1]));
    out_data += offset_y * (int)LD_WIDTH * DISPLAY_BPP + offset_x * DISPLAY_BPP;

    /* compute output width and height */
    if (offset_x)
      width_out = (int)roundf(((corners_f[2][0] - corners[0][0]) * LD_WIDTH) / (corners_f[2][0] - corners_f[0][0]));
    else if (corners[2][0] < lcd_bg_area.XSize-1)
      width_out = LD_WIDTH;
    else
      width_out = (int)roundf(((corners[2][0] - corners_f[0][0]) * LD_WIDTH) / (corners_f[2][0] - corners_f[0][0]));
    if (offset_y)
      height_out = (int)roundf(((corners_f[2][1] - corners[0][1]) * LD_HEIGHT) / (corners_f[2][1] - corners_f[0][1]));
    else if (corners[2][1] < lcd_bg_area.YSize-1)
      height_out = LD_HEIGHT;
    else
      height_out = (int)roundf(((corners[2][1] - corners_f[0][1]) * LD_HEIGHT) / (corners_f[2][1] - corners_f[0][1]));

    assert(width_out > 0);
    assert(height_out > 0);
    assert(width_out <= LD_WIDTH);
    assert(height_out <= LD_HEIGHT);
    assert(offset_x >= 0);
    assert(offset_y >= 0);
    if (offset_x > 0)
      assert(width_out + offset_x == LD_WIDTH);
    if (offset_y > 0)
      assert(height_out + offset_y == LD_HEIGHT);
    {
      uint8_t* out_data_end;

      out_data_end = out_data + (int)LD_WIDTH * DISPLAY_BPP * (height_out - 1) + DISPLAY_BPP * width_out - 1;

      assert(out_data_end >= info->nn_in);
      assert(out_data_end < info->nn_in + info->nn_in_len);
    }
  }

  in_data = buffer + corners[0][1] * LCD_BG_WIDTH * DISPLAY_BPP + corners[0][0]* DISPLAY_BPP;
  width_in = corners[2][0] - corners[0][0];
  height_in = corners[2][1] - corners[0][1];

  assert(width_in > 0);
  assert(height_in > 0);
  {
    uint8_t* in_data_end;

    in_data_end = in_data + LCD_BG_WIDTH * DISPLAY_BPP * (height_in - 1) + DISPLAY_BPP * width_in - 1;

    assert(in_data_end >= buffer);
    assert(in_data_end < buffer + LCD_BG_WIDTH * LCD_BG_HEIGHT * DISPLAY_BPP);
  }

  mve_resize_bilinear_iu8ou8_with_strides(in_data, out_data, LCD_BG_WIDTH * DISPLAY_BPP, LD_WIDTH * DISPLAY_BPP,
                                          width_in, height_in, width_out, height_out, DISPLAY_BPP, pScratch);

  return 0;
}
#else
static void app_transform(nema_matrix3x3_t t, app_v3_t v)
{
  app_v3_t r;
  int i;

  for (i = 0; i < 3; i++)
    r[i] = t[i][0] * v[0] + t[i][1] * v[1] + t[i][2] * v[2];

  for (i = 0; i < 3; i++)
    v[i] = r[i];
}

static int hand_landmark_prepare_input(uint8_t *buffer, roi_t *roi, hl_model_info_t *info)
{
  app_v3_t vertex[] = {
    {           0,             0, 1},
    {LCD_BG_WIDTH,             0, 1},
    {LCD_BG_WIDTH, LCD_BG_HEIGHT, 1},
    {           0, LCD_BG_HEIGHT, 1},
  };
  GFXMMU_BuffersTypeDef buffers = { 0 };
  nema_matrix3x3_t t;
  int ret;
  int i;

  buffers.Buf0Address = (uint32_t) info->nn_in;
  ret = HAL_GFXMMU_ModifyBuffers(&hgfxmmu, &buffers);
  assert(ret == HAL_OK);

  /* bind destination texture */
  nema_bind_dst_tex(GFXMMU_VIRTUAL_BUFFER0_BASE, LD_WIDTH, LD_HEIGHT, NEMA_RGBA8888, -1);
  nema_set_clip(0, 0, LD_WIDTH, LD_HEIGHT);
  nema_clear(0);
  /* bind source texture */
  nema_bind_src_tex((uintptr_t) buffer, LCD_BG_WIDTH, LCD_BG_HEIGHT, NEMA_RGBA8888, -1, NEMA_FILTER_BL);
  nema_enable_tiling(1);
  nema_set_blend_blit(NEMA_BL_SRC);

  /* let's go */
  nema_mat3x3_load_identity(t);
  nema_mat3x3_translate(t, -roi->cx, -roi->cy);
  nema_mat3x3_rotate(t, nema_rad_to_deg(-roi->rotation));
  nema_mat3x3_scale(t, LD_WIDTH / roi->w, LD_HEIGHT / roi->h);
  nema_mat3x3_translate(t, LD_WIDTH / 2, LD_HEIGHT / 2);
  for (i = 0 ; i < 4; i++)
    app_transform(t, vertex[i]);
  nema_blit_quad_fit(vertex[0][0], vertex[0][1], vertex[1][0], vertex[1][1],
                     vertex[2][0], vertex[2][1], vertex[3][0], vertex[3][1]);

  nema_cl_submit(&cl);
  nema_cl_wait(&cl);
  HAL_ICACHE_Invalidate();

  assert(!nema_get_error());

  return 0;
}
#endif

static int hand_landmark_run(uint8_t *buffer, hl_model_info_t *info, roi_t *roi,
                             ld_point_t ld_landmarks[LD_LANDMARK_NB])
{
  static float ld_filtered_ms = 0;
  uint32_t hl_ms;
  int is_clamped;
  int is_valid;
  int ret;

  is_clamped = hand_landmark_prepare_input(buffer, roi, info);
  CACHE_OP(SCB_CleanInvalidateDCache_by_Addr(info->nn_in, info->nn_in_len));
  if (is_clamped)
    return 0;

  hl_ms = HAL_GetTick();
  Run_Inference_Hand_Landmark(network_hand_landmark_ctx);
  hl_ms = HAL_GetTick() - hl_ms;

  is_valid = ld_post_process(info->prob_out, info->landmarks_out, ld_landmarks);

  /* Discard nn_out region (used by pp_input and pp_outputs variables) to avoid Dcache evictions during nn inference */
  CACHE_OP(SCB_InvalidateDCache_by_Addr(info->prob_out, info->prob_out_len));
  CACHE_OP(SCB_InvalidateDCache_by_Addr(info->landmarks_out, info->landmarks_out_len));

  ld_filtered_ms = USE_FILTERED_TS ? (7 * ld_filtered_ms + hl_ms) / 8 : hl_ms;
  ret = xSemaphoreTake(disp.lock, portMAX_DELAY);
  assert(ret == pdTRUE);
  disp.info.hl_ms = is_valid ? (int)ld_filtered_ms : 0;
  ret = xSemaphoreGive(disp.lock);
  assert(ret == pdTRUE);

  return is_valid;
}

#if HAS_ROTATION_SUPPORT == 1
static void app_rot_init(hl_model_info_t *info)
{
  GFXMMU_PackingTypeDef packing = { 0 };
  int ret;

  printf("init nema\n");
  nema_init();
  assert(!nema_get_error());
  nema_ext_hold_enable(2);
  nema_ext_hold_irq_enable(2);
  nema_ext_hold_enable(3);
  nema_ext_hold_irq_enable(3);
  printf("init nema DONE %s\n", nema_get_sw_device_name());

  hgfxmmu.Instance = GFXMMU;
  hgfxmmu.Init.BlockSize = GFXMMU_12BYTE_BLOCKS;
  hgfxmmu.Init.AddressTranslation = DISABLE;
  ret = HAL_GFXMMU_Init(&hgfxmmu);
  assert(ret == HAL_OK);

  packing.Buffer0Activation = ENABLE;
  packing.Buffer0Mode       = GFXMMU_PACKING_MSB_REMOVE;
  packing.DefaultAlpha      = 0xff;
  ret = HAL_GFXMMU_ConfigPacking(&hgfxmmu, &packing);
  assert(ret == HAL_OK);

  cl = nema_cl_create_sized(8192);
  nema_cl_bind_circular(&cl);
}
#endif

static float ld_compute_rotation(ld_point_t lm[LD_LANDMARK_NB])
{
  float x0, y0, x1, y1;
  float rotation;

  x0 = lm[0].x;
  y0 = lm[0].y;
  x1 = lm[9].x;
  y1 = lm[9].y;

  rotation = M_PI * 0.5 - atan2f(-(y1 - y0), x1 - x0);

  return pd_cook_rotation(pd_normalize_angle(rotation));
}

static void ld_to_roi(ld_point_t lm[LD_LANDMARK_NB], roi_t *roi, pd_pp_box_t *next_pd)
{
  const int pd_to_ld_idx[AI_PD_MODEL_PP_NB_KEYPOINTS] = {0, 5, 9, 13, 17, 1, 2};
  const int indices[] = {0, 1, 2, 3, 5, 6, 9, 10, 13, 14, 17, 18};
  float max_x, max_y, min_x, min_y;
  int i;

  max_x = max_y = -10000;
  min_x = min_y =  10000;

  roi->rotation = ld_compute_rotation(lm);

  for (i = 0; i < ARRAY_NB(indices); i++) {
    max_x = MAX(max_x, lm[indices[i]].x);
    max_y = MAX(max_y, lm[indices[i]].y);
    min_x = MIN(min_x, lm[indices[i]].x);
    min_y = MIN(min_y, lm[indices[i]].y);
  }

  roi->cx = (max_x + min_x) / 2;
  roi->cy = (max_y + min_y) / 2;
  roi->w = (max_x - min_x);
  roi->h = (max_y - min_y);

  next_pd->x_center = roi->cx;
  next_pd->y_center = roi->cy;
  next_pd->width = roi->w;
  next_pd->height = roi->h;
  for (i = 0; i < AI_PD_MODEL_PP_NB_KEYPOINTS; i++) {
    next_pd->pKps[i].x = lm[pd_to_ld_idx[i]].x;
    next_pd->pKps[i].y = lm[pd_to_ld_idx[i]].y;
  }
}

static void compute_next_roi(roi_t *src, ld_point_t lm_in[LD_LANDMARK_NB], roi_t *next, pd_pp_box_t *next_pd)
{
  const float shift_x = 0;
  const float shift_y = -0.1;
  const float scale = 2.0;
  ld_point_t lm[LD_LANDMARK_NB];
  roi_t roi;
  int i;

  for (i = 0; i < LD_LANDMARK_NB; i++)
    decode_ld_landmark(src, &lm_in[i], &lm[i]);

  ld_to_roi(lm, &roi, next_pd);
  roi_shift_and_scale(&roi, shift_x, shift_y, scale, scale);

#if HAS_ROTATION_SUPPORT == 0
  /* In that case we can cancel rotation. This ensure corners are corrected oriented */
  roi.rotation = 0;
#endif

  *next = roi;
}

static void nn_thread_fct(void *arg)
{
  float nn_period_filtered_ms = 0;
  float pd_filtered_ms = 0;
  hl_model_info_t hl_info;
  pd_model_info_t pd_info;
  uint32_t nn_period_ms;
  uint32_t nn_period[2];
  uint8_t *nn_pipe_dst;
  pd_pp_point_t box_next_keypoints[AI_PD_MODEL_PP_NB_KEYPOINTS];
  pd_pp_box_t box_next;
  int is_tracking = 0;
  roi_t roi_next;
  uint32_t pd_ms;
  int ret;
  int j;

  /* Current tracking algo only support single hand */
  assert(PD_MAX_HAND_NB == 1);

  /* setup models buffer info */
  TENOMI_APP_LOG("hand_landmark: nn thread start\n");
  ret = stai_runtime_init();
  if (ret != STAI_SUCCESS) {
    HL_ERR((UB *)"hand_landmark: ERROR stai_runtime_init %d\n", ret);
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  TENOMI_APP_LOG("hand_landmark: palm_detector_init...\n");
  palm_detector_init(&pd_info);
  TENOMI_APP_LOG("hand_landmark: palm_detector_init OK\n");
  box_next.pKps = box_next_keypoints;
#if !(TENOMI_M4_PD_ONLY)
  TENOMI_APP_LOG("hand_landmark: hand_landmark_init...\n");
  hand_landmark_init(&hl_info);
  TENOMI_APP_LOG("hand_landmark: hand_landmark_init OK (M5)\n");

#if HAS_ROTATION_SUPPORT == 1
  app_rot_init(&hl_info);
#endif
#else
  memset(&hl_info, 0, sizeof(hl_info));
#endif

  /*** App Loop ***************************************************************/
  nn_period[1] = HAL_GetTick();
  nn_pipe_dst = bqueue_get_free(&nn_input_queue, 0);
  assert(nn_pipe_dst);
  TENOMI_APP_LOG("hand_landmark: CAM_NNPipe_Start...\n");
  CAM_NNPipe_Start(nn_pipe_dst, CMW_MODE_CONTINUOUS);
  TENOMI_APP_LOG("hand_landmark: CAM_NNPipe_Start OK (waiting frames)\n");
#if TENOMI_M4_REAL_NN
  /* Without NOR MMP, NPU weight fetch can hang the bus (black LCD / LED stuck). */
  if (!hl_xspi_nor_ready()) {
    stai_ptr discard;
    HL_ERR((UB *)"hand_landmark: ERROR NOR not ready — skip NN\n");
    while (1) {
      discard = bqueue_get_ready(&nn_input_queue);
      (void)discard;
      bqueue_put_free(&nn_input_queue);
      vTaskDelay(pdMS_TO_TICKS(30));
    }
  }
#endif
  while (1)
  {
    stai_ptr capture_buffer;
    int idx_for_resize;
    static uint32_t s_nn_loops;

    nn_period[0] = nn_period[1];
    nn_period[1] = HAL_GetTick();
    nn_period_ms = nn_period[1] - nn_period[0];
    nn_period_filtered_ms = USE_FILTERED_TS ? (15 * nn_period_filtered_ms + nn_period_ms) / 16 : nn_period_ms;

    capture_buffer = bqueue_get_ready(&nn_input_queue);
    assert(capture_buffer);
    idx_for_resize = frame_event_nb_for_resize % DISPLAY_BUFFER_NB;

#if TENOMI_M4_PD_ONLY
    (void)idx_for_resize;
    (void)roi_next;
    /* Always re-run palm detector (no HL tracking lock) */
    is_tracking = palm_detector_run(capture_buffer, &pd_info, &pd_ms);
    box_next.prob = pd_info.pd_out.pOutData[0].prob;
    pd_filtered_ms = USE_FILTERED_TS ? (7 * pd_filtered_ms + pd_ms) / 8 : pd_ms;
    bqueue_put_free(&nn_input_queue);
    s_nn_loops++;
#else
    /* Only start palm detector when not tracking hand */
    if (!is_tracking) {
      is_tracking = palm_detector_run(capture_buffer, &pd_info, &pd_ms);
      box_next.prob = pd_info.pd_out.pOutData[0].prob;
    } else {
      rois[0] = roi_next;
      copy_pd_box(&pd_info.pd_out.pOutData[0], &box_next);
      pd_ms = 0;
    }
    pd_filtered_ms = USE_FILTERED_TS ? (7 * pd_filtered_ms + pd_ms) / 8 : pd_ms;
    bqueue_put_free(&nn_input_queue);

    /* then run hand landmark detector if needed */
    if (is_tracking) {
      is_tracking = hand_landmark_run(lcd_bg_buffer[idx_for_resize], &hl_info, &rois[0], ld_landmarks[0]);
      CACHE_OP(SCB_InvalidateDCache_by_Addr(lcd_bg_buffer[idx_for_resize], sizeof(lcd_bg_buffer[idx_for_resize])));
      if (is_tracking)
        compute_next_roi(&rois[0], ld_landmarks[0], &roi_next, &box_next);
    }
    s_nn_loops++;
#endif

    /* update display stats */
#if APP_JSON_ENABLE || APP_MOTION_ENABLE
    int hl_json_ms;
    int json_uart;
#endif
    ret = xSemaphoreTake(disp.lock, portMAX_DELAY);
    assert(ret == pdTRUE);
    disp.info.pd_ms = is_tracking ? 0 : (int)pd_filtered_ms;
    disp.info.nn_period_ms = nn_period_filtered_ms;
    disp.info.pd_hand_nb = is_tracking;
    disp.info.pd_max_prob = pd_info.pd_out.pOutData[0].prob;
    disp.info.hands[0].is_valid = is_tracking;
    copy_pd_box(&disp.info.hands[0].pd_hands, &pd_info.pd_out.pOutData[0]);
    disp.info.hands[0].roi = rois[0];
    for (j = 0; j < LD_LANDMARK_NB; j++)
      disp.info.hands[0].ld_landmarks[j] = ld_landmarks[0][j];
#if APP_JSON_ENABLE || APP_MOTION_ENABLE
    hl_json_ms = disp.info.hl_ms;
    json_uart = disp.info.is_pd_displayed;
#endif
    ret = xSemaphoreGive(disp.lock);
    assert(ret == pdTRUE);

    /* Overlay first: JSON USART must not delay disp.update. */
    xSemaphoreGive(disp.update);

#if APP_JSON_ENABLE || APP_MOTION_ENABLE
    app_send_landmarks_json(is_tracking, pd_info.pd_out.pOutData[0].prob,
                            (uint32_t)pd_filtered_ms, (uint32_t)hl_json_ms,
                            nn_period_filtered_ms, &rois[0], ld_landmarks[0],
                            json_uart);
#endif
#if APP_MOTION_ENABLE
    ret = xSemaphoreTake(disp.lock, portMAX_DELAY);
    assert(ret == pdTRUE);
    disp.info.motion_name = s_motion_last_name;
    disp.info.motion_tick = s_motion_last_tick;
    memcpy(disp.info.motion_lcd, s_motion_lcd, sizeof disp.info.motion_lcd);
    ret = xSemaphoreGive(disp.lock);
    assert(ret == pdTRUE);
#endif
  }
}

static void dp_wait_ltdc_reload(void)
{
#ifdef STM32N6570_DK_REV
  uint32_t t0;

  if (!dp_fg_reload_pending)
    return;

  /* ReloadLayer enables RR IE; this project has no LTDC_IRQHandler. */
  __HAL_LTDC_DISABLE_IT(&hlcd_ltdc, LTDC_IT_RR);
  t0 = HAL_GetTick();
  while (__HAL_LTDC_GET_FLAG(&hlcd_ltdc, LTDC_FLAG_RR) == 0U) {
    if ((HAL_GetTick() - t0) >= 50U) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  __HAL_LTDC_CLEAR_FLAG(&hlcd_ltdc, LTDC_FLAG_RR);
  __HAL_LTDC_DISABLE_IT(&hlcd_ltdc, LTDC_IT_RR);
  dp_fg_reload_pending = 0;
#else
  (void)0;
#endif
}

static void dp_update_drawing_area()
{
  /* Wait for the previous FG reload before touching this buffer: commit already
   * swapped rd_idx onto the buffer still scanned until VBLANK. Skipping this
   * wait would FillRect the on-screen overlay. */
  dp_wait_ltdc_reload();

  /* Bind UTIL_LCD to the hidden FG buffer only. Do not program LTDC CFBAR yet:
   * camera VSYNC coincides with LTDC VBLANK, so a pending shadow address would
   * start scanning a buffer that Display_NetworkOutput is still clearing. */
#ifdef STM32N6570_DK_REV
  hlcd_ltdc.LayerCfg[SCRL_LAYER_1].FBStartAdress =
      (uint32_t)lcd_fg_buffer[lcd_fg_buffer_rd_idx];
#else
  {
    int ret;

    __disable_irq();
    ret = SCRL_SetAddress_NoReload(lcd_fg_buffer[lcd_fg_buffer_rd_idx], SCRL_LAYER_1);
    assert(ret == HAL_OK);
    __enable_irq();
  }
#endif
}

static void dp_commit_drawing_area()
{
  int ret;

  __disable_irq();
#ifdef STM32N6570_DK_REV
  __HAL_LTDC_CLEAR_FLAG(&hlcd_ltdc, LTDC_FLAG_RR);
  ret = SCRL_SetAddress_NoReload(lcd_fg_buffer[lcd_fg_buffer_rd_idx], SCRL_LAYER_1);
  assert(ret == HAL_OK);
#endif
  ret = SCRL_ReloadLayer(SCRL_LAYER_1);
  assert(ret == HAL_OK);
#ifdef STM32N6570_DK_REV
  __HAL_LTDC_DISABLE_IT(&hlcd_ltdc, LTDC_IT_RR);
  dp_fg_reload_pending = 1;
#endif
  __enable_irq();

  /* Do not wait here: nn can Give disp.update and this thread can Take it
   * while the just-issued reload completes at the next VBLANK. */
  lcd_fg_buffer_rd_idx = 1 - lcd_fg_buffer_rd_idx;
}

static void on_ld_toggle_button_click(void *args)
{
  display_t *disp = (display_t *) args;
  int ret;

  ret = xSemaphoreTake(disp->lock, portMAX_DELAY);
  assert(ret == pdTRUE);
  disp->info.is_ld_displayed = !disp->info.is_ld_displayed;
  ret = xSemaphoreGive(disp->lock);
  assert(ret == pdTRUE);
}

static void on_pd_toggle_button_click(void *args)
{
  display_t *disp = (display_t *) args;
  int ret;
  int json_on;

  ret = xSemaphoreTake(disp->lock, portMAX_DELAY);
  assert(ret == pdTRUE);
  disp->info.is_pd_displayed = !disp->info.is_pd_displayed;
  json_on = disp->info.is_pd_displayed;
  if (json_on)
    disp->info.is_ld_displayed = 1;
  ret = xSemaphoreGive(disp->lock);
  assert(ret == pdTRUE);
#if APP_UART_TX_ENABLE
  {
    char line[APP_UART_LINE_MAX];
    int n;

    n = snprintf(line, sizeof line, "json: %s\n", json_on ? "on" : "off");
    if (n > 0)
      app_uart_post_line(line, n < (int)sizeof line ? n : (int)sizeof line - 1);
  }
#else
  printf("json: %s\n", json_on ? "on" : "off");
  (void)fflush(stdout);
#endif
}

static void dp_thread_fct(void *arg)
{
  button_t ld_toggle_button;
  button_t hd_toggle_button;
  uint32_t disp_ms = 0;
  display_info_t info;
  uint32_t ts;
  int ret;

#ifdef STM32N6570_DK_REV
  button_init(&ld_toggle_button, BUTTON_USER1, on_ld_toggle_button_click, &disp);
  button_init(&hd_toggle_button, BUTTON_TAMP, on_pd_toggle_button_click, &disp);
#else
  button_init(&ld_toggle_button, BUTTON_USER, on_ld_toggle_button_click, &disp);
  button_init(&hd_toggle_button, BUTTON_USER, on_pd_toggle_button_click, &disp);
#endif
  while (1)
  {
    ret = xSemaphoreTake(disp.update, portMAX_DELAY);
    assert(ret == pdTRUE);

    button_process(&ld_toggle_button);
    button_process(&hd_toggle_button);

    ret = xSemaphoreTake(disp.lock, portMAX_DELAY);
    assert(ret == pdTRUE);
    info = disp.info;
    ret = xSemaphoreGive(disp.lock);
    assert(ret == pdTRUE);
    info.disp_ms = disp_ms;

    ts = HAL_GetTick();
    dp_update_drawing_area();
    Display_NetworkOutput(&info);
    SCB_CleanDCache_by_Addr(lcd_fg_buffer[lcd_fg_buffer_rd_idx], LCD_FG_WIDTH * LCD_FG_HEIGHT* 2);
    dp_commit_drawing_area();
    disp_ms = HAL_GetTick() - ts;
  }
}

static void isp_thread_fct(void *arg)
{
  int ret;

  while (1) {
    ret = xSemaphoreTake(isp_sem, portMAX_DELAY);
    assert(ret == pdTRUE);

    CAM_IspUpdate();
  }
}

static void Display_init()
{
  SCRL_LayerConfig layers_config[2] = {
    {
      .origin = {lcd_bg_area.X0, lcd_bg_area.Y0},
      .size = {lcd_bg_area.XSize, lcd_bg_area.YSize},
#if HAS_ROTATION_SUPPORT == 0
      .format = SCRL_RGB888,
#else
      .format = SCRL_ARGB8888,
#endif
      .address = lcd_bg_buffer[lcd_bg_buffer_disp_idx],
    },
    {
      .origin = {lcd_fg_area.X0, lcd_fg_area.Y0},
      .size = {lcd_fg_area.XSize, lcd_fg_area.YSize},
      .format = SCRL_ARGB4444,
      .address = lcd_fg_buffer[1],
    },
  };
  SCRL_ScreenConfig screen_config = {
    .size = {lcd_bg_area.XSize, lcd_bg_area.YSize},
#ifdef SCR_LIB_USE_SPI
    .format = SCRL_RGB565,
#else
     .format = SCRL_YUV422, /* Use SCRL_RGB565 if host support this format to reduce cpu load */
#endif
    .address = screen_buffer,
    .fps = CAMERA_FPS,
  };
  int ret;

  TENOMI_APP_LOG("hand_landmark: Display_init SCRL_Init...\n");
  ret = SCRL_Init((SCRL_LayerConfig *[2]){&layers_config[0], &layers_config[1]}, &screen_config);
  assert(ret == 0);
  TENOMI_APP_LOG("hand_landmark: Display_init UTIL_LCD...\n");

  UTIL_LCD_SetLayer(SCRL_LAYER_1);
  UTIL_LCD_Clear(UTIL_LCD_COLOR_TRANSPARENT);
  UTIL_LCD_SetFont(&LCD_FONT);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
  UTIL_LCD_SetBackColor(UTIL_LCD_COLOR_TRANSPARENT);
  TENOMI_APP_LOG("hand_landmark: Display_init done\n");
}

#if TENOMI_C5_VIDEO_ONLY
/* Keep layer on a cleared buffer until the first completed capture frame. */
static void tenomi_lcd_blank(void)
{
  memset(lcd_bg_buffer, 0, sizeof(lcd_bg_buffer));
  CACHE_OP(SCB_CleanInvalidateDCache_by_Addr(lcd_bg_buffer, sizeof(lcd_bg_buffer)));
  TENOMI_APP_LOG("hand_landmark: LCD blank (no test pattern)\n");
}

static void tenomi_bind_lcd_to_capture(void)
{
  /* Capture starts in buffer[0]; show black buffer[1] until first frame IRQ. */
  lcd_bg_buffer_capt_idx = 0;
  lcd_bg_buffer_disp_idx = 1;
  reload_bg_layer(1);
  TENOMI_APP_LOG("hand_landmark: LCD ping-pong ready (capt=0 disp=1)\n");
}
#endif

void app_run()
{
  UBaseType_t isp_priority = FREERTOS_PRIORITY(2);
#if !(TENOMI_C5_VIDEO_ONLY)
  UBaseType_t dp_priority = FREERTOS_PRIORITY(-2);
  UBaseType_t nn_priority = FREERTOS_PRIORITY(1);
#if APP_UART_TX_ENABLE
  UBaseType_t uart_priority = FREERTOS_PRIORITY(-3);
#endif
#endif
  TaskHandle_t hdl;
  int ret;

  TENOMI_APP_LOG("hand_landmark: app_run Init application\n");
  /* Enable DWT so DWT_CYCCNT works when debugger not attached */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

  /* screen init */
  TENOMI_APP_LOG("hand_landmark: app_run clear LCD buffers\n");
  memset(lcd_bg_buffer, 0, sizeof(lcd_bg_buffer));
  CACHE_OP(SCB_CleanInvalidateDCache_by_Addr(lcd_bg_buffer, sizeof(lcd_bg_buffer)));
  memset(lcd_fg_buffer, 0, sizeof(lcd_fg_buffer));
  CACHE_OP(SCB_CleanInvalidateDCache_by_Addr(lcd_fg_buffer, sizeof(lcd_fg_buffer)));
  TENOMI_APP_LOG("hand_landmark: app_run Display_init...\n");
  Display_init();
  TENOMI_APP_LOG("hand_landmark: app_run Display_init OK\n");
#if TENOMI_C5_VIDEO_ONLY
  tenomi_lcd_blank();
#endif

#if !(TENOMI_C5_VIDEO_ONLY)
  /* create buffer queues */
  ret = bqueue_init(&nn_input_queue, 2, (uint8_t *[2]){nn_input_buffers[0], nn_input_buffers[1]});
  assert(ret == 0);

  cpuload_init(&cpu_load);
#else
  (void)ret;
  (void)cpu_load;
#endif

  /*** Camera Init ************************************************************/
  TENOMI_APP_LOG("hand_landmark: app_run CAM_Init...\n");
  CAM_Init();
  TENOMI_APP_LOG("hand_landmark: app_run CAM_Init OK\n");

#if TENOMI_C5_VIDEO_ONLY
  /* Short initial exposure: mid-range caused heavy motion blur / ghosting.
   * ISP AEC then trims brightness via the isp thread. */
  {
    ISP_SensorInfoTypeDef sinfo = {0};
    if (CMW_CAMERA_GetSensorInfo(&sinfo) == CMW_ERROR_NONE) {
      int32_t span = sinfo.exposure_max - sinfo.exposure_min;
      int32_t short_exp = sinfo.exposure_min + (span > 0 ? span / 16 : 0);
      int32_t modest_gain = sinfo.gain_min + (sinfo.gain_max - sinfo.gain_min) / 8;
      if (short_exp < sinfo.exposure_min) {
        short_exp = sinfo.exposure_min;
      }
      if (modest_gain < sinfo.gain_min) {
        modest_gain = sinfo.gain_min;
      }
      (void)CMW_CAMERA_SetExposure(short_exp);
      (void)CMW_CAMERA_SetGain(modest_gain);
    }
  }
#endif

  /* sems + mutex init */
  isp_sem = xSemaphoreCreateCountingStatic(1, 0, &isp_sem_buffer);
  assert(isp_sem);
#if !(TENOMI_C5_VIDEO_ONLY)
  disp.update = xSemaphoreCreateCountingStatic(1, 0, &disp.update_buffer);
  assert(disp.update);
  disp.lock = xSemaphoreCreateMutexStatic(&disp.lock_buffer);
  assert(disp.lock);
#if APP_UART_TX_ENABLE
  uart_tx.lock = xSemaphoreCreateMutexStatic(&uart_tx.lock_buffer);
  assert(uart_tx.lock);
  uart_tx.wake = xSemaphoreCreateCountingStatic(1, 0, &uart_tx.wake_buffer);
  assert(uart_tx.wake);
  uart_tx.json_len = 0;
  uart_tx.line_len = 0;
#endif
#endif

  /* Start LCD Display camera pipe stream */
  TENOMI_APP_LOG("hand_landmark: app_run CAM_DisplayPipe_Start...\n");
  CAM_DisplayPipe_Start(lcd_bg_buffer[0], CMW_MODE_CONTINUOUS);
  TENOMI_APP_LOG("hand_landmark: app_run CAM pipe started\n");
#if TENOMI_C5_VIDEO_ONLY
  tenomi_bind_lcd_to_capture();
#endif

  /* threads init */
#if !(TENOMI_C5_VIDEO_ONLY)
  hdl = xTaskCreateStatic(nn_thread_fct, "nn", configMINIMAL_STACK_SIZE * NN_THREAD_STACK_MULT, NULL, nn_priority,
                          nn_thread_stack, &nn_thread);
  assert(hdl != NULL);
  TENOMI_APP_LOG("hand_landmark: app_run nn thread created\n");
  hdl = xTaskCreateStatic(dp_thread_fct, "dp", configMINIMAL_STACK_SIZE * 2, NULL, dp_priority, dp_thread_stack,
                          &dp_thread);
  assert(hdl != NULL);
  TENOMI_APP_LOG("hand_landmark: app_run dp thread created\n");
#if APP_UART_TX_ENABLE
  hdl = xTaskCreateStatic(uart_thread_fct, "uart", configMINIMAL_STACK_SIZE * 2, NULL, uart_priority,
                          uart_thread_stack, &uart_thread);
  assert(hdl != NULL);
  TENOMI_APP_LOG("hand_landmark: app_run uart thread created\n");
#endif
#endif
  hdl = xTaskCreateStatic(isp_thread_fct, "isp", configMINIMAL_STACK_SIZE * 2, NULL, isp_priority, isp_thread_stack,
                          &isp_thread);
  assert(hdl != NULL);
  TENOMI_APP_LOG("hand_landmark: app_run isp thread created\n");
}

int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
  if (pipe == DCMIPP_PIPE1)
    app_main_pipe_frame_event();
  else if (pipe == DCMIPP_PIPE2)
    app_ancillary_pipe_frame_event();

  return HAL_OK;
}

int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
  if (pipe == DCMIPP_PIPE1)
    app_main_pipe_vsync_event();

  return HAL_OK;
}
