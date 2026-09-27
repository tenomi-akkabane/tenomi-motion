/**
 * @file hl_rtos.c
 * @brief FreeRTOS static-API → μT-Kernel implementation (TENOMI Phase C)
 */
#include "hl_rtos.h"

#include <string.h>

#if defined(STM32N657xx)
#include "stm32n6xx.h"
#endif

#ifndef CNF_MAX_TSKPRI
#define CNF_MAX_TSKPRI 32
#endif

static PRI hl_map_priority(UBaseType_t freertos_prio)
{
	int p = (int)freertos_prio;
	int utk;

	if (p < 0) {
		p = 0;
	}
	/* FreeRTOS high number → μT-Kernel low number (higher prio) */
	utk = (int)CNF_MAX_TSKPRI - (p % (int)CNF_MAX_TSKPRI);
	if (utk < 1) {
		utk = 1;
	}
	if (utk > (int)CNF_MAX_TSKPRI) {
		utk = (int)CNF_MAX_TSKPRI;
	}
	return (PRI)utk;
}

static void hl_task_trampoline(INT stacd, void *exinf)
{
	StaticTask_t *tcb = (StaticTask_t *)exinf;

	(void)stacd;
	if (tcb != NULL && tcb->fn != NULL) {
		tcb->fn(tcb->arg);
	}
	tk_ext_tsk();
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char *const pcName, uint32_t ulStackDepth,
                               void *const pvParameters, UBaseType_t uxPriority, StackType_t *const puxStackBuffer,
                               StaticTask_t *const pxTaskBuffer)
{
	T_CTSK ctsk;
	ID id;

	(void)pcName;

	if (pxTaskCode == NULL || puxStackBuffer == NULL || pxTaskBuffer == NULL || ulStackDepth == 0U) {
		return NULL;
	}

	memset(pxTaskBuffer, 0, sizeof(*pxTaskBuffer));
	pxTaskBuffer->fn = pxTaskCode;
	pxTaskBuffer->arg = pvParameters;
	pxTaskBuffer->tskid = 0;

	ctsk.exinf = pxTaskBuffer;
	ctsk.tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF;
	ctsk.task = (FP)hl_task_trampoline;
	ctsk.itskpri = hl_map_priority(uxPriority);
	ctsk.stksz = (SZ)(ulStackDepth * sizeof(StackType_t));
	ctsk.bufptr = puxStackBuffer;

	id = tk_cre_tsk(&ctsk);
	if (id < E_OK) {
		return NULL;
	}
	pxTaskBuffer->tskid = id;

	if (tk_sta_tsk(id, 0) < E_OK) {
		(void)tk_del_tsk(id);
		return NULL;
	}

	return pxTaskBuffer;
}

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount,
                                                 StaticSemaphore_t *pxSemaphoreBuffer)
{
	T_CSEM csem;
	ID id;

	if (pxSemaphoreBuffer == NULL || uxMaxCount == 0U) {
		return NULL;
	}

	csem.exinf = 0;
	csem.sematr = TA_TFIFO;
	csem.isemcnt = (INT)uxInitialCount;
	csem.maxsem = (INT)uxMaxCount;

	id = tk_cre_sem(&csem);
	if (id < E_OK) {
		return NULL;
	}

	pxSemaphoreBuffer->id = id;
	pxSemaphoreBuffer->is_mutex = 0;
	return pxSemaphoreBuffer;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer)
{
	T_CMTX cmtx;
	ID id;

	if (pxMutexBuffer == NULL) {
		return NULL;
	}

	cmtx.exinf = 0;
	cmtx.mtxatr = TA_INHERIT;
	cmtx.ceilpri = 0;

	id = tk_cre_mtx(&cmtx);
	if (id < E_OK) {
		return NULL;
	}

	pxMutexBuffer->id = id;
	pxMutexBuffer->is_mutex = 1;
	return pxMutexBuffer;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	ER er;
	TMO tmout;

	if (xSemaphore == NULL) {
		return pdFALSE;
	}

	tmout = (xTicksToWait == portMAX_DELAY) ? TMO_FEVR : (TMO)xTicksToWait;

	if (xSemaphore->is_mutex) {
		er = tk_loc_mtx(xSemaphore->id, tmout);
	} else {
		er = tk_wai_sem(xSemaphore->id, 1, tmout);
	}
	return (er == E_OK) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	ER er;

	if (xSemaphore == NULL) {
		return pdFALSE;
	}

	if (xSemaphore->is_mutex) {
		er = tk_unl_mtx(xSemaphore->id);
	} else {
		er = tk_sig_sem(xSemaphore->id, 1);
	}
	return (er == E_OK) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	/* Mutex must not be used from ISR; landmarks uses counting sem from ISR */
	if (xSemaphore == NULL || xSemaphore->is_mutex) {
		return pdFALSE;
	}
	return (tk_sig_sem(xSemaphore->id, 1) == E_OK) ? pdTRUE : pdFALSE;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	if (xSemaphore == NULL) {
		return;
	}
	if (xSemaphore->is_mutex) {
		(void)tk_del_mtx(xSemaphore->id);
	} else {
		(void)tk_del_sem(xSemaphore->id);
	}
	xSemaphore->id = 0;
}

void vTaskDelay(TickType_t xTicksToDelay)
{
	/* Our tick mapping treats 1 FreeRTOS tick ≈ 1 ms (HAL tick / tk_dly_tsk) */
	(void)tk_dly_tsk((RELTIM)xTicksToDelay);
}

uint32_t ulTaskGetIdleRunTimeCounter(void)
{
	return 0U;
}

uint32_t hl_get_run_time_counter(void)
{
	/* Prefer DWT when enabled; fall back to HAL tick */
#if defined(DWT)
	return DWT->CYCCNT;
#else
	return 0U;
#endif
}

BaseType_t xPortIsInsideInterrupt(void)
{
	/* __get_IPSR is an inline function — do not use defined(__get_IPSR). */
	return (__get_IPSR() != 0U) ? pdTRUE : pdFALSE;
}
