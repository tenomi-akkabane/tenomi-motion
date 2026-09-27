################################################################################
# TENOMI Phase C1 — extra HAL sources (not yet in CubeIDE auto list)
# CubeIDE refresh may overwrite Drivers/.../subdir.mk; keep this file.
################################################################################

HAL_DRV := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../Drivers/STM32N6xx_HAL_Driver)

TENOMI_HAL_EXTRA := \
stm32n6xx_hal_bsec \
stm32n6xx_hal_cacheaxi \
stm32n6xx_hal_dcmipp \
stm32n6xx_hal_dma2d \
stm32n6xx_hal_gfxmmu \
stm32n6xx_hal_gpu2d \
stm32n6xx_hal_icache \
stm32n6xx_hal_ltdc \
stm32n6xx_hal_ltdc_ex \
stm32n6xx_hal_ramcfg \
stm32n6xx_hal_rif \
stm32n6xx_hal_spi \
stm32n6xx_hal_spi_ex \
stm32n6xx_hal_tim \
stm32n6xx_hal_tim_ex \
stm32n6xx_hal_uart \
stm32n6xx_hal_uart_ex
# stm32n6xx_hal_xspi — already in CubeIDE Drivers subdir.mk

C_SRCS += $(foreach m,$(TENOMI_HAL_EXTRA),$(HAL_DRV)/Src/$(m).c)
OBJS   += $(foreach m,$(TENOMI_HAL_EXTRA),./Drivers/STM32N6xx_HAL_Driver/$(m).o)
C_DEPS += $(foreach m,$(TENOMI_HAL_EXTRA),./Drivers/STM32N6xx_HAL_Driver/$(m).d)

Drivers/STM32N6xx_HAL_Driver/%.o: $(HAL_DRV)/Src/%.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -D_STM32CUBE_DISCOVERY_N657_ -c -I../Core/Inc -I../../Secure_nsclib -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Include -I"../mtk3_bsp2" -I"../mtk3_bsp2/config" -I"../mtk3_bsp2/include" -I"../mtk3_bsp2/mtkernel/kernel/knlinc" -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
