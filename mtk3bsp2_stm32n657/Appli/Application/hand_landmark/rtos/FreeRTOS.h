/* Compatibility shim: landmarks includes "FreeRTOS.h" */
#ifndef TENOMI_FREERTOS_COMPAT_H
#define TENOMI_FREERTOS_COMPAT_H
#include "hl_rtos.h"
#ifndef configSUPPORT_STATIC_ALLOCATION
#define configSUPPORT_STATIC_ALLOCATION 1
#endif
#endif
