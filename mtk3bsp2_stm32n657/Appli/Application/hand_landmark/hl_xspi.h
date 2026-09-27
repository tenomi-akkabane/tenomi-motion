/**
 * @file hl_xspi.h
 * @brief Phase C2: XSPI PSRAM / NOR init for TENOMI
 */
#ifndef TENOMI_HL_XSPI_H
#define TENOMI_HL_XSPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return 0 on success, negative BSP/error code on failure */
int hl_xspi_init(void);

/**
 * After HCLK boost: re-apply XSPI1/2 ker clk, re-init PSRAM, then NOR @ ~200 MHz.
 */
int hl_xspi_reclock(void);

/** Non-zero after successful NOR Init + MMP (NN weights readable). */
int hl_xspi_nor_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* TENOMI_HL_XSPI_H */
