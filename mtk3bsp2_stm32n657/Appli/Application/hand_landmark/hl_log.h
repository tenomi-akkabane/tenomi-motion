/**
 * @file hl_log.h
 * @brief TENOMI hand_landmark UART logging
 *
 * TENOMI_VERBOSE_LOG=0 (default): quiet — JSON / faults / hard errors only.
 * Set to 1 to restore Phase C/M4 bring-up chatter.
 */
#ifndef TENOMI_HL_LOG_H
#define TENOMI_HL_LOG_H

#include <tm/tmonitor.h>

#ifndef TENOMI_VERBOSE_LOG
#define TENOMI_VERBOSE_LOG 0
#endif

#if TENOMI_VERBOSE_LOG
#define HL_LOG(...) tm_printf(__VA_ARGS__)
#else
#define HL_LOG(...) ((void)0)
#endif

/* Always print (NOR missing, init hard-fail, etc.) */
#define HL_ERR(...) tm_printf(__VA_ARGS__)

#endif /* TENOMI_HL_LOG_H */
