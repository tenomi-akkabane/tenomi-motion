/**
 * @file hl_app_stubs.c
 * @brief Phase C3/C5 stubs: NN/PP/MVE (+ optional CAM/LCD until TENOMI_C5_REAL_CAM_LCD)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "stm32n6xx_hal.h"
#include "app_cam.h"
#include "scrl.h"
#include "stm32_lcd.h"
#include "stm32_lcd_ex.h"
#include "stm32n6570_discovery.h"
#include "fonts.h"
#include "cmw_camera.h"
#include "app_postprocess.h"
#include "stai_palm_detector.h"
#include "stai_hand_landmark.h"
#include "mve_resize.h"

#ifndef TENOMI_C5_REAL_CAM_LCD
#define TENOMI_C5_REAL_CAM_LCD 0
#endif

#if !(TENOMI_C5_REAL_CAM_LCD)
/* ---- Font (app.c references Font20) ---- */
static const uint8_t s_font20_table[1] = {0};
sFONT Font20 = {s_font20_table, 10, 20};

/* ---- Camera ---- */
void CAM_Init(void) {}
void CAM_DisplayPipe_Start(uint8_t *display_pipe_dst, uint32_t cam_mode)
{
	(void)display_pipe_dst;
	(void)cam_mode;
}
void CAM_IspUpdate(void) {}

DCMIPP_HandleTypeDef *CMW_CAMERA_GetDCMIPPHandle(void)
{
	return NULL;
}

HAL_StatusTypeDef HAL_DCMIPP_PIPE_SetMemoryAddress(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe, uint32_t Memory,
                                                   uint32_t DstAddress)
{
	(void)hdcmipp;
	(void)Pipe;
	(void)Memory;
	(void)DstAddress;
	return HAL_OK;
}

/* ---- Screen layer ---- */
int SCRL_Init(SCRL_LayerConfig *layers_config[SCRL_LAYER_NB], SCRL_ScreenConfig *screen_config)
{
	(void)layers_config;
	(void)screen_config;
	return 0;
}

int SCRL_SetAddress_NoReload(void *address, SCRL_Layer layer)
{
	(void)address;
	(void)layer;
	return 0;
}

int SCRL_ReloadLayer(SCRL_Layer layer)
{
	(void)layer;
	return 0;
}

int SRCL_Update(void)
{
	return 0;
}

/* ---- UTIL_LCD (no-op) ---- */
static sFONT *s_util_font = &Font20;
static uint32_t s_util_fg = UTIL_LCD_COLOR_WHITE;

void UTIL_LCD_SetLayer(uint32_t Layer)
{
	(void)Layer;
}
void UTIL_LCD_Clear(uint32_t Color)
{
	(void)Color;
}
void UTIL_LCD_SetFont(sFONT *fonts)
{
	if (fonts != NULL) {
		s_util_font = fonts;
	}
}
sFONT *UTIL_LCD_GetFont(void)
{
	return s_util_font;
}
void UTIL_LCD_SetTextColor(uint32_t Color)
{
	s_util_fg = Color;
}
uint32_t UTIL_LCD_GetTextColor(void)
{
	return s_util_fg;
}
void UTIL_LCD_FillRect(uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height, uint32_t Color)
{
	(void)Xpos;
	(void)Ypos;
	(void)Width;
	(void)Height;
	(void)Color;
}
void UTIL_LCD_DrawRect(uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height, uint32_t Color)
{
	(void)Xpos;
	(void)Ypos;
	(void)Width;
	(void)Height;
	(void)Color;
}
void UTIL_LCD_DrawLine(uint32_t Xpos1, uint32_t Ypos1, uint32_t Xpos2, uint32_t Ypos2, uint32_t Color)
{
	(void)Xpos1;
	(void)Ypos1;
	(void)Xpos2;
	(void)Ypos2;
	(void)Color;
}
void UTIL_LCD_FillCircle(uint32_t Xpos, uint32_t Ypos, uint32_t Radius, uint32_t Color)
{
	(void)Xpos;
	(void)Ypos;
	(void)Radius;
	(void)Color;
}

void UTIL_LCDEx_PrintfAt(uint32_t x_pos, uint32_t y_pos, Text_AlignModeTypdef mode, const char *format, ...)
{
	(void)x_pos;
	(void)y_pos;
	(void)mode;
	(void)format;
}

/* ---- Push buttons (until discovery.c linked) ---- */
int32_t BSP_PB_Init(Button_TypeDef Button, ButtonMode_TypeDef ButtonMode)
{
	(void)Button;
	(void)ButtonMode;
	return BSP_ERROR_NONE;
}

uint32_t BSP_PB_GetState(Button_TypeDef Button)
{
	(void)Button;
	return 0U;
}
#else
/* REAL_CAM_LCD: Font20 comes from Utilities/Fonts via stm32_lcd.c */

/* GPIO poll, same pins/mode as stm32n6570_discovery.c (avoid linking full discovery.c). */
int32_t BSP_PB_Init(Button_TypeDef Button, ButtonMode_TypeDef ButtonMode)
{
	GPIO_InitTypeDef gpio = {0};
	GPIO_TypeDef *port;
	uint16_t pin;

	if (ButtonMode != BUTTON_MODE_GPIO) {
		return BSP_ERROR_WRONG_PARAM;
	}
	if (Button == BUTTON_USER1) {
		BUTTON_USER1_GPIO_CLK_ENABLE();
		port = BUTTON_USER1_GPIO_PORT;
		pin = BUTTON_USER1_PIN;
	} else if (Button == BUTTON_TAMP) {
		BUTTON_TAMP_GPIO_CLK_ENABLE();
		port = BUTTON_TAMP_GPIO_PORT;
		pin = BUTTON_TAMP_PIN;
	} else {
		return BSP_ERROR_WRONG_PARAM;
	}

	gpio.Pin = pin;
	gpio.Mode = GPIO_MODE_INPUT;
	gpio.Pull = GPIO_PULLDOWN;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(port, &gpio);
	return BSP_ERROR_NONE;
}

uint32_t BSP_PB_GetState(Button_TypeDef Button)
{
	if (Button == BUTTON_USER1) {
		return (uint32_t)HAL_GPIO_ReadPin(BUTTON_USER1_GPIO_PORT, BUTTON_USER1_PIN);
	}
	if (Button == BUTTON_TAMP) {
		return (uint32_t)HAL_GPIO_ReadPin(BUTTON_TAMP_GPIO_PORT, BUTTON_TAMP_PIN);
	}
	return 0U;
}

void UTIL_LCDEx_PrintfAt(uint32_t x_pos, uint32_t y_pos, Text_AlignModeTypdef mode, const char *format, ...)
{
	char buffer[48];
	va_list args;

	va_start(args, format);
	(void)vsnprintf(buffer, sizeof buffer, format, args);
	va_end(args);
	UTIL_LCD_DisplayStringAt(x_pos, y_pos, (uint8_t *)buffer, mode);
}
#endif /* !TENOMI_C5_REAL_CAM_LCD */

#if !(TENOMI_C5_REAL_CAM_LCD)
/* NN pipe start kept as stub while CAM is stubbed */
void CAM_NNPipe_Start(uint8_t *nn_pipe_dst, uint32_t cam_mode)
{
	(void)nn_pipe_dst;
	(void)cam_mode;
}
#endif

#ifndef TENOMI_M4_REAL_NN
#define TENOMI_M4_REAL_NN 0
#endif
#ifndef TENOMI_M4_PD_ONLY
#define TENOMI_M4_PD_ONLY 0
#endif

#if !(TENOMI_M4_REAL_NN) || (TENOMI_M4_PD_ONLY)
/* ---- MVE resize stub (real mve_resize.c linked on M5) ---- */
void mve_resize_bilinear_iu8ou8_with_strides(const uint8_t *in_data, uint8_t *out_data, const size_t stride_in,
                                            const size_t stride_out, const size_t width_in, const size_t height_in,
                                            const size_t width_out, const size_t height_out, const size_t n_channels,
                                            const void *pScratch)
{
	(void)in_data;
	(void)stride_in;
	(void)stride_out;
	(void)width_in;
	(void)height_in;
	(void)pScratch;
	if (out_data != NULL) {
		memset(out_data, 0, width_out * height_out * n_channels);
	}
}
#endif

#if !(TENOMI_M4_REAL_NN)
/* ---- Postprocess (stub until real PP linked) ---- */
int32_t app_postprocess_init(void *params_postprocess, stai_network_info *NN_Info)
{
	(void)params_postprocess;
	(void)NN_Info;
	return 0;
}

int32_t app_postprocess_run(void *pInput[], int nb_input, void *pOutput, void *pInput_param)
{
	(void)pInput;
	(void)nb_input;
	(void)pOutput;
	(void)pInput_param;
	return 0;
}

/* ---- stai palm (stub) ---- */
stai_return_code stai_runtime_init(void)
{
	return STAI_SUCCESS;
}

stai_return_code stai_palm_detector_init(stai_network *network)
{
	(void)network;
	return STAI_SUCCESS;
}

stai_return_code stai_palm_detector_run(stai_network *network, const stai_run_mode mode)
{
	(void)network;
	(void)mode;
	return STAI_SUCCESS;
}

stai_return_code stai_palm_detector_get_info(stai_network *network, stai_network_info *info)
{
	(void)network;
	if (info != NULL) {
		memset(info, 0, sizeof(*info));
	}
	return STAI_SUCCESS;
}

stai_return_code stai_palm_detector_get_outputs(stai_network *network, stai_ptr *outputs, stai_size *n_outputs)
{
	(void)network;
	if (n_outputs != NULL) {
		*n_outputs = STAI_PALM_DETECTOR_OUT_NUM;
	}
	if (outputs != NULL) {
		for (stai_size i = 0; i < STAI_PALM_DETECTOR_OUT_NUM; i++) {
			outputs[i] = NULL;
		}
	}
	return STAI_SUCCESS;
}

stai_return_code stai_palm_detector_set_inputs(stai_network *network, const stai_ptr *inputs, stai_size n_inputs)
{
	(void)network;
	(void)inputs;
	(void)n_inputs;
	return STAI_SUCCESS;
}

stai_return_code stai_ext_palm_detector_new_inference(stai_network *network)
{
	(void)network;
	return STAI_SUCCESS;
}
#endif /* !TENOMI_M4_REAL_NN */

#if !(TENOMI_M4_REAL_NN) || (TENOMI_M4_PD_ONLY)
/* ---- stai hand landmark stub (real model linked when M5 / PD_ONLY=0) ---- */
stai_return_code stai_hand_landmark_init(stai_network *network)
{
	(void)network;
	return STAI_SUCCESS;
}

stai_return_code stai_hand_landmark_run(stai_network *network, const stai_run_mode mode)
{
	(void)network;
	(void)mode;
	return STAI_SUCCESS;
}

stai_return_code stai_hand_landmark_get_inputs(stai_network *network, stai_ptr *inputs, stai_size *n_inputs)
{
	(void)network;
	if (n_inputs != NULL) {
		*n_inputs = 1;
	}
	if (inputs != NULL) {
		inputs[0] = NULL;
	}
	return STAI_SUCCESS;
}

stai_return_code stai_hand_landmark_get_outputs(stai_network *network, stai_ptr *outputs, stai_size *n_outputs)
{
	(void)network;
	if (n_outputs != NULL) {
		*n_outputs = STAI_HAND_LANDMARK_OUT_NUM;
	}
	if (outputs != NULL) {
		for (stai_size i = 0; i < STAI_HAND_LANDMARK_OUT_NUM; i++) {
			outputs[i] = NULL;
		}
	}
	return STAI_SUCCESS;
}

stai_return_code stai_ext_hand_landmark_new_inference(stai_network *network)
{
	(void)network;
	return STAI_SUCCESS;
}
#endif /* HL stub */
