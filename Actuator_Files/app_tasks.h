/**
 ******************************************************************************
 * @file    app_tasks.h
 * @brief   FreeRTOS task/queue architecture for the Actuator Node (PRD §3.4).
 ******************************************************************************
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "can_security.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------- */
/* Task priorities (configMAX_PRIORITIES must be >= 6, see FreeRTOSConfig */
/* edits) -- matches PRD §3.4 table.                                      */
/* ---------------------------------------------------------------------- */
#define ACC_TASK_PRIO_CAN_RX        (5) /*!< highest: deferred-interrupt drain */
#define ACC_TASK_PRIO_SECURITY      (4)
#define ACC_TASK_PRIO_MOTOR         (3)
#define ACC_TASK_PRIO_SAFETY        (2)
#define ACC_TASK_PRIO_LED           (1)
#define ACC_TASK_PRIO_DISPLAY       (1)

/* Stack sizes in words, per PRD §3.4. */
#define ACC_TASK_STACK_CAN_RX        (256)
#define ACC_TASK_STACK_SECURITY      (384)
#define ACC_TASK_STACK_MOTOR         (256)
#define ACC_TASK_STACK_SAFETY        (128)
#define ACC_TASK_STACK_LED           (128)
#define ACC_TASK_STACK_DISPLAY       (512)

/* Periods, in ms. */
#define ACC_SAFETY_TASK_PERIOD_MS   (100U)
#define ACC_LED_TASK_PERIOD_MS      (200U)
#define ACC_DISPLAY_TASK_PERIOD_MS  (200U)
#define ACC_LINK_TIMEOUT_MS         (500U) /*!< PRD §3.5 fail-safe trigger */
#define ACC_SEC_FAULT_HOLD_MS       (2000U) /*!< how long a fault stays shown on LED/OLED */

/* Queue depths, per PRD §3.4. */
#define ACC_RX_FRAME_QUEUE_DEPTH    (8U)
#define ACC_SEC_FRAME_QUEUE_DEPTH   (8U)
#define ACC_COMMAND_QUEUE_DEPTH     (4U)

/**
 * @brief Latest-value snapshot for the OLED, written by several tasks via
 *        xQueueOverwrite and read by DisplayTask via xQueuePeek (PRD §3.4
 *        "many writers, one reader wants the freshest snapshot" mailbox).
 */
typedef struct
{
    ACC_Command_t last_command;
    uint8_t        distance_cm;
    uint8_t        rx_counter;
    uint8_t        motor_duty_pct;
    bool           motor_forward;
    bool           link_ok;
    bool           security_fault;
} ACC_DisplaySnapshot_t;

/* Queue handles -- defined in app_tasks.c, used by can_driver.c (ISR) too. */
extern QueueHandle_t xRxFrameQueue;   /*!< ACC_RawFrame_t, ISR -> CanRxTask        */
extern QueueHandle_t xSecFrameQueue;  /*!< ACC_CanFrame_t, CanRxTask -> SecurityValidateTask */
extern QueueHandle_t xCommandQueue;   /*!< ACC_Command_t, SecurityValidateTask -> MotorControlTask */
extern QueueHandle_t xDisplayMailbox; /*!< ACC_DisplaySnapshot_t, depth 1 mailbox  */

/**
 * @brief Create all queues/mailbox and all six tasks, and initialize the
 *        shared security state. Call once from main() after peripheral
 *        init, before vTaskStartScheduler().
 */
void ACC_Tasks_Init(void);

#endif /* APP_TASKS_H */
