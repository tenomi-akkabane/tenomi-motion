################################################################################
# TENOMI compile options (Q2: Release / -O2)
#
# Default: -O2 for hand_landmark custom rules (tenomi_*.mk).
# Revert to -O0 baseline:  make TENOMI_OPT=-O0
#   or set TENOMI_OPT := -O0 below before Clean Build.
################################################################################

ifndef TENOMI_OPT
TENOMI_OPT := -O2
endif

ifndef TENOMI_GDB
TENOMI_GDB := -g3
endif

# Q5: JSON/UART. MotionMLP was trained on ~100 ms landmark frames — keep 100 ms.
# Override: make TENOMI_JSON_ENABLE=0 TENOMI_JSON_INTERVAL_MS=200 TENOMI_MOTION_ENABLE=0
ifndef TENOMI_JSON_ENABLE
TENOMI_JSON_ENABLE := 1
endif
ifndef TENOMI_JSON_INTERVAL_MS
TENOMI_JSON_INTERVAL_MS := 100
endif
ifndef TENOMI_MOTION_ENABLE
TENOMI_MOTION_ENABLE := 1
endif

# Q2b: CPUCLK 800 MHz trial (FreeRTOS-matched PLL1). Revert: TENOMI_CPUCLK_800MHZ := 0
ifndef TENOMI_CPUCLK_800MHZ
TENOMI_CPUCLK_800MHZ := 1
endif

# Q3: NemaGFX rotation (M1). Requires libnemagfx — see tools/setup_nema_lib.ps1
ifndef TENOMI_NEMA_ENABLE
TENOMI_NEMA_ENABLE := 1
endif

TENOMI_CC_COMMON := -mcpu=cortex-m55 -std=gnu11 $(TENOMI_GDB) $(TENOMI_OPT) \
	-ffunction-sections -fdata-sections -Wall -fstack-usage \
	-fcyclomatic-complexity -mcmse -MMD -MP --specs=nano.specs \
	-mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
