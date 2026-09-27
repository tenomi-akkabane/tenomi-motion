/**
 * @file hl_xspi.c
 * @brief Phase C2/M4: XSPI PSRAM + NOR (memory-mapped)
 *
 * PSRAM: early init (pre-boost) then DeInit+Init in hl_xspi_reclock() after PLL1.
 * NOR: init only AFTER boost at ~200 MHz HCLK (same as landmarks). Pre-boost NOR
 * MMP leaves stale Refresh → garbage weights @ 0x70380000 → no palm boxes / NN hang.
 */
#include "hl_xspi.h"
#include "hl_log.h"

#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_xspi.h"

#define HL_PSRAM_TEST_ADDR ((volatile uint32_t *)0x91000000U)
#define HL_PSRAM_TEST_PAT  (0xA5C31F07UL)
#define HL_PALM_WEIGHT_ADDR ((volatile uint32_t *)0x70380000U)
#define HL_HL_WEIGHT_ADDR   ((volatile uint32_t *)0x70580000U)
#define HL_MOTION_WEIGHT_ADDR ((volatile uint32_t *)0x70A00000U)
#define HL_MOTION_WEIGHT_MAGIC (0x504C4D4DUL)

static int s_hl_nor_ready;

static int hl_xspi_clocks(void)
{
	RCC_PeriphCLKInitTypeDef periph = {0};

	periph.PeriphClockSelection = RCC_PERIPHCLK_XSPI1 | RCC_PERIPHCLK_XSPI2;
	periph.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;
	periph.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;

	if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
		return -100;
	}
	return 0;
}

static int hl_psram_rw_test(void)
{
	uint32_t rd;

	*HL_PSRAM_TEST_ADDR = HL_PSRAM_TEST_PAT;
	rd = *HL_PSRAM_TEST_ADDR;
	if (rd != HL_PSRAM_TEST_PAT) {
		HL_ERR((UB *)"hand_landmark: ERROR PSRAM R/W FAIL\n");
		return -1;
	}
	return 0;
}

static int hl_psram_bringup(const char *tag)
{
	int32_t ret;

	(void)tag;
	ret = BSP_XSPI_RAM_Init(0);
	if (ret != BSP_ERROR_NONE) {
		HL_ERR((UB *)"hand_landmark: ERROR BSP_XSPI_RAM_Init (%d)\n", (INT)ret);
		return (int)ret;
	}
	ret = BSP_XSPI_RAM_EnableMemoryMappedMode(0);
	if (ret != BSP_ERROR_NONE) {
		HL_ERR((UB *)"hand_landmark: ERROR PSRAM MMP (%d)\n", (INT)ret);
		return (int)ret;
	}
	HL_LOG((UB *)"hand_landmark: XSPI PSRAM OK %s\n", tag);

	if (hl_psram_rw_test() != 0) {
		return -2;
	}
	return 0;
}

static int hl_nor_try_once(void)
{
	BSP_XSPI_NOR_Init_t nor_init;
	int32_t ret;

	nor_init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
	nor_init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;

	(void)BSP_XSPI_NOR_DeInit(0);
	/* CubeProgrammer External Loader leaves XSPI2/NOR in OPI DTR. */
	__HAL_RCC_XSPI2_FORCE_RESET();
	__HAL_RCC_XSPI2_RELEASE_RESET();
	HAL_Delay(5);

	ret = BSP_XSPI_NOR_Init(0, &nor_init);
	if (ret != BSP_ERROR_NONE) {
		(void)BSP_XSPI_NOR_DeInit(0);
		return (int)ret;
	}
	ret = BSP_XSPI_NOR_EnableMemoryMappedMode(0);
	if (ret != BSP_ERROR_NONE) {
		(void)BSP_XSPI_NOR_DeInit(0);
		return (int)ret;
	}
	return 0;
}

static int hl_nor_bringup(void)
{
	int ret = -1;
	uint32_t w0;
	uint32_t w1;
	int attempt;

	s_hl_nor_ready = 0;

	for (attempt = 1; attempt <= 5; attempt++) {
		ret = hl_nor_try_once();
		if (ret == 0) {
			break;
		}
		HL_ERR((UB *)"hand_landmark: NOR init retry %d (%d)\n", (INT)attempt, (INT)ret);
		HAL_Delay(50);
	}
	if (ret != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR NOR init (%d) — NN weights unavailable\n", (INT)ret);
		HL_ERR((UB *)"hand_landmark: power-cycle after CubeProgrammer, then Debug again\n");
		return ret;
	}

	HL_ERR((UB *)"hand_landmark: XSPI NOR OK\n");

	w0 = HL_PALM_WEIGHT_ADDR[0];
	w1 = HL_PALM_WEIGHT_ADDR[1];
	if (w0 == 0xFFFFFFFFUL && w1 == 0xFFFFFFFFUL) {
		HL_ERR((UB *)"hand_landmark: ERROR palm weights erased @0x70380000\n");
	}
	w0 = HL_HL_WEIGHT_ADDR[0];
	w1 = HL_HL_WEIGHT_ADDR[1];
	if (w0 == 0xFFFFFFFFUL && w1 == 0xFFFFFFFFUL) {
		HL_ERR((UB *)"hand_landmark: ERROR HL weights erased @0x70580000\n");
	}
	w0 = HL_MOTION_WEIGHT_ADDR[0];
	if (w0 != HL_MOTION_WEIGHT_MAGIC) {
		HL_ERR((UB *)"hand_landmark: ERROR MotionMLP weights missing @0x70A00000\n");
	}

	s_hl_nor_ready = 1;
	return 0;
}

int hl_xspi_nor_ready(void)
{
	return s_hl_nor_ready;
}

int hl_xspi_reclock(void)
{
	int32_t ret;
	int skip_nor = 0;

	if (hl_xspi_clocks() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR XSPI clock config (reclock)\n");
		return -1;
	}

	ret = BSP_XSPI_RAM_DeInit(0);
	(void)ret;

	if (hl_psram_bringup("(post-boost)") != 0) {
		return -2;
	}

#if defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY)
	skip_nor = 1;
#endif
	if (skip_nor) {
		return 0;
	}

	(void)hl_nor_bringup();
	return 0;
}

int hl_xspi_init(void)
{
	if (hl_xspi_clocks() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR XSPI clock config\n");
		return -1;
	}

	if (hl_psram_bringup("(pre-boost)") != 0) {
		return -1;
	}
	return 0;
}
