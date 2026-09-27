/**
 * @file hl_npu.c
 * @brief Phase M4 NPU bring-up (from landmarks main.c NPURam_enable / NPUCache_config)
 *
 * AXISRAM3–6 are enabled earlier by hl_axisram_enable(). Here we clock the NPU
 * and enable CACHEAXI via npu_cache_enable() so ATON's npu_cache_* helpers share
 * the same CACHEAXI handle.
 */
#include "hl_npu.h"

#include "npu_cache.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_ll_bus.h"

/* Strong overrides for npu_cache.c weak empty stubs (landmarks main.c). */
void npu_cache_enable_clocks_and_reset(void)
{
	__HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
	__HAL_RCC_CACHEAXI_CLK_ENABLE();
	__HAL_RCC_CACHEAXI_FORCE_RESET();
	__HAL_RCC_CACHEAXI_RELEASE_RESET();
}

void npu_cache_disable_clocks_and_reset(void)
{
	__HAL_RCC_CACHEAXIRAM_MEM_CLK_DISABLE();
	__HAL_RCC_CACHEAXI_CLK_DISABLE();
	__HAL_RCC_CACHEAXI_FORCE_RESET();
}

void hl_npu_enable(void)
{
	__HAL_RCC_NPU_CLK_ENABLE();
	__HAL_RCC_NPU_FORCE_RESET();
	__HAL_RCC_NPU_RELEASE_RESET();

	/* Keep IPs clocked during idle/WFE so NPU IRQ can wake the CPU (landmarks). */
	LL_BUS_EnableClockLowPower(~0U);
	LL_MEM_EnableClockLowPower(~0U);
	LL_AHB5_GRP1_EnableClockLowPower(~0U);

	/* NPU0 = ATON_STD (platform.h). Mid priority so ISR may tk_sig_sem. */
	HAL_NVIC_SetPriority(NPU0_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(NPU0_IRQn);

	/* Use ATON's npu_cache_enable() — not a separate HAL handle. */
	npu_cache_enable();
}
