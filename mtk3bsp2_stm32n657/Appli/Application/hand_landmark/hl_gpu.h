/**
 * @file hl_gpu.h
 * @brief GPU2D / ICACHE init for NemaGFX (Q3 rotation path)
 */
#ifndef TENOMI_HL_GPU_H
#define TENOMI_HL_GPU_H

#include "stm32n6xx_hal.h"

extern GPU2D_HandleTypeDef hgpu2d;

/** Call after hl_clock_init / hl_axisram_enable, before app_run / Nema. */
int hl_gpu_init(void);

#endif /* TENOMI_HL_GPU_H */
