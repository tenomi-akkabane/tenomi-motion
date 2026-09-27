TENOMI Phase C4 — ATON OSAL (μT-Kernel) USER_IMPL
=================================================

Files
-----
  ll_aton_osal_utkernel.h   … FreeRTOS/ThreadX 相当のマクロを μT-Kernel API に割当
  ll_aton_osal_utkernel.c   … ST テンプレート取り込み（TENOMI_BUILD_ATON_OSAL=1）
  ll_aton_osal_user_impl.h  … LL_ATON_OSAL_USER_IMPL 用グルー

Status
------
  Phase C4: 有効化済み（2026/8/2）
    -DLL_ATON_OSAL=LL_ATON_OSAL_USER_IMPL
    -DAPP_HAS_PARALLEL_NETWORKS=0
    -DTENOMI_BUILD_ATON_OSAL=1
    include: Application/aton_osal + landmarks ll_aton
  FEAT_FREERTOS / ll_aton_osal_freertos はビルドに含めない

Notes
-----
  - ISR からの WFE signal は tk_sig_sem（isig_sem は無い）
  - 優先度比較は「数値が小さいほど高い」（FreeRTOS OSAL と逆、ThreadX と同じ）
  - _FinalizeIRQHandling_ は空。CDNN IRQ 優先度は NPU 統合時（C5/M4）に設定する
