/**
 * @file stm32n6570_discovery_conf.h
 * @brief Minimal BSP conf for TENOMI Phase C2 (XSPI only)
 */
#ifndef STM32N6570_DISCOVERY_CONF_H
#define STM32N6570_DISCOVERY_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"

#define STM32N6570_DK_A01 0
#define STM32N6570_DK_B01 1
#define STM32N6570_DK_C01 2

#define USE_COM_LOG                         0U
#define USE_BSP_COM_FEATURE                 0U

#define USE_FT5336_TS_CTRL                  0U
#define USE_TS_GESTURE                      0U
#define USE_TS_MULTI_TOUCH                  0U
#define TS_TOUCH_NBR                        1U

/* Default LTDC FB during BSP_LCD_Init — must be powered memory.
 * AXISRAM3 (0x34200000) is off after reset → Bus Fault if used before RAMCFG enable.
 * Use PSRAM (MMP @ 0x91000000); SCRL then rebinds to app lcd_* buffers. */
#define LCD_LAYER_0_ADDRESS                 0x91000000U
#define LCD_LAYER_1_ADDRESS                 0x91100000U

#define DEFAULT_AUDIO_IN_BUFFER_SIZE        2048U

#define BSP_SDRAM_IT_PRIORITY               15U
#define BSP_BUTTON_USER1_IT_PRIORITY        15U
#define BSP_BUTTON_USER2_IT_PRIORITY        15U
#define BSP_BUTTON_TAMP_IT_PRIORITY         15U
#define BSP_AUDIO_OUT_IT_PRIORITY           14U
#define BSP_AUDIO_IN_IT_PRIORITY            15U
#define BSP_SD_IT_PRIORITY                  14U
#define BSP_SD_RX_IT_PRIORITY               14U
#define BSP_SD_TX_IT_PRIORITY               15U
#define BSP_TS_IT_PRIORITY                  15U

#ifdef __cplusplus
}
#endif

#endif /* STM32N6570_DISCOVERY_CONF_H */
