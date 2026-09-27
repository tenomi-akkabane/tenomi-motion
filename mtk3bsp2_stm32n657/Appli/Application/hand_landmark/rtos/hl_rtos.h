/**
 * @file hl_rtos.h
 * @brief FreeRTOS static-API subset mapped to μT-Kernel 3.0 (TENOMI Phase C)
 *
 * Covers APIs used by landmarks Src/app.c (and similar):
 *   xTaskCreateStatic, xSemaphoreCreate*Static, Take/Give/GiveFromISR,
 *   portMAX_DELAY, portYIELD_FROM_ISR
 */
#ifndef TENOMI_HL_RTOS_H
#define TENOMI_HL_RTOS_H

#include <stddef.h>
#include <stdint.h>

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;

typedef void (*TaskFunction_t)(void *);

typedef struct hl_static_task {
	ID tskid;
	TaskFunction_t fn;
	void *arg;
} StaticTask_t;

typedef struct hl_static_sem {
	ID id;
	int is_mutex;
} StaticSemaphore_t;

typedef StaticTask_t *TaskHandle_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;

#ifndef pdTRUE
#define pdTRUE  ((BaseType_t)1)
#endif
#ifndef pdFALSE
#define pdFALSE ((BaseType_t)0)
#endif
#ifndef pdPASS
#define pdPASS  pdTRUE
#endif

#define portMAX_DELAY ((TickType_t)TMO_FEVR)

/* FreeRTOS: higher number = higher priority. μT-Kernel: opposite. */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES 32
#endif
#ifndef tskIDLE_PRIORITY
#define tskIDLE_PRIORITY 0
#endif
#ifndef configMINIMAL_STACK_SIZE
#define configMINIMAL_STACK_SIZE 1024
#endif

#ifndef FREERTOS_PRIORITY
#define FREERTOS_PRIORITY(p)                                                                                           \
	((UBaseType_t)((int)tskIDLE_PRIORITY + (int)configMAX_PRIORITIES / 2 + (int)(p)))
#endif

#define portYIELD_FROM_ISR(x)                                                                                          \
	do {                                                                                                           \
		(void)(x);                                                                                             \
	} while (0)

/* Used by landmarks app.c cpuload / bqueue */
uint32_t hl_get_run_time_counter(void);
BaseType_t xPortIsInsideInterrupt(void);
#define portGET_RUN_TIME_COUNTER_VALUE() hl_get_run_time_counter()

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char *const pcName, uint32_t ulStackDepth,
                               void *const pvParameters, UBaseType_t uxPriority, StackType_t *const puxStackBuffer,
                               StaticTask_t *const pxTaskBuffer);

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount,
                                                 StaticSemaphore_t *pxSemaphoreBuffer);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer);

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken);

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

void vTaskDelay(TickType_t xTicksToDelay);

/* CPU-load helper used by landmarks; stub until TIM stats ported */
uint32_t ulTaskGetIdleRunTimeCounter(void);

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif

#ifdef __cplusplus
}
#endif

#endif /* TENOMI_HL_RTOS_H */
