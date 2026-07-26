/**
 ******************************************************************************
 * @file    motor_driver.h
 * @brief   L298N direction control + TIM3 PWM speed control with ramping.
 *
 * ACC_CMD_STOP and ACC_CMD_SENSOR_FAULT snap directly to 0% (no ramp).
 * All other commands ramp toward their target duty at
 * ACC_MOTOR_RAMP_STEP_PCT per ACC_MOTOR_CONTROL_PERIOD_MS (PRD §3.4).
 ******************************************************************************
 */
#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include "tim.h"          /* CubeMX-generated TIM3 handle (htim3) */
#include "can_security.h" /* ACC_Command_t */
#include <stdint.h>
#include <stdbool.h>

/* Target duty cycle (%) per command -- named constants, no magic numbers. */
#define ACC_DUTY_STOP_PCT           (0U)
#define ACC_DUTY_DECELERATE_PCT     (30U)
#define ACC_DUTY_MAINTAIN_PCT       (60U)
#define ACC_DUTY_ACCELERATE_PCT     (90U)

#define ACC_MOTOR_RAMP_STEP_PCT     (5U)   /*!< max duty change per control iteration */
#define ACC_MOTOR_CONTROL_PERIOD_MS (50U)  /*!< MotorControlTask period               */

/**
 * @brief Initialize motor GPIOs to a safe (stopped) state and start PWM.
 * @param htim Pointer to the CubeMX-generated TIM3 handle.
 * @return HAL_OK on success, propagated HAL error otherwise.
 */
HAL_StatusTypeDef ACC_Motor_Init(TIM_HandleTypeDef *htim);

/**
 * @brief Set the ramp target based on a validated ACC command. Does not
 *        itself change the current duty -- call ACC_Motor_Update() to
 *        advance the ramp.
 * @param cmd Validated command from SecurityValidateTask.
 */
void ACC_Motor_SetTarget(ACC_Command_t cmd);

/**
 * @brief Advance the current duty one ramp step toward the target duty
 *        and apply it to the PWM compare register. Call once per
 *        MotorControlTask iteration (~ACC_MOTOR_CONTROL_PERIOD_MS).
 */
void ACC_Motor_Update(void);

/**
 * @brief Immediately zero the motor (no ramp), for STOP/FAULT commands and
 *        for the fail-safe path. Safe to call from any task.
 */
void ACC_Motor_ForceStop(void);

/**
 * @brief Read back the currently-applied duty and direction, e.g. for the
 *        OLED display.
 * @param[out] out_duty_pct Current duty, 0-100.
 * @param[out] out_forward  true if driving forward, false if stopped/idle.
 */
void ACC_Motor_GetStatus(uint8_t *out_duty_pct, bool *out_forward);

#endif /* MOTOR_DRIVER_H */
