/**
 * @file hl_fault.c
 * @brief Richer BusFault dump (BFAR / CFSR) for TENOMI bring-up
 */
#include <tm/tmonitor.h>

/* Strong override of μT-Kernel weak knl_busfault_handler */
void knl_busfault_handler(void)
{
	volatile unsigned int *scb_cfsr = (volatile unsigned int *)0xE000ED28U;
	volatile unsigned int *scb_bfar = (volatile unsigned int *)0xE000ED38U;
	unsigned int cfsr = *scb_cfsr;
	unsigned int bfar = *scb_bfar;

	tm_printf((UB *)"*** Bus Fault *** CFSR=0x%08x BFAR=0x%08x\n", cfsr, bfar);
	if (cfsr & (1U << 15)) {
		tm_printf((UB *)"  BFAR valid — faulting address above\n");
	}
	while (1) {
	}
}
