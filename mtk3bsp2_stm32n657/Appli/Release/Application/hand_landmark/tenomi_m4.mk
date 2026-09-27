################################################################################
# TENOMI M4/M5 — real PD (+ HL on M5) NOR weights + ll_aton + NetworkRuntime + PP
#
# Included from tenomi_c5.mk via makefile.defs.
# TENOMI_M4_REAL_NN=1 removes palm/PP stubs in hl_app_stubs.c
# TENOMI_M4_PD_ONLY=0 enables hand landmark (M5)
################################################################################

# <tenomi>/mtk3bsp2_stm32n657/Appli/Debug/Application/hand_landmark/ → 5 levels up = tenomi
ifndef TENOMI_ROOT
TENOMI_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../..)
endif
LM := $(TENOMI_ROOT)/landmarks
APPLI := $(TENOMI_ROOT)/mtk3bsp2_stm32n657/Appli

ifeq ($(TENOMI_NEMA_ENABLE),1)
TENOMI_ROTATION_DEF := -DHAS_ROTATION_SUPPORT=1
else
TENOMI_ROTATION_DEF := -DHAS_ROTATION_SUPPORT=0
endif

TENOMI_M4_DEFS := -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ \
	-DSTM32N6570_DK_REV $(TENOMI_ROTATION_DEF) \
	-DTENOMI_NEMA_ENABLE=$(TENOMI_NEMA_ENABLE) \
	-DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 -DLL_ATON_OSAL=LL_ATON_OSAL_USER_IMPL \
	-DLL_ATON_RT_MODE=LL_ATON_RT_ASYNC -DLL_ATON_SW_FALLBACK \
	-DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 -DAPP_HAS_PARALLEL_NETWORKS=0 \
	-DTENOMI_BUILD_ATON_OSAL=1 -DTENOMI_C5_VIDEO_ONLY=0 -DTENOMI_M4_PD_ONLY=0 \
	-DTENOMI_M4_REAL_NN=1 -DTENOMI_C5_REAL_CAM_LCD=1 -DTENOMI_C3_CALL_APP_RUN=1 \
	-DUSE_IMX335_SENSOR -DSCR_LIB_USE_LTDC

TENOMI_M4_INCLUDES := \
	-I../Application/hand_landmark/rtos -I../Application/aton_osal -I../Core/Inc \
	-I../Application -I../Application/hand_landmark -I../Application/hand_landmark/bsp \
	-I$(LM)/Inc -I$(LM)/Src -I$(LM)/Model \
	-I$(LM)/Lib/AI_Runtime/Inc \
	-I$(LM)/Lib/AI_Runtime/Npu/ll_aton \
	-I$(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx \
	-I$(LM)/Lib/lib_vision_models_pp/lib_vision_models_pp/Inc \
	-I$(LM)/Lib/ai-postprocessing-wrapper \
	-I$(LM)/Lib/Camera_Middleware \
	-I$(LM)/Lib/Camera_Middleware/sensors \
	-I$(LM)/Lib/Camera_Middleware/sensors/imx335 \
	-I$(LM)/Lib/Camera_Middleware/ISP_Library/isp/Inc \
	-I$(LM)/Lib/ipl/Inc -I$(LM)/Lib/screenl/Inc \
	-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/STM32N6570-DK \
	-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/Components/Common \
	-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/Components/aps256xx \
	-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/Components/mx66uw1g45g \
	-I$(LM)/STM32Cube_FW_N6/Drivers/CMSIS/DSP/Include \
	-I$(LM)/STM32Cube_FW_N6/Utilities/lcd \
	-I$(LM)/STM32Cube_FW_N6/Utilities/Fonts \
	-I../../Secure_nsclib \
	-I../../Drivers/STM32N6xx_HAL_Driver/Inc \
	-I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include \
	-I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
	-I../../Drivers/CMSIS/Include \
	-I"$(APPLI)/mtk3_bsp2" \
	-I"$(APPLI)/mtk3_bsp2/config" \
	-I"$(APPLI)/mtk3_bsp2/include" \
	-I"$(APPLI)/mtk3_bsp2/mtkernel/kernel/knlinc"

# Q3: app.c includes nema_core.h when HAS_ROTATION_SUPPORT=1
ifeq ($(TENOMI_NEMA_ENABLE),1)
TENOMI_M4_INCLUDES += -I$(LM)/Lib/NemaGFX/include
endif

TENOMI_M4_CFLAGS = $(TENOMI_CC_COMMON) $(TENOMI_M4_DEFS) -c $(TENOMI_M4_INCLUDES)

TENOMI_M4_CFLAGS_OS = $(TENOMI_CC_COMMON) $(TENOMI_M4_DEFS) -c $(TENOMI_M4_INCLUDES)

# --- NetworkRuntime ---
LIBS += -L"$(LM)/Lib/AI_Runtime/Lib/GCC/ARMCortexM55" -l:NetworkRuntime1100_CM55_GCC.a

# --- npu_cache + mcu_cache ---
C_SRCS += $(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx/npu_cache.c \
	$(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx/mcu_cache.c
OBJS   += ./Application/hand_landmark/m4/npu_cache.o \
	./Application/hand_landmark/m4/mcu_cache.o
C_DEPS += ./Application/hand_landmark/m4/npu_cache.d \
	./Application/hand_landmark/m4/mcu_cache.d

Application/hand_landmark/m4/npu_cache.o: $(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx/npu_cache.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -include stm32n6xx_hal.h \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/m4/mcu_cache.o: $(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx/mcu_cache.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -include stm32n6xx_hal.h \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- Model (palm) ---
C_SRCS += $(LM)/Model/stai_palm_detector.c $(LM)/Model/palm_detector.c
OBJS   += ./Application/hand_landmark/m4/stai_palm_detector.o \
	./Application/hand_landmark/m4/palm_detector.o
C_DEPS += ./Application/hand_landmark/m4/stai_palm_detector.d \
	./Application/hand_landmark/m4/palm_detector.d

Application/hand_landmark/m4/stai_palm_detector.o: $(LM)/Model/stai_palm_detector.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS_OS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/m4/palm_detector.o: $(LM)/Model/palm_detector.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS_OS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- Model (hand landmark / M5) ---
C_SRCS += $(LM)/Model/stai_hand_landmark.c $(LM)/Model/hand_landmark.c
OBJS   += ./Application/hand_landmark/m4/stai_hand_landmark.o \
	./Application/hand_landmark/m4/hand_landmark.o
C_DEPS += ./Application/hand_landmark/m4/stai_hand_landmark.d \
	./Application/hand_landmark/m4/hand_landmark.d

ifneq ($(TENOMI_NEMA_ENABLE),1)
C_SRCS += $(LM)/Lib/ipl/Src/mve_resize.c
OBJS   += ./Application/hand_landmark/m4/mve_resize.o
C_DEPS += ./Application/hand_landmark/m4/mve_resize.d
endif

Application/hand_landmark/m4/stai_hand_landmark.o: $(LM)/Model/stai_hand_landmark.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS_OS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/m4/hand_landmark.o: $(LM)/Model/hand_landmark.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS_OS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

ifneq ($(TENOMI_NEMA_ENABLE),1)
Application/hand_landmark/m4/mve_resize.o: $(LM)/Lib/ipl/Src/mve_resize.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
endif

# --- ll_aton (no FreeRTOS OSAL) ---
LL_ATON_SRCS := ecloader.c ll_aton.c ll_aton_cipher.c ll_aton_debug.c \
	ll_aton_lib.c ll_aton_lib_sw_operators.c ll_aton_rt_main.c \
	ll_aton_runtime.c ll_aton_stai_internal.c ll_aton_util.c \
	ll_sw_float.c ll_sw_integer.c

C_SRCS += $(addprefix $(LM)/Lib/AI_Runtime/Npu/ll_aton/,$(LL_ATON_SRCS))
OBJS   += $(addprefix ./Application/hand_landmark/m4/ll_aton/,$(LL_ATON_SRCS:.c=.o))
C_DEPS += $(addprefix ./Application/hand_landmark/m4/ll_aton/,$(LL_ATON_SRCS:.c=.d))

Application/hand_landmark/m4/ll_aton/%.o: $(LM)/Lib/AI_Runtime/Npu/ll_aton/%.c
	@mkdir -p Application/hand_landmark/m4/ll_aton
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- Postprocess ---
C_SRCS += $(LM)/Lib/ai-postprocessing-wrapper/app_postprocess_mpe_pd_uf.c \
	$(LM)/Lib/lib_vision_models_pp/lib_vision_models_pp/Src/pd_pp_model.c
OBJS   += ./Application/hand_landmark/m4/app_postprocess_mpe_pd_uf.o \
	./Application/hand_landmark/m4/pd_pp_model.o
C_DEPS += ./Application/hand_landmark/m4/app_postprocess_mpe_pd_uf.d \
	./Application/hand_landmark/m4/pd_pp_model.d

Application/hand_landmark/m4/app_postprocess_mpe_pd_uf.o: $(LM)/Lib/ai-postprocessing-wrapper/app_postprocess_mpe_pd_uf.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/m4/pd_pp_model.o: $(LM)/Lib/lib_vision_models_pp/lib_vision_models_pp/Src/pd_pp_model.c
	@mkdir -p Application/hand_landmark/m4
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# Q5: override CubeIDE c3/subdir.mk so app.c gets JSON flags (Nema path: tenomi_nema.mk)
ifneq ($(TENOMI_NEMA_ENABLE),1)
Application/hand_landmark/c3/app.o: $(LM)/Src/app.c
	@mkdir -p Application/hand_landmark/c3
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) \
		-DAPP_JSON_ENABLE=$(TENOMI_JSON_ENABLE) \
		-DAPP_JSON_INTERVAL_MS=$(TENOMI_JSON_INTERVAL_MS) \
		-DAPP_MOTION_ENABLE=$(TENOMI_MOTION_ENABLE) \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
endif

# --- Motion hub (stop rule + MotionMLP). Weights are NOR @ 0x70A00000, not linked. ---
ifeq ($(TENOMI_MOTION_ENABLE),1)
C_SRCS += $(LM)/Src/stop.c $(LM)/Src/motion_mlp.c $(LM)/Src/motion_hub.c
OBJS   += ./Application/hand_landmark/motion/stop.o \
	./Application/hand_landmark/motion/motion_mlp.o \
	./Application/hand_landmark/motion/motion_hub.o
C_DEPS += ./Application/hand_landmark/motion/stop.d \
	./Application/hand_landmark/motion/motion_mlp.d \
	./Application/hand_landmark/motion/motion_hub.d

Application/hand_landmark/motion/%.o: $(LM)/Src/%.c
	@mkdir -p Application/hand_landmark/motion
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) \
		-DAPP_MOTION_ENABLE=$(TENOMI_MOTION_ENABLE) \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
endif

# Q2b: hl_clock.o needs TENOMI_CPUCLK_800MHZ from tenomi_build_opts.mk
Application/hand_landmark/hl_clock.o: ../Application/hand_landmark/hl_clock.c
	@mkdir -p Application/hand_landmark
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) \
		-DTENOMI_CPUCLK_800MHZ=$(TENOMI_CPUCLK_800MHZ) \
		-MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
