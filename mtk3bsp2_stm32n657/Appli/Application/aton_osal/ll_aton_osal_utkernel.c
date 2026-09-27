/**
 * @file    ll_aton_osal_utkernel.c
 * @brief   ATON OSAL for μT-Kernel (TENOMI Phase C4)
 *
 * Enable with:
 *   -DTENOMI_BUILD_ATON_OSAL=1
 *   -DLL_ATON_OSAL=LL_ATON_OSAL_USER_IMPL
 *   -DAPP_HAS_PARALLEL_NETWORKS=0
 *   -I Application/aton_osal + landmarks ll_aton
 */
#if defined(TENOMI_BUILD_ATON_OSAL) && (TENOMI_BUILD_ATON_OSAL)

#include "ll_aton_config.h"

#if (LL_ATON_OSAL == LL_ATON_OSAL_USER_IMPL)

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

/* Defined in landmarks app.c (cam_mon). */
extern volatile uint32_t g_tenomi_npu_sig_cnt;

/* Pulls ll_aton_osal_user_impl.h → renames LL_ATON_OSAL_* to aton_osal_utkernel_* */
#include "ll_aton_osal.h"

static utkernel_osal_static_dummy_t _dao_mutex_buffer;
static utkernel_osal_static_dummy_t _dao_wait_queue_buffer;
static utkernel_osal_static_dummy_t _wfe_sem_buffer;
static utkernel_osal_static_dummy_t _cache_mutex_buffer;

#include "ll_aton_osal_rtos_template.c"

#endif /* LL_ATON_OSAL_USER_IMPL */

#else /* !TENOMI_BUILD_ATON_OSAL */

/* Placeholder so CubeIDE can compile this file before C4 flags are set. */
typedef int tenomi_aton_osal_utkernel_placeholder_t;

#endif /* TENOMI_BUILD_ATON_OSAL */
