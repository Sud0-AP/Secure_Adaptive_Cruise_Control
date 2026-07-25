################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_tasks.c \
../Core/Src/can_driver.c \
../Core/Src/can_security.c \
../Core/Src/hcsr04_driver.c \
../Core/Src/main.c \
../Core/Src/oled_display.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_hal_timebase_tim.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/system_stm32f4xx.c 

OBJS += \
./Core/Src/app_tasks.o \
./Core/Src/can_driver.o \
./Core/Src/can_security.o \
./Core/Src/hcsr04_driver.o \
./Core/Src/main.o \
./Core/Src/oled_display.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_hal_timebase_tim.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/system_stm32f4xx.o 

C_DEPS += \
./Core/Src/app_tasks.d \
./Core/Src/can_driver.d \
./Core/Src/can_security.d \
./Core/Src/hcsr04_driver.d \
./Core/Src/main.d \
./Core/Src/oled_display.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_hal_timebase_tim.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/system_stm32f4xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/FreeRTOS/portable/GCC/ARM_CM4F" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/FreeRTOS/include" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/SEGGER/FreeRTOSV11" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/SEGGER/Config" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/FreeRTOS" -I"/home/Sud0/STM32CubeIDE/workspace_1.18.1/ACC_Sensor/Middleware/SEGGER/SEGGER" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_tasks.cyclo ./Core/Src/app_tasks.d ./Core/Src/app_tasks.o ./Core/Src/app_tasks.su ./Core/Src/can_driver.cyclo ./Core/Src/can_driver.d ./Core/Src/can_driver.o ./Core/Src/can_driver.su ./Core/Src/can_security.cyclo ./Core/Src/can_security.d ./Core/Src/can_security.o ./Core/Src/can_security.su ./Core/Src/hcsr04_driver.cyclo ./Core/Src/hcsr04_driver.d ./Core/Src/hcsr04_driver.o ./Core/Src/hcsr04_driver.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/oled_display.cyclo ./Core/Src/oled_display.d ./Core/Src/oled_display.o ./Core/Src/oled_display.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_hal_timebase_tim.cyclo ./Core/Src/stm32f4xx_hal_timebase_tim.d ./Core/Src/stm32f4xx_hal_timebase_tim.o ./Core/Src/stm32f4xx_hal_timebase_tim.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su

.PHONY: clean-Core-2f-Src

