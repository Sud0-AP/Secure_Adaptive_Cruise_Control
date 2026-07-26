################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Middleware/mbedtls/library/aes.c \
../Middleware/mbedtls/library/cipher.c \
../Middleware/mbedtls/library/cipher_wrap.c \
../Middleware/mbedtls/library/cmac.c \
../Middleware/mbedtls/library/constant_time.c \
../Middleware/mbedtls/library/platform_util.c 

OBJS += \
./Middleware/mbedtls/library/aes.o \
./Middleware/mbedtls/library/cipher.o \
./Middleware/mbedtls/library/cipher_wrap.o \
./Middleware/mbedtls/library/cmac.o \
./Middleware/mbedtls/library/constant_time.o \
./Middleware/mbedtls/library/platform_util.o 

C_DEPS += \
./Middleware/mbedtls/library/aes.d \
./Middleware/mbedtls/library/cipher.d \
./Middleware/mbedtls/library/cipher_wrap.d \
./Middleware/mbedtls/library/cmac.d \
./Middleware/mbedtls/library/constant_time.d \
./Middleware/mbedtls/library/platform_util.d 


# Each subdirectory must supply rules for building sources it contributes
Middleware/mbedtls/library/%.o Middleware/mbedtls/library/%.su Middleware/mbedtls/library/%.cyclo: ../Middleware/mbedtls/library/%.c Middleware/mbedtls/library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG '-DMBEDTLS_CONFIG_FILE="mbedtls_config.h"' -DUSE_HAL_DRIVER -DSTM32F407xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS/portable/GCC/ARM_CM4F" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS/include" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/SEGGER" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/FreeRTOSV11" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/Config" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/u8g2" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls/include" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls/library" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middleware-2f-mbedtls-2f-library

clean-Middleware-2f-mbedtls-2f-library:
	-$(RM) ./Middleware/mbedtls/library/aes.cyclo ./Middleware/mbedtls/library/aes.d ./Middleware/mbedtls/library/aes.o ./Middleware/mbedtls/library/aes.su ./Middleware/mbedtls/library/cipher.cyclo ./Middleware/mbedtls/library/cipher.d ./Middleware/mbedtls/library/cipher.o ./Middleware/mbedtls/library/cipher.su ./Middleware/mbedtls/library/cipher_wrap.cyclo ./Middleware/mbedtls/library/cipher_wrap.d ./Middleware/mbedtls/library/cipher_wrap.o ./Middleware/mbedtls/library/cipher_wrap.su ./Middleware/mbedtls/library/cmac.cyclo ./Middleware/mbedtls/library/cmac.d ./Middleware/mbedtls/library/cmac.o ./Middleware/mbedtls/library/cmac.su ./Middleware/mbedtls/library/constant_time.cyclo ./Middleware/mbedtls/library/constant_time.d ./Middleware/mbedtls/library/constant_time.o ./Middleware/mbedtls/library/constant_time.su ./Middleware/mbedtls/library/platform_util.cyclo ./Middleware/mbedtls/library/platform_util.d ./Middleware/mbedtls/library/platform_util.o ./Middleware/mbedtls/library/platform_util.su

.PHONY: clean-Middleware-2f-mbedtls-2f-library

