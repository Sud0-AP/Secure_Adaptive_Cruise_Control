################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.c 

OBJS += \
./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.o 

C_DEPS += \
./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.d 


# Each subdirectory must supply rules for building sources it contributes
Middleware/FreeRTOS/portable/GCC/ARM_CM4F/%.o Middleware/FreeRTOS/portable/GCC/ARM_CM4F/%.su Middleware/FreeRTOS/portable/GCC/ARM_CM4F/%.cyclo: ../Middleware/FreeRTOS/portable/GCC/ARM_CM4F/%.c Middleware/FreeRTOS/portable/GCC/ARM_CM4F/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG '-DMBEDTLS_CONFIG_FILE="mbedtls_config.h"' -DUSE_HAL_DRIVER -DSTM32F407xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS/portable/GCC/ARM_CM4F" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS/include" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/FreeRTOS" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/SEGGER" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/FreeRTOSV11" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/SEGGER/Config" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/u8g2" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls/include" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Actuator/Middleware/mbedtls/library" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middleware-2f-FreeRTOS-2f-portable-2f-GCC-2f-ARM_CM4F

clean-Middleware-2f-FreeRTOS-2f-portable-2f-GCC-2f-ARM_CM4F:
	-$(RM) ./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.cyclo ./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.d ./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.o ./Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.su

.PHONY: clean-Middleware-2f-FreeRTOS-2f-portable-2f-GCC-2f-ARM_CM4F

