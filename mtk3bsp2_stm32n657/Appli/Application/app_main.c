#include <tk/tkernel.h>

#include "os_bsp.h"
#include "hand_landmark/hl_startup.h"

/* μT-Kernel: smaller itskpri = higher priority.
 * nn thread maps to ~15 (FreeRTOS FREERTOS_PRIORITY(1)).
 * LED tasks were 10 and could preempt NN; keep them below dp (~18). */
#define TENOMI_LED_TASK_PRI 20

LOCAL void task_1(INT stacd, void *exinf);	// task execution function
LOCAL ID	tskid_1;			// Task ID number
LOCAL T_CTSK ctsk_1 = {				// Task creation information
	.itskpri	= TENOMI_LED_TASK_PRI,	.stksz		= 1024,
	.task		= task_1,
	.tskatr		= TA_HLNG | TA_RNG3,
};

LOCAL void task_2(INT stacd, void *exinf);	// task execution function
LOCAL ID	tskid_2;			// Task ID number
LOCAL T_CTSK ctsk_2 = {				// Task creation information
	.itskpri	= TENOMI_LED_TASK_PRI,	.stksz		= 1024,
	.task		= task_2,
	.tskatr		= TA_HLNG | TA_RNG3,
};

LOCAL void task_1(INT stacd, void *exinf)
{
	(void)stacd;
	(void)exinf;
	while(1) {
		/* UART quiet for M6 JSON (LED still blinks). */
		out_h(GPIO_ODR(O), in_h(GPIO_ODR(O))^(1<<1));
		tk_dly_tsk(500);
	}
}

LOCAL void task_2(INT stacd, void *exinf)
{
	(void)stacd;
	(void)exinf;
	while(1) {
		tk_dly_tsk(700);
	}
}

/* usermain関数 */
EXPORT INT usermain(void)
{
	/* HAL_GetTick/HAL_Delay switch to μT-Kernel time from here */
	os_bsp_notify_kernel_started();

	/* Turn off the LED on the board. */
	out_h(GPIO_ODR(O), in_h(GPIO_ODR(O))&~(1<<1));

	/* Create & Start Tasks */
	tskid_1 = tk_cre_tsk(&ctsk_1);
	tk_sta_tsk(tskid_1, 0);

	tskid_2 = tk_cre_tsk(&ctsk_2);
	tk_sta_tsk(tskid_2, 0);

	/* Phase C0: hand_landmark RTOS stub (does not replace LED tasks) */
	hand_landmark_startup();

	tk_slp_tsk(TMO_FEVR);

	return 0;
}
