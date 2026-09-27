/**
 * @file    ll_aton_osal_utkernel.h
 * @brief   ATON OSAL macros for μT-Kernel 3.0 (TENOMI Phase B skeleton)
 *
 * Mirrors landmarks ll_aton_osal_freertos.h / ll_aton_osal_threadx.h so that
 * ll_aton_osal_rtos_template.c can be reused in Phase C.
 *
 * Not linked yet: AI Runtime is not in the Appli build.
 * When integrating, define:
 *   -DLL_ATON_OSAL=LL_ATON_OSAL_USER_IMPL
 *   -DAPP_HAS_PARALLEL_NETWORKS=0
 * and put this directory + ST ll_aton headers on the include path.
 * Provide ll_aton_osal_user_impl.h (see sibling file) for ll_aton_osal.h.
 */
#ifndef LL_ATON_OSAL_UTKERNEL_H
#define LL_ATON_OSAL_UTKERNEL_H

#ifndef APP_HAS_PARALLEL_NETWORKS
#define APP_HAS_PARALLEL_NETWORKS 0
#endif

#include <tk/tkernel.h>

/* Minimal stand-ins so this header is readable without full ATON tree.
 * When Phase C adds AI Runtime, prefer including the real headers instead
 * and remove these guards if they conflict.
 */
#ifndef LL_ATON_ASSERT
#include <assert.h>
#define LL_ATON_ASSERT(x) assert(x)
#endif
#ifndef LL_ATON_LIB_UNUSED
#define LL_ATON_LIB_UNUSED(x) ((void)(x))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void aton_osal_utkernel_init(void);
void aton_osal_utkernel_deinit(void);
void aton_osal_utkernel_wfe(void);
void aton_osal_utkernel_signal_event(void);
void aton_osal_utkernel_dao_lock(void);
void aton_osal_utkernel_dao_unlock(void);
void aton_osal_utkernel_lock(void);
void aton_osal_utkernel_unlock(void);

/*** Types (μT-Kernel object IDs) ***/
#define _DaoMutexNoWaitersType_ ID
#define _DaoWaitQueueType_      ID
#define _DaoWaitQueueValueType_ INT
#define _WfeSemaphoreType_      ID
#define _CacheMutexType_        ID
#define _TaskHandleType_        ID
#define _PriorityType_          PRI
#define _ReturnType_            ER

#define _OsTrue_     E_OK
#define _NullHandle_ ((ID)0)

/* Dummy "static buffer" type: μT-Kernel allocates objects in kernel memory */
typedef int utkernel_osal_static_dummy_t;

static inline ER _ut_cre_sem(ID *out, INT isemcnt, INT maxsem)
{
  T_CSEM csem;
  ID id;

  csem.exinf = 0;
  csem.sematr = TA_TFIFO;
  csem.isemcnt = isemcnt;
  csem.maxsem = maxsem;
  id = tk_cre_sem(&csem);
  if (id < E_OK) {
    return id;
  }
  *out = id;
  return E_OK;
}

static inline ER _ut_cre_mtx(ID *out)
{
  T_CMTX cmtx;
  ID id;

  cmtx.exinf = 0;
  cmtx.mtxatr = TA_INHERIT;
  cmtx.ceilpri = 0;
  id = tk_cre_mtx(&cmtx);
  if (id < E_OK) {
    return id;
  }
  *out = id;
  return E_OK;
}

static inline PRI _ut_get_task_priority(ID tskid)
{
  T_RTSK rtsk;
  ER er = tk_ref_tsk(tskid, &rtsk);
  LL_ATON_ASSERT(er == E_OK);
  LL_ATON_LIB_UNUSED(er);
  return rtsk.tskpri;
}

static inline INT _ut_get_sem_count(ID semid)
{
  T_RSEM rsem;
  ER er = tk_ref_sem(semid, &rsem);
  LL_ATON_ASSERT(er == E_OK);
  LL_ATON_LIB_UNUSED(er);
  return rsem.semcnt;
}

/*** Create ***/
#define _CreateDaoMutexNoWaiters_(_dao_obj, _dao_static_buffer)                                                        \
  _ut_cre_sem(&(_dao_obj), 0, 1)
#define _CreateDaoWaitQueue_(_dao_obj, _dao_static_buffer)                                                             \
  _ut_cre_sem(&(_dao_obj), 0, 1)
#define _CreateWfeSemaphore_(_dao_obj, _dao_static_buffer)                                                             \
  _ut_cre_sem(&(_dao_obj), 0, 65535)
#define _CreateCacheMutex_(_dao_obj, _dao_static_buffer) _ut_cre_mtx(&(_dao_obj))

/* IRQ priority: set in hl_npu_enable() (ATON_STD_IRQ_LINE not visible here). */
#define _FinalizeIRQHandling_()

#define _DisablePreemption_() (void)tk_dis_dsp()
#define _EnablePreemption_()  (void)tk_ena_dsp()

#define _InitNonDao_()
#define _DeInitNonDao_()

#define _GetCurrentTaskHandle_()        tk_get_tid()
#define _GetTaskPriority_(_task)        _ut_get_task_priority(_task)
#define _SetTaskPriority_(_task, _prio) (void)tk_chg_pri((_task), (_prio))
#define _YieldCurrentTask_()            (void)tk_rot_rdq(TPRI_RUN)

#define _MakeDaoMutexNoWaitersAvailable_(_mutex) (void)tk_sig_sem((_mutex), 1)
#define _GetDaoMutexNoWaiters_(_mutex)           tk_wai_sem((_mutex), 1, TMO_POL)
#define _ReleaseDaoMutexNoWaiters_(_mutex)       tk_sig_sem((_mutex), 1)

#define _MakeDaoWaitQueueUnavailable_(_wq)
#define _GetDaoWaitQueueValue_(_wq) _ut_get_sem_count(_wq)
#define _GetDaoWaitQueue_(_wq)      tk_wai_sem((_wq), 1, TMO_FEVR)
#define _ReleaseDaoWaitQueue_(_wq)  tk_sig_sem((_wq), 1)

#define _MakeWfeSemaphoreUnavailable_(_sem)
#define _GetWfeSemaphore_(_sem)             tk_wai_sem((_sem), 1, TMO_FEVR)
#define _ReleaseWfeSemaphore_(_sem)         tk_sig_sem((_sem), 1)
/* Same API from ISR: μT-Kernel allows tk_sig_sem in task-independent context */
extern volatile uint32_t g_tenomi_npu_sig_cnt;
#define _ReleaseWfeSemaphoreISR_(_sem, ...)                                                                            \
  (g_tenomi_npu_sig_cnt++, tk_sig_sem((_sem), 1))

#define _MakeCacheMutexAvailable_(_mutex)
#define _GetCacheMutex_(_mutex)     tk_loc_mtx((_mutex), TMO_FEVR)
#define _ReleaseCacheMutex_(_mutex) tk_unl_mtx(_mutex)

#define _HeadIsrCode_(...)
#define _TailIsrCode_(...)

/* μT-Kernel: smaller priority number = higher priority (same as ThreadX OSAL) */
#define _FirstPrioHigherThanScnd_(_first, _second) ((_first) < (_second))

#ifdef __cplusplus
}
#endif

#endif /* LL_ATON_OSAL_UTKERNEL_H */
