/**
 * @file    os_bsp.c
 * @brief   HAL tick/delay bridge for μT-Kernel 3.0 (TENOMI Phase B)
 *
 * Replaces landmarks/Src/freertos_bsp.c roles:
 *   HAL_GetTick / HAL_Delay  (FreeRTOS xTaskGetTickCount / vTaskDelay)
 *
 * Pre-kernel: use HAL uwTick (SysTick still driven by HAL_IncTick).
 * Post-kernel: use tk_get_otm / tk_dly_tsk (CNF_TIMER_PERIOD ms units).
 *
 * After kernel start, HAL_RCC_ClockConfig must NOT reprogram SysTick via
 * HAL_InitTick — that steals the timer from μT-Kernel and freezes tk_dly_tsk.
 */
#include <assert.h>
#include <stdint.h>

#include "cmsis_compiler.h"
#include "os_bsp.h"
#include "stm32n6xx_hal.h"

#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <mtkernel/lib/libtm/libtm.h>

#ifndef CNF_TIMER_PERIOD
#define CNF_TIMER_PERIOD 1
#endif

#define IS_IRQ_MODE() (__get_IPSR() != 0U)

/* Declared in stm32n6xx_hal.c */
extern __IO uint32_t uwTick;
extern uint32_t uwTickPrio;
extern HAL_TickFreqTypeDef uwTickFreq;
extern UW knl_sysclk;

static volatile int s_kernel_time_ready;

void os_bsp_notify_kernel_started(void)
{
  s_kernel_time_ready = 1;
}

/**
 * After CPU clock changes under a running kernel: refresh knl_sysclk and
 * reload SysTick for TIMER_PERIOD (do not use HAL_InitTick).
 */
void os_bsp_retune_kernel_timer(void)
{
  uint32_t load;

  SystemCoreClockUpdate();
  knl_sysclk = SystemCoreClock;

  /* Same formula as knl_start_hw_timer(): period_ms * (clk_Hz/1000) - 1 */
  load = (uint32_t)CNF_TIMER_PERIOD * (knl_sysclk / 1000U);
  if (load < 2U) {
    load = 2U;
  }
  SysTick->CTRL = 0U;
  SysTick->LOAD = load - 1U;
  SysTick->VAL = 0U;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                  SysTick_CTRL_ENABLE_Msk;
}

/**
 * Override weak HAL_InitTick: after usermain, SysTick belongs to μT-Kernel.
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  if (s_kernel_time_ready) {
    /* ClockConfig called post-kernel — retune knl timer instead of HAL tick */
    (void)TickPriority;
    os_bsp_retune_kernel_timer();
    return HAL_OK;
  }

  if ((uint32_t)uwTickFreq == 0UL) {
    return HAL_ERROR;
  }

  if (HAL_SYSTICK_Config(SystemCoreClock / (1000UL / (uint32_t)uwTickFreq)) > 0U) {
    return HAL_ERROR;
  }

  if (TickPriority < (1UL << __NVIC_PRIO_BITS)) {
    HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
    uwTickPrio = TickPriority;
  } else {
    return HAL_ERROR;
  }

  return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
  SYSTIM tim;

  if (!s_kernel_time_ready) {
    return uwTick;
  }

  /* knl_current_time advances by TIMER_PERIOD (ms) each tick → lo is milliseconds */
  (void)tk_get_otm(&tim);
  return tim.lo;
}

void HAL_Delay(uint32_t Delay)
{
  if (IS_IRQ_MODE()) {
    assert(0);
    return;
  }

  if (!s_kernel_time_ready) {
    uint32_t tickstart = uwTick;
    if (Delay < HAL_MAX_DELAY) {
      Delay += 1U; /* same guard as ST weak HAL_Delay */
    }
    while ((uwTick - tickstart) < Delay) {
      /* busy wait until kernel takes SysTick */
    }
    return;
  }

  if (Delay == 0U) {
    return;
  }

  (void)tk_dly_tsk((RELTIM)Delay);
}

/**
 * Bridge newlib printf → USART1 (same as tm_printf / T-Monitor).
 * Required for APP_JSON_ENABLE landmark lines.
 */
int __io_putchar(int ch)
{
  UB c = (UB)ch;

  tm_snd_dat(&c, 1);
  return ch;
}
