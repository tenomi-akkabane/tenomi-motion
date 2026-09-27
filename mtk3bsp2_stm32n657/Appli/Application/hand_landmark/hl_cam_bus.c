/**
 * @file hl_cam_bus.c
 * @brief Fix I2C1 ownership + TIMING for camera under μT-Kernel.
 *
 * Board scan found ACK @ 0x34 = IMX335 (not VD66GY @ 0x20).
 * Force CubeMX/FSBL TIMINGR 0x30C0EDFF (GetPCLK1Freq is wrong on this bring-up).
 */
#include "hl_cam_bus.h"

#include "stm32n6570_discovery_bus.h"
#include "stm32n6xx_hal.h"

#define HL_I2C1_TIMING_CUBEMX  0x30C0EDFFU

extern I2C_HandleTypeDef hi2c1;

HAL_StatusTypeDef MX_I2C1_Init(I2C_HandleTypeDef *hI2c, uint32_t timing)
{
	HAL_StatusTypeDef status = HAL_OK;

	(void)timing;

	hI2c->Init.Timing           = HL_I2C1_TIMING_CUBEMX;
	hI2c->Init.OwnAddress1      = 0;
	hI2c->Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
	hI2c->Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
	hI2c->Init.OwnAddress2      = 0;
	hI2c->Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	hI2c->Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
	hI2c->Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

	if (HAL_I2C_Init(hI2c) != HAL_OK) {
		return HAL_ERROR;
	}

	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);

	if (HAL_I2CEx_ConfigAnalogFilter(hI2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
		status = HAL_ERROR;
	} else if (HAL_I2CEx_ConfigDigitalFilter(hI2c, 0) != HAL_OK) {
		status = HAL_ERROR;
	}

	return status;
}

/**
 * IMX335 / MB1854 power-on (cmw_imx335.c):
 *   PD2 = EN_MODULE (1V2 + 24 MHz CAM_CLK)
 *   PC8 = XCLR reset (active low)
 */
void hl_cam_power_on(void)
{
	GPIO_InitTypeDef gpio = {0};

	HAL_PWREx_EnableVddIO4();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();

	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

	gpio.Pin = GPIO_PIN_2;
	HAL_GPIO_Init(GPIOD, &gpio);
	gpio.Pin = GPIO_PIN_8;
	HAL_GPIO_Init(GPIOC, &gpio);

	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET);   /* EN + 24MHz */
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET); /* XCLR low */
	HAL_Delay(1);
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);   /* XCLR release */
	HAL_Delay(10);
}

void hl_cam_bus_prep(void)
{
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);

	if (hi2c1.Instance != NULL) {
		(void)HAL_I2C_DeInit(&hi2c1);
	}

	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);

	/* Power sensor before CMW_CAMERA_Init (was smoke-test side effect). */
	hl_cam_power_on();
}
