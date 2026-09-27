 /**
 ******************************************************************************
 * @file    app_cam.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#include <assert.h>
#include "app.h"
#include "cmw_camera.h"
#include "app_cam.h"
#include "app_config.h"
#include "utils.h"
#include "stai_palm_detector.h"

#ifndef TENOMI_M4_PD_ONLY
#define TENOMI_M4_PD_ONLY 0
#endif

#if (defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY)) || (TENOMI_M4_PD_ONLY)
#include <tm/tmonitor.h>
#include <tk/tkernel.h>
#define CAM_LOG(msg) do { } while (0)
#define CAM_LOG1(fmt, a) do { } while (0)
/* Soft fail: park task (LED tasks keep running) instead of abort() */
#define CAM_CHECK(cond, msg)                                                       \
  do {                                                                             \
    if (!(cond)) {                                                                 \
      tm_printf((UB *)(msg));                                                      \
      for (;;) {                                                                   \
        tk_dly_tsk(1000);                                                          \
      }                                                                            \
    }                                                                              \
  } while (0)
#else
#define CAM_LOG(msg)                                                               \
  do {                                                                             \
  } while (0)
#define CAM_LOG1(fmt, a)                                                           \
  do {                                                                             \
  } while (0)
#define CAM_CHECK(cond, msg) assert(cond)
#endif

/* Keep display output aspect ratio using crop area */
static void CAM_InitCropConfig(CMW_Manual_roi_area_t *roi, int sensor_width, int sensor_height)
{
  const float ratiox = (float)sensor_width / LCD_BG_WIDTH;
  const float ratioy = (float)sensor_height / LCD_BG_HEIGHT;
  const float ratio = MIN(ratiox, ratioy);

  assert(ratio >= 1);
  assert(ratio < 64);

  roi->width = (uint32_t) MIN(LCD_BG_WIDTH * ratio, sensor_width);
  roi->height = (uint32_t) MIN(LCD_BG_HEIGHT * ratio, sensor_height);
  roi->offset_x = (sensor_width - roi->width + 1) / 2;
  roi->offset_y = (sensor_height - roi->height + 1) / 2;
}

static void DCMIPP_PipeInitDisplay(int sensor_width, int sensor_height)
{
  CMW_DCMIPP_Conf_t dcmipp_conf;
  uint32_t hw_pitch;
  int ret;

  assert(LCD_BG_WIDTH >= LCD_BG_HEIGHT);

  dcmipp_conf.output_width = LCD_BG_WIDTH;
  dcmipp_conf.output_height = LCD_BG_HEIGHT;
  dcmipp_conf.output_format = DISPLAY_FORMAT;
  dcmipp_conf.output_bpp = DISPLAY_BPP;
  dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
  dcmipp_conf.enable_swap = 1;
  dcmipp_conf.enable_gamma_conversion = 0;
  CAM_InitCropConfig(&dcmipp_conf.manual_conf, sensor_width, sensor_height);
  ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &dcmipp_conf, &hw_pitch);
  assert(ret == HAL_OK);
  assert(hw_pitch == dcmipp_conf.output_width * dcmipp_conf.output_bpp);
}

static void DCMIPP_PipeInitNn(int sensor_width, int sensor_height)
{
  CMW_DCMIPP_Conf_t dcmipp_conf;
  uint32_t hw_pitch;
  int ret;

  assert(LCD_BG_HEIGHT <= LCD_BG_WIDTH);
  /* Keep screen aspect ratio. Consequence is that bottom of palm detector input will be black */
  dcmipp_conf.output_width = STAI_PALM_DETECTOR_IN_1_WIDTH;
  dcmipp_conf.output_height = (int) (STAI_PALM_DETECTOR_IN_1_HEIGHT * ((float)LCD_BG_HEIGHT / LCD_BG_WIDTH));
  dcmipp_conf.output_format = NN_FORMAT;
  dcmipp_conf.output_bpp = STAI_PALM_DETECTOR_IN_1_CHANNEL;
  dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
  dcmipp_conf.enable_swap = 1;
  dcmipp_conf.enable_gamma_conversion = 0;
  CAM_InitCropConfig(&dcmipp_conf.manual_conf, sensor_width, sensor_height);
  ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE2, &dcmipp_conf, &hw_pitch);
  assert(ret == HAL_OK);
  assert(hw_pitch == dcmipp_conf.output_width * dcmipp_conf.output_bpp);
}

static void DCMIPP_IpPlugInit(DCMIPP_HandleTypeDef *hdcmipp)
{
  DCMIPP_IPPlugConfTypeDef ipplug_conf = { 0 };
  int ret;

  ipplug_conf.MemoryPageSize = DCMIPP_MEMORY_PAGE_SIZE_256BYTES;

#if (defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY))
  /* Preview-only: CLIENT5 full DP (split → PIPE1_OVR/snow on this board). */
  ipplug_conf.Client = DCMIPP_CLIENT5; /* main rgb pipe */
  ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
  ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_3;
  ipplug_conf.DPREGStart = 0;
  ipplug_conf.DPREGEnd = 639;
  ipplug_conf.WLRURatio = 15;
  ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
  assert(ret == HAL_OK);
  CAM_LOG("hand_landmark: IpPlug CLIENT5 full DP (preview-safe)\n");
#else
  /* landmarks split: NN (CLIENT2) gets most BW; display (CLIENT5) remainder.
   * Outstanding NONE can stall PIPE2→PSRAM on this board; use 3 like CLIENT5. */
  ipplug_conf.Client = DCMIPP_CLIENT2; /* aux pipe */
  ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
  ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_3;
  ipplug_conf.DPREGStart = 0;
  ipplug_conf.DPREGEnd = 559; /* (4480 bytes / one line) */
  ipplug_conf.WLRURatio = 15; /* 16 parts of BW */
  ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
  assert(ret == HAL_OK);

  ipplug_conf.Client = DCMIPP_CLIENT5; /* main rgb pipe */
  ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
  ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_3;
  ipplug_conf.DPREGStart = 560;
  ipplug_conf.DPREGEnd = 639;
  ipplug_conf.WLRURatio = 0; /* 1 parts of BW */
  ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
  assert(ret == HAL_OK);
  CAM_LOG("hand_landmark: IpPlug CLIENT2+CLIENT5 split (NN+preview)\n");
#endif
}

static void DCMIPP_ReduceSpurious(DCMIPP_HandleTypeDef *hdcmipp)
{
  int ret;

  ret = HAL_DCMIPP_PIPE_EnableLineEvent(hdcmipp, DCMIPP_PIPE1, DCMIPP_MULTILINE_128_LINES);
  assert(ret == HAL_OK);
  ret = HAL_DCMIPP_PIPE_DisableLineEvent(hdcmipp, DCMIPP_PIPE1);
  assert(ret == HAL_OK);
}

void CAM_Init(void)
{
  CMW_Advanced_Config_t sensor_config;
  CMW_CameraInit_t cam_conf;
  CMW_Sensor_Name_t sensor;
  int ret;

#if (defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY)) || (TENOMI_M4_PD_ONLY)
  /* Avoid GetSensorName: it fully inits DCMIPP+sensor, then CMW_CAMERA_Init
   * does it again (double Probe/Init) and contributes to flaky first-frame stop. */
  sensor = CMW_IMX335_Sensor;
  CAM_LOG("hand_landmark: CAM_Init skip GetSensorName (IMX335)\n");
#else
  CAM_LOG("hand_landmark: CAM_Init GetSensorName...\n");
  ret = CMW_CAMERA_GetSensorName(&sensor);
  CAM_LOG1("hand_landmark: CAM_Init GetSensorName ret=%d\n", ret);
  CAM_LOG1("hand_landmark: CAM_Init sensor=%d (IMX335=expected)\n", (int)sensor);
  CAM_CHECK(ret == CMW_ERROR_NONE, "hand_landmark: ERROR GetSensorName (I2C/sensor probe)\n");
#endif

  sensor_config.selected_sensor = CMW_IMX335_Sensor;
  ret = CMW_CAMERA_SetDefaultSensorValues(&sensor_config);
  CAM_CHECK(ret == CMW_ERROR_NONE, "hand_landmark: ERROR SetDefaultSensorValues\n");

  /* Full IMX335 resolution; pipe crop handles LCD aspect */
  cam_conf.width = 2592;
  cam_conf.height = 1944;
  cam_conf.fps = CAMERA_FPS;
  cam_conf.mirror_flip = CAMERA_FLIP;
  CAM_LOG("hand_landmark: CAM_Init CMW_CAMERA_Init (IMX335)...\n");
  ret = CMW_CAMERA_Init(&cam_conf, sensor == CMW_IMX335_Sensor ? &sensor_config : NULL);
  CAM_LOG1("hand_landmark: CAM_Init CMW_CAMERA_Init ret=%d\n", ret);
  CAM_CHECK(ret == CMW_ERROR_NONE, "hand_landmark: ERROR CMW_CAMERA_Init\n");

  /* cam_conf.width / cam_conf.height now contains choose resolution */
  CAM_LOG("hand_landmark: CAM_Init pipes...\n");
  DCMIPP_IpPlugInit(CMW_CAMERA_GetDCMIPPHandle());
  DCMIPP_PipeInitDisplay(cam_conf.width, cam_conf.height);
#if !(defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY))
  DCMIPP_PipeInitNn(cam_conf.width, cam_conf.height);
  CAM_LOG("hand_landmark: NN pipe (PIPE2) configured\n");
#endif
  DCMIPP_ReduceSpurious(CMW_CAMERA_GetDCMIPPHandle());
  CAM_LOG("hand_landmark: CAM_Init done\n");
}

void CAM_DisplayPipe_Start(uint8_t *display_pipe_dst, uint32_t cam_mode)
{
  int ret;

  ret = CMW_CAMERA_Start(DCMIPP_PIPE1, display_pipe_dst, cam_mode);
  assert(ret == CMW_ERROR_NONE);
}

void CAM_NNPipe_Start(uint8_t *nn_pipe_dst, uint32_t cam_mode)
{
  int ret;

  ret = CMW_CAMERA_Start(DCMIPP_PIPE2, nn_pipe_dst, cam_mode);
  assert(ret == CMW_ERROR_NONE);
}

void CAM_IspUpdate(void)
{
  int ret;

  ret = CMW_CAMERA_Run();
  assert(ret == CMW_ERROR_NONE);
}

volatile uint32_t g_tenomi_pipe_err_cnt;
volatile uint32_t g_tenomi_pipe_err_id;

void CMW_CAMERA_PIPE_ErrorCallback(uint32_t pipe)
{
  /* Overrun / pipe error — cam_mon prints these under VIDEO_ONLY */
  g_tenomi_pipe_err_cnt++;
  g_tenomi_pipe_err_id = pipe;
}
