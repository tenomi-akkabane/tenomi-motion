/**
 * @file hl_startup.c
 * @brief Phase C startup: clock/AXISRAM/XSPI → C4 OSAL → C5 app_run
 *
 * app_run runs on a dedicated task: init-task stack was 1KB and overflowed
 * inside BSP_LCD_Init (Bus Fault BFAR=0x8 = NULL→LTDC SSCR).
 */
#include "hl_startup.h"

#include "hl_cam_bus.h"
#include "hl_clock.h"
#include "hl_log.h"
#include "hl_npu.h"
#include "hl_security.h"
#include "hl_xspi.h"
#if defined(TENOMI_NEMA_ENABLE) && (TENOMI_NEMA_ENABLE)
#include "hl_gpu.h"
#endif
#include "rtos/hl_rtos.h"

#include <tk/tkernel.h>

#if defined(TENOMI_BUILD_ATON_OSAL) && (TENOMI_BUILD_ATON_OSAL)
void aton_osal_utkernel_init(void);
void aton_osal_utkernel_deinit(void);
#endif

void app_run(void);

#ifndef TENOMI_C3_CALL_APP_RUN
#define TENOMI_C3_CALL_APP_RUN 0
#endif

/* 4096 words × 4 = 16KB — LCD/CAM HAL nest needs this */
#define HL_APP_STACK_WORDS 4096

static StaticTask_t s_hl_app_tcb;
static StackType_t s_hl_app_stack[HL_APP_STACK_WORDS];

static StaticTask_t s_hl_stub_tcb;
static StackType_t s_hl_stub_stack[256];
static StaticSemaphore_t s_hl_stub_sem;

static void hl_app_task(void *arg)
{
	(void)arg;

	/* CubeMX hi2c1 IRQ must not fight BSP camera I2C (hbus_i2c1 polling) */
	hl_cam_bus_prep();

	app_run();

	for (;;) {
		tk_dly_tsk(1000);
	}
}

static void hl_stub_task(void *arg)
{
	(void)arg;

	for (;;) {
		if (xSemaphoreTake(&s_hl_stub_sem, portMAX_DELAY) == pdTRUE) {
			/* woken once from hand_landmark_startup */
		}
		tk_dly_tsk(1000);
	}
}

void hand_landmark_startup(void)
{
	SemaphoreHandle_t sem;
	TaskHandle_t tsk;

	/*
	 * XSPI (PSRAM+NOR) before PLL1 boost: NOR OPI hung when inited at 600 MHz HCLK.
	 * M3 skipped NOR under VIDEO_ONLY; M4 needs NOR for weights @ 0x70xxxxxx.
	 */
	if (hl_xspi_init() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR XSPI init failed\n");
	}

	/* PLL1 restore + PLL2/3/4; retunes μT-Kernel SysTick via os_bsp */
	if (hl_clock_init() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR clock init failed\n");
	}

	hl_axisram_enable();

	/* PSRAM was inited at slow HCLK — reinit after PLL1 so Refresh matches ~200 MHz */
	if (hl_xspi_reclock() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR XSPI reclock failed\n");
	}

	hl_security_config();

#if defined(TENOMI_NEMA_ENABLE) && (TENOMI_NEMA_ENABLE)
	if (hl_gpu_init() != 0) {
		HL_ERR((UB *)"hand_landmark: ERROR GPU init failed\n");
	}
#endif

#if !(defined(TENOMI_C5_VIDEO_ONLY) && (TENOMI_C5_VIDEO_ONLY))
	hl_npu_enable();
#endif

#if defined(TENOMI_BUILD_ATON_OSAL) && (TENOMI_BUILD_ATON_OSAL)
	aton_osal_utkernel_init();
#endif

#if TENOMI_C3_CALL_APP_RUN
	/* Do NOT call app_run() on init-task stack — use a large dedicated stack */
	tsk = xTaskCreateStatic(hl_app_task, "hlapp", HL_APP_STACK_WORDS, NULL, FREERTOS_PRIORITY(1),
	                        s_hl_app_stack, &s_hl_app_tcb);
	if (tsk == NULL) {
		HL_ERR((UB *)"hand_landmark: ERROR hlapp task create\n");
		return;
	}
#else
	(void)app_run;
#endif

	sem = xSemaphoreCreateCountingStatic(1, 0, &s_hl_stub_sem);
	if (sem == NULL) {
		HL_ERR((UB *)"hand_landmark: ERROR sem create\n");
		return;
	}

	tsk = xTaskCreateStatic(hl_stub_task, "hlstub", 256, NULL, FREERTOS_PRIORITY(0), s_hl_stub_stack,
	                        &s_hl_stub_tcb);
	if (tsk == NULL) {
		HL_ERR((UB *)"hand_landmark: ERROR task create\n");
		return;
	}

	(void)xSemaphoreGive(sem);
}
