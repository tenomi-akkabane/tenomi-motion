################################################################################
# TENOMI Phase C3 — landmarks app.c + ld.c (path ref) + C3 stubs
# C4: OSAL USER_IMPL defs come from tenomi_c4.mk (included from makefile first).
################################################################################

ifndef TENOMI_ROOT
TENOMI_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../..)
endif
LM := $(TENOMI_ROOT)/landmarks

TENOMI_C3_DEFS := $(TENOMI_C4_DEFS)
TENOMI_C3_INCLUDES := $(TENOMI_C4_INCLUDES)

C_SRCS += \
$(LM)/Src/app.c \
$(LM)/Src/ld.c \
../Application/hand_landmark/stubs/hl_app_stubs.c

OBJS += \
./Application/hand_landmark/c3/app.o \
./Application/hand_landmark/c3/ld.o \
./Application/hand_landmark/stubs/hl_app_stubs.o

C_DEPS += \
./Application/hand_landmark/c3/app.d \
./Application/hand_landmark/c3/ld.d \
./Application/hand_landmark/stubs/hl_app_stubs.d

Application/hand_landmark/c3/app.o: $(LM)/Src/app.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) $(TENOMI_C3_DEFS) -c $(TENOMI_C3_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/c3/ld.o: $(LM)/Src/ld.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) $(TENOMI_C3_DEFS) -c $(TENOMI_C3_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Application/hand_landmark/stubs/hl_app_stubs.o: ../Application/hand_landmark/stubs/hl_app_stubs.c
	arm-none-eabi-gcc "$<" $(TENOMI_CC_COMMON) $(TENOMI_C3_DEFS) -c $(TENOMI_C3_INCLUDES) -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"
