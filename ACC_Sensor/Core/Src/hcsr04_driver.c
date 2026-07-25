/**
 * @file    hcsr04_driver.c
 * @brief   See hcsr04_driver.h.
 */
#include "hcsr04_driver.h"
#include "app_tasks.h" /* for xEchoCaptureSem, owned centrally per PRD §3.4 */
#include "FreeRTOS.h"
#include "semphr.h"

/** Echo state machine: both-edges IC mode delivers rising and falling
 *  captures through the same callback, so we must track which edge we're
 *  expecting next and read the pin level to disambiguate them (PRD's
 *  "both edges" wiring change from the original rising-only default). */
typedef enum
{
    HCSR04_WAIT_RISING = 0,
    HCSR04_WAIT_FALLING
} hcsr04_echo_state_t;

static volatile hcsr04_echo_state_t s_echo_state = HCSR04_WAIT_RISING;
static volatile uint32_t s_t_rise_us = 0U;
static volatile uint32_t s_last_pulse_width_us = 0U;

static TIM_HandleTypeDef *s_htim = NULL;

/**
 * @brief  Busy-wait a precise number of microseconds using the Cortex-M4
 *         DWT cycle counter. HAL_Delay() only has millisecond resolution,
 *         and the HC-SR04 trigger pulse needs a tight ~10us pulse, so a
 *         calibrated busy-loop is the right tool here (this briefly blocks
 *         UltrasonicCaptureTask itself, not any other task, since it's the
 *         highest-priority task in the system).
 */
static void hcsr04_delay_us(uint32_t us)
{
    const uint32_t cycles = (SystemCoreClock / 1000000U) * us;
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles)
    {
        /* busy wait */
    }
}

void HCSR04_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /* Enable the DWT cycle counter for hcsr04_delay_us(). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_RESET);

    HAL_TIM_IC_Start_IT(s_htim, TIM_CHANNEL_2);
}

bool HCSR04_MeasureDistanceCm(uint16_t *out_distance_cm, uint32_t timeout_ticks)
{
    /* Reset the state machine before each trigger so a stale/partial
     * capture from a prior timed-out cycle can't be misread as this
     * cycle's result. */
    s_echo_state = HCSR04_WAIT_RISING;
    (void)xSemaphoreTake(xEchoCaptureSem, 0U); /* drain any stale give */

    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_SET);
    hcsr04_delay_us(HCSR04_TRIG_PULSE_US);
    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_RESET);

    if (xSemaphoreTake(xEchoCaptureSem, timeout_ticks) != pdTRUE)
    {
        return false; /* no echo within timeout -- caller counts this toward SENSOR_FAULT */
    }

    const uint32_t pulse_us = s_last_pulse_width_us;
    const uint32_t distance_cm = pulse_us / HCSR04_SOUND_US_PER_CM;

    if (distance_cm > HCSR04_MAX_PLAUSIBLE_CM)
    {
        return false; /* implausible reading (electrical noise, echo cross-talk) */
    }

    *out_distance_cm = (uint16_t)distance_cm;
    return true;
}

void HCSR04_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance != s_htim->Instance) || (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2))
    {
        return;
    }

    const uint32_t captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
    const GPIO_PinState level = HAL_GPIO_ReadPin(HCSR04_ECHO_GPIO_Port, HCSR04_ECHO_Pin);

    if (level == GPIO_PIN_SET)
    {
        /* Rising edge just captured: ECHO has gone high, pulse starts now. */
        s_t_rise_us = captured;
        s_echo_state = HCSR04_WAIT_FALLING;
    }
    else if (s_echo_state == HCSR04_WAIT_FALLING)
    {
        /* Falling edge, and we were legitimately expecting it: pulse ends now. */
        const uint32_t t_fall_us = captured;
        /* TIM2 is a 32-bit counter on the F407 (unlike TIM3/TIM4, which are
         * 16-bit) running at 1 MHz, so a wraparound would need ~71 minutes
         * of continuous counting -- unreachable within an 11ms echo pulse.
         * The subtraction below is still written to handle it correctly
         * regardless, using the full 32-bit range. */
        s_last_pulse_width_us = (t_fall_us >= s_t_rise_us)
                                     ? (t_fall_us - s_t_rise_us)
                                     : ((0xFFFFFFFFU - s_t_rise_us) + t_fall_us + 1U);
        s_echo_state = HCSR04_WAIT_RISING;

        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(xEchoCaptureSem, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
    else
    {
        /* A falling edge with no matching rising edge recorded (e.g. we
         * missed one due to a prior timeout) -- ignore it, the next
         * rising edge will resynchronise the state machine. */
    }
}
