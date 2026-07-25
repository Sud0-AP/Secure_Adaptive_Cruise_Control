/**
 * @file    hcsr04_driver.h
 * @brief   HC-SR04 ultrasonic distance driver using TIM2 input capture
 *          (both-edges mode) for microsecond-accurate ECHO pulse timing.
 */
#ifndef HCSR04_DRIVER_H
#define HCSR04_DRIVER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define HCSR04_TRIG_GPIO_Port          GPIOA
#define HCSR04_TRIG_Pin                GPIO_PIN_0

#define HCSR04_ECHO_GPIO_Port          GPIOA
#define HCSR04_ECHO_Pin                GPIO_PIN_1

#define HCSR04_TRIG_PULSE_US           10U   /**< HC-SR04 datasheet minimum trigger pulse */
#define HCSR04_ECHO_TIMEOUT_MS         30U   /**< generous margin over the ~11.6ms round trip at 200cm */
#define HCSR04_SOUND_US_PER_CM         58U   /**< round-trip time constant: distance_cm = pulse_us / 58 */
#define HCSR04_MAX_PLAUSIBLE_CM        500U  /**< sanity ceiling before app-level 0-200 clamp; catches noise spikes */

/**
 * @brief  Initialise the driver: enables the DWT cycle counter (for the
 *         microsecond trigger-pulse delay) and starts TIM2 channel 2
 *         input capture in interrupt mode.
 * @param  htim Pointer to the TIM2 handle (channel 2, both-edges IC mode,
 *              already configured by CubeMX to count at 1 MHz).
 */
void HCSR04_Init(TIM_HandleTypeDef *htim);

/**
 * @brief  Fire one trigger pulse and block (via semaphore) until the ECHO
 *         pulse width has been captured, then convert it to centimeters.
 *
 * Must be called only from UltrasonicCaptureTask -- this function blocks
 * the calling task, so it is not safe to call from an ISR or from more
 * than one task concurrently (single-owner by design, per PRD §3.4).
 *
 * @param  out_distance_cm Output: measured distance in cm, unclamped
 *                          beyond HCSR04_MAX_PLAUSIBLE_CM sanity checking.
 * @param  timeout_ticks   FreeRTOS ticks to wait for the echo before
 *                          giving up (pass pdMS_TO_TICKS(HCSR04_ECHO_TIMEOUT_MS)).
 * @return true if a plausible reading was captured, false on timeout or
 *         an implausible (noise) pulse width.
 */
bool HCSR04_MeasureDistanceCm(uint16_t *out_distance_cm, uint32_t timeout_ticks);

/**
 * @brief  Call this from HAL_TIM_IC_CaptureCallback() in stm32f4xx_it.c
 *         (or wherever the project's ISR trampoline lives) for every TIM2
 *         channel-2 capture event.
 * @param  htim Pointer to the TIM2 handle, as passed by the HAL callback.
 */
void HCSR04_IC_CaptureCallback(TIM_HandleTypeDef *htim);

#endif /* HCSR04_DRIVER_H */
