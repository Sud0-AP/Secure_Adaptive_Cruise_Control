/**
 * @file    app_tasks.h
 * @brief   FreeRTOS task architecture for the Sensor Node (PRD §3.4):
 *          owns all inter-task synchronisation objects (queues, semaphore,
 *          display mailbox) and creates the six tasks.
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "can_driver.h" /* ACC_Command_t */
#include <stdint.h>
#include <stdbool.h>

/* --- Task priorities: higher number = higher FreeRTOS priority ---
 * Requires configMAX_PRIORITIES >= 6 (indices 0..5 used). */
#define APP_TASK_PRIO_ULTRASONIC   (tskIDLE_PRIORITY + 5U) /* highest */
#define APP_TASK_PRIO_CAN_TX       (tskIDLE_PRIORITY + 4U)
#define APP_TASK_PRIO_DIST_PROC    (tskIDLE_PRIORITY + 3U)
#define APP_TASK_PRIO_HEALTH       (tskIDLE_PRIORITY + 2U)
#define APP_TASK_PRIO_LED          (tskIDLE_PRIORITY + 1U)
#define APP_TASK_PRIO_DISPLAY      (tskIDLE_PRIORITY + 1U) /* lowest active, tied with LED */

/* --- Stack sizes, in words (PRD §3.4) --- */
#define APP_STACK_WORDS_ULTRASONIC  256U
#define APP_STACK_WORDS_DIST_PROC   256U
#define APP_STACK_WORDS_CAN_TX      384U /* CMAC needs headroom */
#define APP_STACK_WORDS_HEALTH      128U
#define APP_STACK_WORDS_LED         128U
#define APP_STACK_WORDS_DISPLAY     512U /* framebuffer + call stack headroom */

/* --- Timing constants --- */
#define APP_ULTRASONIC_PERIOD_MS    65U  /* HC-SR04 needs >=60ms between pulses */
#define APP_HEALTH_PERIOD_MS        500U
#define APP_LED_PERIOD_MS           200U
#define APP_DISPLAY_PERIOD_MS       200U
#define APP_SENSOR_STALE_MS         500U /* HealthMonitorTask: no good reading in this long -> fault */
#define APP_CAN_STALE_MS            500U /* HealthMonitorTask: no good Tx in this long -> fault */
#define APP_CONSECUTIVE_TIMEOUTS_FOR_FAULT 3U /* PRD §2.2: 3+ consecutive echo timeouts */

/* --- Status LED pin: PD13, this board's actual onboard LED (not PC13) --- */
#define APP_LED_GPIO_Port  GPIOD
#define APP_LED_Pin        GPIO_PIN_13

/** Distance sample as pushed by UltrasonicCaptureTask; HCSR04_TIMEOUT_SENTINEL
 *  marks a timed-out/implausible reading. */
#define APP_DISTANCE_TIMEOUT_SENTINEL  0xFFFFU

/** Request queued from DistanceProcessingTask to CanTxTask. */
typedef struct
{
    ACC_Command_t command;
    uint8_t distance_cm;
} ACC_TxRequest_t;

/* --- Centrally-owned synchronisation objects (PRD §3.4) --- */
extern SemaphoreHandle_t xEchoCaptureSem;  /**< given by TIM2 IC ISR, taken by UltrasonicCaptureTask */
extern QueueHandle_t xDistanceQueue;       /**< depth 4, element uint16_t (raw cm or timeout sentinel) */
extern QueueHandle_t xTxRequestQueue;      /**< depth 4, element ACC_TxRequest_t */
extern QueueHandle_t xDisplayMailbox;      /**< depth 1, "latest value" mailbox, element ACC_DisplaySnapshot_t */

/**
 * @brief  Create all synchronisation objects and all six tasks. Call once
 *         from main(), after all MX_*_Init() calls and HCSR04_Init()/
 *         CANDRV_Init()/OLED_Init(), and before vTaskStartScheduler().
 * @return true if every queue/semaphore/task was created successfully.
 */
bool APP_Tasks_Init(void);

#endif /* APP_TASKS_H */
