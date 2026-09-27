################################################################################
# TENOMI Phase C2  EXSPI BSP sources (linked from landmarks via .project)
# Fallback if CubeIDE has not yet regenerated linked-source rules.
################################################################################

ifndef TENOMI_ROOT
TENOMI_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../..)
endif
LM_BSP := $(TENOMI_ROOT)/landmarks/STM32Cube_FW_N6/Drivers/BSP
LM_DK  := $(LM_BSP)/STM32N6570-DK
LM_CMP := $(LM_BSP)/Components
HAL_DRV := $(TENOMI_ROOT)/mtk3bsp2_stm32n657/Drivers/STM32N6xx_HAL_Driver

TENOMI_XSPI_INCLUDES := \
-I../Application/hand_landmark/bsp \
-I$(LM_DK) \
-I$(LM_CMP)/aps256xx \
-I$(LM_CMP)/mx66uw1g45g \
-I../Core/Inc \
-I../Application \
-I../Application/hand_landmark \
-I../Application/hand_landmark/rtos -I../Application/aton_osal \
-I../../Secure_nsclib \
-I../../Drivers/STM32N6xx_HAL_Driver/Inc \
-I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include \
-I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
-I../../Drivers/CMSIS/Include \
-I"../mtk3_bsp2" \
-I"../mtk3_bsp2/config" \
-I"../mtk3_bsp2/include" \
-I"../mtk3_bsp2/mtkernel/kernel/knlinc"

C_SRCS += \
$(LM_DK)/stm32n6570_discovery_xspi.c \
$(LM_CMP)/aps256xx/aps256xx.c \
$(LM_CMP)/mx66uw1g45g/mx66uw1g45g.c \
$(HAL_DRV)/Src/stm32n6xx_hal_xspi.c

OBJS += \
./Application/hand_landmark/bsp_src/stm32n6570_discovery_xspi.o \
./Application/hand_landmark/bsp_src/aps256xx.o \
./Application/hand_landmark/bsp_src/mx66uw1g45g.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.o

C_DEPS += \
./Application/hand_landmark/bsp_src/stm32n6570_discovery_xspi.d \
./Application/hand_landmark/bsp_src/aps256xx.d \
./Application/hand_landmark/bsp_src/mx66uw1g45g.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.d

Application/hand_landmark/bsp_src/stm32n6570_discovery_xspi.o: $(LM_DK)/stm32n6570_discovery_xspi.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DSTM32N6570_DK_REV -c $(TENOMI_XSPI_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/bsp_src/aps256xx.o: $(LM_CMP)/aps256xx/aps256xx.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DSTM32N6570_DK_REV -c $(TENOMI_XSPI_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/bsp_src/mx66uw1g45g.o: $(LM_CMP)/mx66uw1g45g/mx66uw1g45g.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -DSTM32N6570_DK_REV -c $(TENOMI_XSPI_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.o: $(HAL_DRV)/Src/stm32n6xx_hal_xspi.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"../mtk3_bsp2" -I"../mtk3_bsp2/config" -I"../mtk3_bsp2/include" -I"../mtk3_bsp2/mtkernel/kernel/knlinc" -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
