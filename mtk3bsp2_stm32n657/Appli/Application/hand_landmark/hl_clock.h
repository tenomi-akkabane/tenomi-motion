/**
 * @file hl_clock.h
 * @brief Phase C5: enable PLL2/3/4 for DCMIPP / LTDC (landmarks SystemClock subset)
 */
#ifndef HL_CLOCK_H
#define HL_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/** Enable SMPS overdrive + PLL2/3/4. Does not change CPU/SYSCLK (keeps μT-Kernel SysTick). */
int hl_clock_init(void);

/** Power on AXISRAM3–6 (LCD default / NPU pools). Call after hl_clock_init(). */
void hl_axisram_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* HL_CLOCK_H */
