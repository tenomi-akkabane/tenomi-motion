/**
 * @file hl_gpu.c
 * @brief GPU2D + ICACHE bring-up (from landmarks/Src/main.c, Q3)
 */
#include "hl_gpu.h"

#include "hl_log.h"

GPU2D_HandleTypeDef hgpu2d;

static void hl_gpu_cache_enable(void)
{
	HAL_ICACHE_DeInit();
	HAL_ICACHE_Disable();
	HAL_ICACHE_ConfigAssociativityMode(ICACHE_4WAYS);
	HAL_ICACHE_Enable();
}

static int hl_gpu2d_init(void)
{
	hgpu2d.Instance = (uint32_t)GPU2D;

	if (HAL_GPU2D_Init(&hgpu2d) != HAL_OK) {
		HL_ERR((UB *)"hand_landmark: ERROR GPU2D_Init\n");
		return -1;
	}
	return 0;
}

int hl_gpu_init(void)
{
	HL_LOG((UB *)"hand_landmark: GPU2D init (Q3 Nema)\n");

	if (hl_gpu2d_init() != 0) {
		return -1;
	}

	hl_gpu_cache_enable();
	HL_LOG((UB *)"hand_landmark: GPU2D OK\n");
	return 0;
}

void HAL_GPU2D_MspInit(GPU2D_HandleTypeDef *hgpu2d_init)
{
	if (hgpu2d_init->Instance == (uint32_t)GPU2D) {
		__HAL_RCC_GPU2D_FORCE_RESET();
		__HAL_RCC_GPU2D_RELEASE_RESET();
		__HAL_RCC_GPU2D_CLK_ENABLE();

		HAL_NVIC_SetPriority(GPU2D_IRQn, 6, 0);
		HAL_NVIC_EnableIRQ(GPU2D_IRQn);

		HAL_NVIC_SetPriority(GPU2D_ER_IRQn, 5, 0);
		HAL_NVIC_EnableIRQ(GPU2D_ER_IRQn);
	}
}

void HAL_GPU2D_MspDeInit(GPU2D_HandleTypeDef *hgpu2d_deinit)
{
	if (hgpu2d_deinit->Instance == (uint32_t)GPU2D) {
		__HAL_RCC_GPU2D_CLK_DISABLE();
	}
}

void HAL_GFXMMU_MspInit(GFXMMU_HandleTypeDef *hgfxmmu)
{
	(void)hgfxmmu;

	__HAL_RCC_GFXMMU_CLK_ENABLE();
	HAL_NVIC_SetPriority(GFXMMU_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(GFXMMU_IRQn);
}

void HAL_GFXMMU_MspDeInit(GFXMMU_HandleTypeDef *hgfxmmu)
{
	(void)hgfxmmu;

	__HAL_RCC_GFXMMU_CLK_DISABLE();
	HAL_NVIC_DisableIRQ(GFXMMU_IRQn);
}
