################################################################################
# TENOMI Q3 — NemaGFX + GPU rotation (HAS_ROTATION_SUPPORT=1)
#
# Requires: landmarks/Lib/NemaGFX/lib/core/cortex_m55/gcc/libnemagfx-float-abi-hard.a
# Run:  powershell -File tools/setup_nema_lib.ps1
#
# TENOMI_NEMA_ENABLE=0 in tenomi_build_opts.mk disables this block.
################################################################################

ifeq ($(TENOMI_NEMA_ENABLE),1)

ifndef TENOMI_ROOT
TENOMI_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../..)
endif
LM := $(TENOMI_ROOT)/landmarks
APPLI := $(TENOMI_ROOT)/mtk3bsp2_stm32n657/Appli
NEMA_LIB_DIR := $(LM)/Lib/NemaGFX/lib/core/cortex_m55/gcc
NEMA_LIB_A := $(NEMA_LIB_DIR)/libnemagfx-float-abi-hard.a

TENOMI_NEMA_DEFS := $(TENOMI_M4_DEFS) -DTENOMI_NEMA_ENABLE=1
TENOMI_NEMA_INCLUDES := $(TENOMI_M4_INCLUDES) \
	-I$(LM)/Lib/NemaGFX/include

TENOMI_NEMA_CFLAGS := $(TENOMI_CC_COMMON) $(TENOMI_NEMA_DEFS) -c $(TENOMI_NEMA_INCLUDES)

ifeq ($(wildcard $(NEMA_LIB_A)),)
$(error TENOMI Q3: Nema library missing at $(NEMA_LIB_A). Run: powershell -File tools/setup_nema_lib.ps1)
endif

LIBS += -L"$(NEMA_LIB_DIR)" -lnemagfx-float-abi-hard

# hl_gpu.c is already listed in Application/hand_landmark/subdir.mk — override compile flags only
Application/hand_landmark/hl_gpu.o: ../Application/hand_landmark/hl_gpu.c
	@mkdir -p Application/hand_landmark
	arm-none-eabi-gcc "$<" $(TENOMI_NEMA_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- nema_hal_freertos.c (FreeRTOS API → hl_rtos shim) ---
C_SRCS += $(LM)/Src/nema_hal_freertos.c
OBJS += ./Application/nema/nema_hal_freertos.o
C_DEPS += ./Application/nema/nema_hal_freertos.d

Application/nema/nema_hal_freertos.o: $(LM)/Src/nema_hal_freertos.c
	@mkdir -p Application/nema
	arm-none-eabi-gcc "$<" $(TENOMI_NEMA_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- hl_startup / stm32n6xx_it: need TENOMI_NEMA_ENABLE ---
Application/hand_landmark/hl_startup.o: ../Application/hand_landmark/hl_startup.c
	@mkdir -p Application/hand_landmark
	arm-none-eabi-gcc "$<" $(TENOMI_NEMA_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Core/Src/stm32n6xx_it.o: ../Core/Src/stm32n6xx_it.c
	@mkdir -p Core/Src
	arm-none-eabi-gcc "$<" $(TENOMI_NEMA_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# Q5/Q3: app.c needs Nema headers + JSON flags (override c3/subdir.mk)
Application/hand_landmark/c3/app.o: $(LM)/Src/app.c
	@mkdir -p Application/hand_landmark/c3
	arm-none-eabi-gcc "$<" $(TENOMI_NEMA_CFLAGS) \
		-DAPP_JSON_ENABLE=$(TENOMI_JSON_ENABLE) \
		-DAPP_JSON_INTERVAL_MS=$(TENOMI_JSON_INTERVAL_MS) \
		-DAPP_MOTION_ENABLE=$(TENOMI_MOTION_ENABLE) \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

else

# Nema off: drop hl_gpu from CubeIDE subdir.mk (startup does not call hl_gpu_init)
C_SRCS := $(filter-out ../Application/hand_landmark/hl_gpu.c,$(C_SRCS))
OBJS := $(filter-out ./Application/hand_landmark/hl_gpu.o,$(OBJS))
C_DEPS := $(filter-out ./Application/hand_landmark/hl_gpu.d,$(C_DEPS))

endif
