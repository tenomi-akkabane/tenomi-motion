/**
 * @file    os_bsp.h
 * @brief   HAL tick/delay bridge for μT-Kernel 3.0 (TENOMI Phase B)
 *
 * After knl_start_mtkernel(), SysTick is owned by μT-Kernel.
 * Call os_bsp_notify_kernel_started() once from usermain (or equivalent)
 * so HAL_GetTick()/HAL_Delay() switch from HAL uwTick to tk_get_otm()/tk_dly_tsk().
 */
#ifndef TENOMI_OS_BSP_H
#define TENOMI_OS_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

/** Mark that μT-Kernel time services are usable (call from usermain). */
void os_bsp_notify_kernel_started(void);

/** After HAL_RCC_ClockConfig under a running kernel: reload μT-Kernel SysTick. */
void os_bsp_retune_kernel_timer(void);

#ifdef __cplusplus
}
#endif

#endif /* TENOMI_OS_BSP_H */
