################################################################################
# TENOMI Phase C4 — ATON OSAL USER_IMPL (μT-Kernel)
# Shared defs used by C3 app compile + aton_osal override rule.
################################################################################

ifndef TENOMI_ROOT
TENOMI_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../..)
endif
LM := $(TENOMI_ROOT)/landmarks

TENOMI_C4_DEFS := \
-DDEBUG \
-DUSE_HAL_DRIVER \
-DSTM32N657xx \
-D_STM32CUBE_DISCOVERY_N657_ \
-DSTM32N6570_DK_REV \
-DHAS_ROTATION_SUPPORT=$(TENOMI_NEMA_ENABLE) \
-DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 \
-DLL_ATON_OSAL=LL_ATON_OSAL_USER_IMPL \
-DLL_ATON_RT_MODE=LL_ATON_RT_ASYNC \
-DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1 \
-DAPP_HAS_PARALLEL_NETWORKS=0 \
-DTENOMI_BUILD_ATON_OSAL=1 \
-DAPP_JSON_ENABLE=$(TENOMI_JSON_ENABLE) \
-DAPP_JSON_INTERVAL_MS=$(TENOMI_JSON_INTERVAL_MS) \
-DAPP_MOTION_ENABLE=$(TENOMI_MOTION_ENABLE)

TENOMI_C4_INCLUDES := \
-I../Application/hand_landmark/rtos \
-I../Application/aton_osal \
-I../Application/hand_landmark \
-I../Application/hand_landmark/bsp \
-I../Core/Inc \
-I../Application \
-I$(LM)/Inc \
-I$(LM)/Src \
-I$(LM)/Model \
-I$(LM)/Lib/AI_Runtime/Inc \
-I$(LM)/Lib/AI_Runtime/Npu/ll_aton \
-I$(LM)/Lib/AI_Runtime/Npu/Devices/STM32N6xx \
-I$(LM)/Lib/lib_vision_models_pp/lib_vision_models_pp/Inc \
-I$(LM)/Lib/ai-postprocessing-wrapper \
-I$(LM)/Lib/Camera_Middleware \
-I$(LM)/Lib/Camera_Middleware/sensors \
-I$(LM)/Lib/Camera_Middleware/ISP_Library/isp/Inc \
-I$(LM)/Lib/ipl/Inc \
-I$(LM)/Lib/screenl/Inc \
-I$(LM)/STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/Inc \
-I$(LM)/STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
-I$(LM)/STM32Cube_FW_N6/Drivers/CMSIS/Device/ST/STM32N6xx/Include \
-I$(LM)/STM32Cube_FW_N6/Drivers/CMSIS/Include \
-I$(LM)/STM32Cube_FW_N6/Drivers/CMSIS/DSP/Include \
-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/Components/Common \
-I$(LM)/STM32Cube_FW_N6/Drivers/BSP/STM32N6570-DK \
-I$(LM)/STM32Cube_FW_N6/Utilities/lcd \
-I$(LM)/STM32Cube_FW_N6/Utilities/Fonts \
-I../../Secure_nsclib \
-I../../Drivers/STM32N6xx_HAL_Driver/Inc \
-I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include \
-I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
-I../../Drivers/CMSIS/Include \
-I"../mtk3_bsp2" \
-I"../mtk3_bsp2/config" \
-I"../mtk3_bsp2/include" \
-I"../mtk3_bsp2/mtkernel/kernel/knlinc"

# Override auto-generated aton_osal rule so OSAL body is compiled
Application/aton_osal/ll_aton_osal_utkernel.o: ../Application/aton_osal/ll_aton_osal_utkernel.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) $(TENOMI_C4_DEFS) -c $(TENOMI_C4_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
