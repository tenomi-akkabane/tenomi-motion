/**
 * @file hl_clock.c
 * @brief Enable PLL2 (DCMIPP) / PLL4 (LTDC); keep PLL1/CSI at landmarks rates.
 *
 * BSP2 FSBL starts PLL1 @ 1200 MHz (HSI/4*75). CSI needs ~20 MHz → IC18 /60.
 * Q2b (TENOMI_CPUCLK_800MHZ=1): retune PLL1 to 800 MHz (FreeRTOS match), IC1/1.
 * If PLL1 is left in bypass (64 MHz), CSI /3≈21 MHz still receives frames but
 * HCLK/XSPI is too slow → PIPE1_OVR and colorful garbage on LCD.
 */
#include "hl_clock.h"
#include "hl_log.h"

#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_dcmipp.h"
#include "stm32n6xx_ll_rcc.h"
#include "os_bsp.h"

/* DK SMPS control (stm32n6570_discovery.h) — avoid linking full discovery.c */
#define HL_SMPS_GPIO_PORT          GPIOF
#define HL_SMPS_GPIO_PIN           GPIO_PIN_4
#define HL_SMPS_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOF_CLK_ENABLE()

static void hl_smps_overdrive(void)
{
	GPIO_InitTypeDef gpio = {0};

	HL_SMPS_GPIO_CLK_ENABLE();
	gpio.Pin = HL_SMPS_GPIO_PIN;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(HL_SMPS_GPIO_PORT, &gpio);
	HAL_GPIO_WritePin(HL_SMPS_GPIO_PORT, HL_SMPS_GPIO_PIN, GPIO_PIN_SET);
	HAL_Delay(1);
}

/** PLL1 output (VCO/P1/P2) from registers — does not require PLL1P enable bit. */
static uint32_t hl_pll1_out_hz_from_regs(void)
{
	uint32_t cfgr1 = READ_REG(RCC->PLL1CFGR1);
	uint32_t cfgr3;
	uint32_t src;
	uint32_t m;
	uint32_t n;
	uint32_t p1;
	uint32_t p2;
	uint32_t src_hz;

	if ((cfgr1 & RCC_PLL1CFGR1_PLL1BYP) != 0U) {
		src = cfgr1 & RCC_PLL1CFGR1_PLL1SEL;
		if (src == 0U) {
			return HSI_VALUE >> ((RCC->HSICFGR & RCC_HSICFGR_HSIDIV) >> RCC_HSICFGR_HSIDIV_Pos);
		}
		return HSI_VALUE;
	}

	m = (cfgr1 & RCC_PLL1CFGR1_PLL1DIVM) >> RCC_PLL1CFGR1_PLL1DIVM_Pos;
	n = (cfgr1 & RCC_PLL1CFGR1_PLL1DIVN) >> RCC_PLL1CFGR1_PLL1DIVN_Pos;
	if (m == 0U || n == 0U) {
		return 0U;
	}

	cfgr3 = READ_REG(RCC->PLL1CFGR3);
	p1 = (cfgr3 & RCC_PLL1CFGR3_PLL1PDIV1) >> RCC_PLL1CFGR3_PLL1PDIV1_Pos;
	p2 = (cfgr3 & RCC_PLL1CFGR3_PLL1PDIV2) >> RCC_PLL1CFGR3_PLL1PDIV2_Pos;
	if (p1 == 0U) {
		p1 = 1U;
	}
	if (p2 == 0U) {
		p2 = 1U;
	}

	src = cfgr1 & RCC_PLL1CFGR1_PLL1SEL;
	if (src == 0U) {
		src_hz = HSI_VALUE >> ((RCC->HSICFGR & RCC_HSICFGR_HSIDIV) >> RCC_HSICFGR_HSIDIV_Pos);
	} else if (src == RCC_PLL1CFGR1_PLL1SEL_1) {
		src_hz = HSE_VALUE;
	} else {
		src_hz = HSI_VALUE;
	}

	return (src_hz / m) * n / p1 / p2;
}

#ifndef TENOMI_CPUCLK_800MHZ
#define TENOMI_CPUCLK_800MHZ 0
#endif

#if TENOMI_CPUCLK_800MHZ
#define HL_PLL1_TARGET_HZ   800000000U
#define HL_PLL1_PLLM          2U
#define HL_PLL1_PLLN         25U
#define HL_IC1_DIV            1U
#define HL_IC2_DIV            2U
#define HL_PLL1_FALLBACK_HZ   800000000U
#else
#define HL_PLL1_TARGET_HZ  1200000000U
#define HL_PLL1_PLLM          4U
#define HL_PLL1_PLLN         75U
#define HL_IC1_DIV            2U
#define HL_IC2_DIV            3U
#define HL_PLL1_FALLBACK_HZ  1200000000U
#endif

static int hl_pll1_matches_target(uint32_t pll1_hz)
{
	if (LL_RCC_PLL1_IsEnabledBypass() != 0U) {
		return 0;
	}
#if TENOMI_CPUCLK_800MHZ
	return (pll1_hz >= 750000000U && pll1_hz <= 850000000U);
#else
	return (pll1_hz >= 1000000000U);
#endif
}

static void hl_log_pll1(const char *tag)
{
	HL_LOG((UB *)"hand_landmark: %s PLL1 ready=%d bypass=%d Pen=%d\n",
	       tag,
	       (int)LL_RCC_PLL1_IsReady(),
	       (int)LL_RCC_PLL1_IsEnabledBypass(),
	       (int)LL_RCC_PLL1P_IsEnabled());
	HL_LOG((UB *)"  GetPLL1=%d reg=%d SysClk=%d CFGR1=0x%x\n",
	       (int)HAL_RCCEx_GetPLL1CLKFreq(),
	       (int)hl_pll1_out_hz_from_regs(),
	       (int)SystemCoreClock,
	       (unsigned)READ_REG(RCC->PLL1CFGR1));
}

/**
 * Ensure PLL1 matches the selected CPUCLK profile.
 * Q2b (TENOMI_CPUCLK_800MHZ=1): FreeRTOS-style PLL1=800 MHz, IC1/1 → CPU 800 MHz.
 * Default: FSBL-style PLL1=1200 MHz, IC1/2 → CPU 600 MHz.
 * Slow PLL1 → slow HCLK/XSPI → DCMIPP PIPE1_OVR → colorful LCD noise.
 */
static int hl_ensure_pll1(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clk = {0};
	uint32_t pll1_hz;

	hl_log_pll1("PLL1 before");

	pll1_hz = hl_pll1_out_hz_from_regs();
	if (hl_pll1_matches_target(pll1_hz)) {
		HL_LOG((UB *)"hand_landmark: PLL1 OK (%d Hz, CPUCLK %s)\n",
		       (int)pll1_hz,
#if TENOMI_CPUCLK_800MHZ
		       (UB *)"800 MHz");
#else
		       (UB *)"600 MHz");
#endif
		SystemCoreClockUpdate();
		return 0;
	}

#if TENOMI_CPUCLK_800MHZ
	HL_LOG((UB *)"hand_landmark: PLL1 retune — FreeRTOS 800 MHz (Q2b)\n");
#else
	HL_LOG((UB *)"hand_landmark: PLL1 weak/bypass — restore FSBL 1200 MHz\n");
#endif

	/* Switch CPU/SYS to HSI before retuning PLL1 (same sequence as FSBL). */
	HAL_RCC_GetClockConfig(&clk);
	if ((clk.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
	    (clk.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11)) {
		clk.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
		clk.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
		clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
		if (HAL_RCC_ClockConfig(&clk) != HAL_OK) {
			HL_ERR((UB *)"hand_landmark: ERROR switch to HSI\n");
			return -1;
		}
	}

	osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;
	osc.PLL1.PLLState = RCC_PLL_ON;
	osc.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
	osc.PLL1.PLLM = HL_PLL1_PLLM;
	osc.PLL1.PLLN = HL_PLL1_PLLN;
	osc.PLL1.PLLFractional = 0;
	osc.PLL1.PLLP1 = 1;
	osc.PLL1.PLLP2 = 1;
	osc.PLL2.PLLState = RCC_PLL_NONE;
	osc.PLL3.PLLState = RCC_PLL_NONE;
	osc.PLL4.PLLState = RCC_PLL_NONE;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
		HL_ERR((UB *)"hand_landmark: ERROR PLL1 OscConfig\n");
		return -1;
	}

	clk.ClockType = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
	                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK5 |
	                RCC_CLOCKTYPE_PCLK4;
	clk.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
	clk.AHBCLKDivider = RCC_HCLK_DIV2;
	clk.APB1CLKDivider = RCC_APB1_DIV1;
	clk.APB2CLKDivider = RCC_APB2_DIV1;
	clk.APB4CLKDivider = RCC_APB4_DIV1;
	clk.APB5CLKDivider = RCC_APB5_DIV1;
	clk.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
	clk.IC1Selection.ClockDivider = HL_IC1_DIV;
	clk.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
	clk.IC2Selection.ClockDivider = HL_IC2_DIV;
	clk.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
	clk.IC6Selection.ClockDivider = 4;
	clk.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
	clk.IC11Selection.ClockDivider = 3;
	if (HAL_RCC_ClockConfig(&clk) != HAL_OK) {
		HL_ERR((UB *)"hand_landmark: ERROR PLL1 ClockConfig\n");
		return -1;
	}

	/* HAL_InitTick (via ClockConfig) retunes μT-Kernel SysTick when kernel runs.
	 * Call again explicitly in case OscConfig path skipped it. */
	os_bsp_retune_kernel_timer();

	SystemCoreClockUpdate();
	hl_log_pll1("PLL1 after");
	return 0;
}

int hl_clock_init(void)
{
	RCC_OscInitTypeDef osc = {0};

	HL_LOG((UB *)"hand_landmark: Phase C5 clock (PLL1/2/3/4, CPUCLK %s)\n",
#if TENOMI_CPUCLK_800MHZ
	       (UB *)"800 MHz Q2b");
#else
	       (UB *)"600 MHz");
#endif

	hl_smps_overdrive();

	if (hl_ensure_pll1() != 0) {
		return -1;
	}

	/* Leave PLL1 as ensured above; start PLL2/3/4 for DCMIPP/LTDC/NPU. */
	osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;
	osc.PLL1.PLLState = RCC_PLL_NONE;

	/* PLL2 = 64 x 125 / 8 = 1000 MHz (DCMIPP via IC17) */
	osc.PLL2.PLLState = RCC_PLL_ON;
	osc.PLL2.PLLSource = RCC_PLLSOURCE_HSI;
	osc.PLL2.PLLM = 8;
	osc.PLL2.PLLN = 125;
	osc.PLL2.PLLFractional = 0;
	osc.PLL2.PLLP1 = 1;
	osc.PLL2.PLLP2 = 1;

	/* PLL3 = (64 x 225 / 8) / 2 = 900 MHz (NPU RAM path; harmless for VIDEO_ONLY) */
	osc.PLL3.PLLState = RCC_PLL_ON;
	osc.PLL3.PLLSource = RCC_PLLSOURCE_HSI;
	osc.PLL3.PLLM = 8;
	osc.PLL3.PLLN = 225;
	osc.PLL3.PLLFractional = 0;
	osc.PLL3.PLLP1 = 1;
	osc.PLL3.PLLP2 = 2;

	/* PLL4 = (64 x 225 / 8) / 36 = 50 MHz (LTDC via IC16 / 2 = 25 MHz) */
	osc.PLL4.PLLState = RCC_PLL_ON;
	osc.PLL4.PLLSource = RCC_PLLSOURCE_HSI;
	osc.PLL4.PLLM = 8;
	osc.PLL4.PLLN = 225;
	osc.PLL4.PLLFractional = 0;
	osc.PLL4.PLLP1 = 6;
	osc.PLL4.PLLP2 = 6;

	if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
		HL_ERR((UB *)"hand_landmark: ERROR PLL2/3/4 OscConfig\n");
		return -1;
	}

	/* NPU (sysc/IC6) and AXISRAM3–6 (sysd/IC11): landmarks SystemClock_Config.
	 * FSBL / PLL1 restore leave IC6=PLL1/4 and IC11=PLL1/3 — too wrong for ATON. */
	LL_RCC_IC6_SetSource(LL_RCC_ICCLKSOURCE_PLL2);
	LL_RCC_IC6_SetDivider(1U);
	LL_RCC_IC6_Enable();
	LL_RCC_IC11_SetSource(LL_RCC_ICCLKSOURCE_PLL3);
	LL_RCC_IC11_SetDivider(1U);
	LL_RCC_IC11_Enable();
	HL_LOG((UB *)"hand_landmark: IC6=PLL2/1 (NPU) IC11=PLL3/1 (AXISRAM)\n");

	SystemCoreClockUpdate();
	HL_ERR((UB *)"hand_landmark: CPUCLK=%u\n", (unsigned)SystemCoreClock);
	HL_LOG((UB *)"hand_landmark: Phase C5 clock OK (PLL1/2/3/4)\n");
	return 0;
}

void hl_axisram_enable(void)
{
	RAMCFG_HandleTypeDef hramcfg = {0};

	HL_LOG((UB *)"hand_landmark: Phase C5 AXISRAM3-6 enable\n");

	__HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
	__HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
	__HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
	__HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
	__HAL_RCC_RAMCFG_CLK_ENABLE();

	hramcfg.Instance = RAMCFG_SRAM3_AXI;
	HAL_RAMCFG_EnableAXISRAM(&hramcfg);
	hramcfg.Instance = RAMCFG_SRAM4_AXI;
	HAL_RAMCFG_EnableAXISRAM(&hramcfg);
	hramcfg.Instance = RAMCFG_SRAM5_AXI;
	HAL_RAMCFG_EnableAXISRAM(&hramcfg);
	hramcfg.Instance = RAMCFG_SRAM6_AXI;
	HAL_RAMCFG_EnableAXISRAM(&hramcfg);

	HL_LOG((UB *)"hand_landmark: Phase C5 AXISRAM OK\n");
}

/**
 * Override weak CMW/BSP stub: DCMIPP from PLL2, CSI lane clock ~20 MHz.
 *
 * Use register-derived PLL1 (not only HAL_RCCEx_GetPLL1CLKFreq, which can
 * report HSI when PLL1P enable/ready bits disagree with IC routing).
 */
HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
	RCC_PeriphCLKInitTypeDef periph = {0};
	HAL_StatusTypeDef ret;
	uint32_t pll1_hz;
	uint32_t csi_div;
	const uint32_t csi_target_hz = 20000000U;

	(void)hdcmipp;

	periph.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
	periph.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
	periph.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
	periph.ICSelection[RCC_IC17].ClockDivider = 3;
	ret = HAL_RCCEx_PeriphCLKConfig(&periph);
	if (ret != HAL_OK) {
		return ret;
	}

	pll1_hz = hl_pll1_out_hz_from_regs();
	if (pll1_hz < 100000000U) {
		pll1_hz = HAL_RCCEx_GetPLL1CLKFreq();
	}
	if (pll1_hz < 1000000U) {
		pll1_hz = HL_PLL1_FALLBACK_HZ;
	}

	csi_div = (pll1_hz + (csi_target_hz / 2U)) / csi_target_hz;
	if (csi_div < 1U) {
		csi_div = 1U;
	}
	if (csi_div > 256U) {
		csi_div = 256U;
	}

	HL_LOG((UB *)"hand_landmark: CSI clk PLL1=%d /%d -> ~%d Hz (GetPLL1=%d)\n",
	       (int)pll1_hz, (int)csi_div, (int)(pll1_hz / csi_div),
	       (int)HAL_RCCEx_GetPLL1CLKFreq());

	periph.PeriphClockSelection = RCC_PERIPHCLK_CSI;
	periph.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
	periph.ICSelection[RCC_IC18].ClockDivider = csi_div;
	return HAL_RCCEx_PeriphCLKConfig(&periph);
}
