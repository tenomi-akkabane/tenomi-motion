/**
 * @file    ll_aton_osal_user_impl.h
 * @brief   Glue for LL_ATON_OSAL_USER_IMPL → μT-Kernel OSAL (TENOMI)
 *
 * ll_aton_osal.h includes this file when:
 *   #define LL_ATON_OSAL LL_ATON_OSAL_USER_IMPL
 *
 * Place this directory on the include path ahead of other OSAL headers.
 */
#ifndef LL_ATON_OSAL_USER_IMPL_H
#define LL_ATON_OSAL_USER_IMPL_H

#include "ll_aton_osal_utkernel.h"

#define LL_ATON_OSAL_INIT()         aton_osal_utkernel_init()
#define LL_ATON_OSAL_DEINIT()       aton_osal_utkernel_deinit()

#define LL_ATON_OSAL_WFE()          aton_osal_utkernel_wfe()
#define LL_ATON_OSAL_SIGNAL_EVENT() aton_osal_utkernel_signal_event()

#if defined(APP_HAS_PARALLEL_NETWORKS) && (APP_HAS_PARALLEL_NETWORKS == 1)
#define LL_ATON_OSAL_LOCK_ATON()   aton_osal_utkernel_dao_lock()
#define LL_ATON_OSAL_UNLOCK_ATON() aton_osal_utkernel_dao_unlock()
#endif

#define LL_ATON_OSAL_LOCK_NPU_CACHE()   aton_osal_utkernel_lock()
#define LL_ATON_OSAL_UNLOCK_NPU_CACHE() aton_osal_utkernel_unlock()

#endif /* LL_ATON_OSAL_USER_IMPL_H */
