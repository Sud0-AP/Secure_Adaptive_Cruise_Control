/**
 ******************************************************************************
 * @file    motor_driver.c
 * @brief   L298N direction/PWM control implementation.
 ******************************************************************************
 */
#include "motor_driver.h"
#include "main.h" /* GPIO port/pin defines for IN1/IN2, e.g. MOTOR_IN1_GPIO_Port */

#include "FreeRTOS.h"
#include "semphr.h"

/**
 * @brief Internal motor state, protected by xMotorMutex since both
 *        MotorControlTask and SafetyMonitorTask (fail-safe path) touch it.
 */
typedef struct
{
    uint8_t current_duty_pct;
    uint8_t target_duty_pct;
    bool    forward;
} ACC_MotorState_t;

static ACC_MotorState_t s_motor_state = {0};
static SemaphoreHandle_t s_motor_mutex = NULL;
static TIM_HandleTypeDef *s_htim = NULL;

/**
 * @brief Apply a duty percentage (0-100) to the PWM compare register.
 */
static void ACC_Motor_ApplyDutyLocked(uint8_t duty_pct)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(s_htim); /* ARR = 999 per CubeMX config */
    uint32_t compare = ((uint32_t)duty_pct * (period + 1U)) / 100U;
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, compare);

    HAL_GPIO_WritePin(MOTOR_IN1_GPIO_Port, MOTOR_IN1_Pin,
                       (s_motor_state.forward && duty_pct > 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_GPIO_Port, MOTOR_IN2_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef ACC_Motor_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;
    s_motor_mutex = xSemaphoreCreateMutex();
    configASSERT(s_motor_mutex != NULL);

    s_motor_state.current_duty_pct = ACC_DUTY_STOP_PCT;
    s_motor_state.target_duty_pct  = ACC_DUTY_STOP_PCT;
    s_motor_state.forward          = true;

    HAL_GPIO_WritePin(MOTOR_IN1_GPIO_Port, MOTOR_IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_GPIO_Port, MOTOR_IN2_Pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef status = HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    if (status == HAL_OK)
    {
        ACC_Motor_ApplyDutyLocked(ACC_DUTY_STOP_PCT);
    }
    return status;
}

void ACC_Motor_SetTarget(ACC_Command_t cmd)
{
    uint8_t target;

    /* STOP and SENSOR_FAULT bypass the ramp entirely -- immediate zero,
     * per PRD §3.4 ("no ramp on the way to stopping"). */
    if ((cmd == ACC_CMD_STOP) || (cmd == ACC_CMD_SENSOR_FAULT))
    {
        ACC_Motor_ForceStop();
        return;
    }

    switch (cmd)
    {
        case ACC_CMD_DECELERATE: target = ACC_DUTY_DECELERATE_PCT; break;
        case ACC_CMD_MAINTAIN:   target = ACC_DUTY_MAINTAIN_PCT;   break;
        case ACC_CMD_ACCELERATE: target = ACC_DUTY_ACCELERATE_PCT; break;
        default:                 target = ACC_DUTY_STOP_PCT;       break; /* defensive */
    }

    if (xSemaphoreTake(s_motor_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_motor_state.target_duty_pct = target;
        s_motor_state.forward         = true;
        xSemaphoreGive(s_motor_mutex);
    }
}

void ACC_Motor_Update(void)
{
    if (xSemaphoreTake(s_motor_mutex, portMAX_DELAY) == pdTRUE)
    {
        int16_t current = (int16_t)s_motor_state.current_duty_pct;
        int16_t target  = (int16_t)s_motor_state.target_duty_pct;
        int16_t diff     = target - current;

        if (diff > 0)
        {
            current += (diff > (int16_t)ACC_MOTOR_RAMP_STEP_PCT) ? (int16_t)ACC_MOTOR_RAMP_STEP_PCT : diff;
        }
        else if (diff < 0)
        {
            current += (diff < -(int16_t)ACC_MOTOR_RAMP_STEP_PCT) ? -(int16_t)ACC_MOTOR_RAMP_STEP_PCT : diff;
        }

        s_motor_state.current_duty_pct = (uint8_t)current;
        ACC_Motor_ApplyDutyLocked(s_motor_state.current_duty_pct);

        xSemaphoreGive(s_motor_mutex);
    }
}

void ACC_Motor_ForceStop(void)
{
    /* Short, bounded wait rather than portMAX_DELAY: this can be called
     * from the fail-safe path and must never hang the caller. */
    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        s_motor_state.current_duty_pct = ACC_DUTY_STOP_PCT;
        s_motor_state.target_duty_pct  = ACC_DUTY_STOP_PCT;
        ACC_Motor_ApplyDutyLocked(ACC_DUTY_STOP_PCT);
        xSemaphoreGive(s_motor_mutex);
    }
}

void ACC_Motor_GetStatus(uint8_t *out_duty_pct, bool *out_forward)
{
    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        *out_duty_pct = s_motor_state.current_duty_pct;
        *out_forward  = s_motor_state.forward && (s_motor_state.current_duty_pct > 0U);
        xSemaphoreGive(s_motor_mutex);
    }
    else
    {
        *out_duty_pct = 0U;
        *out_forward  = false;
    }
}
