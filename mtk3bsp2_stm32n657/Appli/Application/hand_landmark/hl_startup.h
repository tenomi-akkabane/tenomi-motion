/**
 * @file hl_startup.h
 * @brief Hand-landmark integration entry from usermain (Phase C)
 */
#ifndef TENOMI_HL_STARTUP_H
#define TENOMI_HL_STARTUP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Phase C0: prints stub message and exercises hl_rtos (create task/sem).
 * Later: HW init + app_run() equivalent.
 */
void hand_landmark_startup(void);

#ifdef __cplusplus
}
#endif

#endif /* TENOMI_HL_STARTUP_H */
