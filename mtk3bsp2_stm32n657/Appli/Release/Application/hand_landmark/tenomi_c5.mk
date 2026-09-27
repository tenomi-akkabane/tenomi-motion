################################################################################
# TENOMI C5 / M3 — HAL extras only
#
# CubeIDE already builds Application/hand_landmark/c5/* via c5/subdir.mk and
# hl_security via hand_landmark/subdir.mk. Do NOT re-add those sources here
# (causes "overriding recipe" warnings).
#
# Link must use $(OBJS) (see ../../makefile.targets) so these HAL objects are
# included; @"objects.list" omits them.
################################################################################

-include Drivers/STM32N6xx_HAL_Driver/tenomi_hal_extra.mk
-include Application/hand_landmark/tenomi_m4.mk

# Q3: app_cam.c includes app.h (DISPLAY_BPP / DISPLAY_FORMAT for DCMIPP).
# CubeIDE c5/subdir.mk hardcodes HAS_ROTATION_SUPPORT=0 → RGB888 pitch 2400
# while app.c (LTDC) uses ARGB8888 pitch 3200 → LCD snow / 4-quadrant corruption.
Application/hand_landmark/c5/app_cam.o: $(LM)/Src/app_cam.c
	@mkdir -p Application/hand_landmark/c5
	arm-none-eabi-gcc "$<" $(TENOMI_M4_CFLAGS) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
