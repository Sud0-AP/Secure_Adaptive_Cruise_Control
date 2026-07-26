/**
 ******************************************************************************
 * @file    app_tasks.c
 * @brief   Task definitions, queues, and fail-safe orchestration for the
 *          Actuator Node (PRD §3.4, §3.5).
 ******************************************************************************
 */
#include "app_tasks.h"
#include "can_driver.h"
#include "motor_driver.h"
#include "oled_display.h"

#include "main.h" /* GPIO port/pin macros: MOTOR_IN1/IN2, ACC_STATUS_LED */
#include "iwdg.h" /* CubeMX-generated IWDG handle (hiwdg) */
#include "i2c.h"  /* CubeMX-generated I2C1 handle (hi2c1) */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>

/* ---------------------------------------------------------------------- */
/* Shared state                                                           */
/* ---------------------------------------------------------------------- */
QueueHandle_t xRxFrameQueue   = NULL;
QueueHandle_t xSecFrameQueue  = NULL;
QueueHandle_t xCommandQueue   = NULL;
QueueHandle_t xDisplayMailbox = NULL;

static ACC_SecurityState_t s_security_state;

/*! Tick of the last successfully validated frame. Single writer
 *  (SecurityValidateTask), read by SafetyMonitorTask. 32-bit aligned
 *  read/modify on Cortex-M is atomic enough for this "is it stale" check. */
static volatile TickType_t s_last_valid_frame_tick = 0;

/*! Single writer (SafetyMonitorTask), read by MotorControlTask/LedStatusTask. */
static volatile bool s_link_ok            = false;
static volatile bool s_security_fault_now = false;

static u8g2_t s_u8g2;

/* ---------------------------------------------------------------------- */
/* Display mailbox helpers -- read-modify-write against the "latest        */
/* value" mailbox so each writer only updates the fields it owns.         */
/* ---------------------------------------------------------------------- */
static void ACC_Display_UpdateSnapshot_ApplyField(void (*apply)(ACC_DisplaySnapshot_t *, const void *),
                                                   const void *value)
{
    ACC_DisplaySnapshot_t snap;

    if (xQueuePeek(xDisplayMailbox, &snap, 0) != pdPASS)
    {
        memset(&snap, 0, sizeof(snap));
    }
    apply(&snap, value);
    (void)xQueueOverwrite(xDisplayMailbox, &snap);
}

static void ACC_ApplyValidated(ACC_DisplaySnapshot_t *snap, const void *value)
{
    const ACC_DisplaySnapshot_t *src = (const ACC_DisplaySnapshot_t *)value;
    snap->last_command = src->last_command;
    snap->distance_cm  = src->distance_cm;
    snap->rx_counter   = src->rx_counter;
}

static void ACC_ApplyMotor(ACC_DisplaySnapshot_t *snap, const void *value)
{
    const ACC_DisplaySnapshot_t *src = (const ACC_DisplaySnapshot_t *)value;
    snap->motor_duty_pct = src->motor_duty_pct;
    snap->motor_forward  = src->motor_forward;
}

static void ACC_ApplyLink(ACC_DisplaySnapshot_t *snap, const void *value)
{
    snap->link_ok = *(const bool *)value;
}

static void ACC_ApplySecFault(ACC_DisplaySnapshot_t *snap, const void *value)
{
    snap->security_fault = *(const bool *)value;
}

/* ---------------------------------------------------------------------- */
/* CanRxTask -- highest priority, drains ISR-fed raw frames, hands         */
/* well-formed ones to SecurityValidateTask.                              */
/* ---------------------------------------------------------------------- */
static void ACC_CanRxTask(void *pv)
{
    (void)pv;
    ACC_RawFrame_t raw;

    for (;;)
    {
        if (xQueueReceive(xRxFrameQueue, &raw, portMAX_DELAY) == pdPASS)
        {
            if ((raw.id == ACC_CAN_ID_DATA) && (raw.dlc == ACC_CAN_DLC))
            {
                ACC_CanFrame_t frame;
                memcpy(&frame, raw.data, sizeof(frame));
                /* Non-blocking send: if SecurityValidateTask is briefly
                 * behind, dropping under sustained overload is preferable
                 * to backing up the ISR-drain task. */
                (void)xQueueSend(xSecFrameQueue, &frame, 0);
            }
            /* Other IDs are already excluded by the CAN1 filter; a DLC
             * mismatch here means a malformed frame -- silently dropped. */
        }
    }
}

/* ---------------------------------------------------------------------- */
/* SecurityValidateTask -- CRC8 -> CMAC -> counter freshness.             */
/* ---------------------------------------------------------------------- */
static void ACC_SecurityValidateTask(void *pv)
{
    (void)pv;
    ACC_CanFrame_t frame;

    for (;;)
    {
        if (xQueueReceive(xSecFrameQueue, &frame, portMAX_DELAY) == pdPASS)
        {
            ACC_Command_t cmd;
            uint8_t distance;
            uint8_t counter;

            if (ACC_Security_ValidateFrame(&s_security_state, &frame, &cmd, &distance, &counter))
            {
                s_last_valid_frame_tick = xTaskGetTickCount();

                (void)xQueueSend(xCommandQueue, &cmd, 0);

                ACC_DisplaySnapshot_t src = {0};
                src.last_command = cmd;
                src.distance_cm  = distance;
                src.rx_counter   = counter;
                ACC_Display_UpdateSnapshot_ApplyField(ACC_ApplyValidated, &src);
            }
            /* On failure, fault counters are already incremented inside
             * ACC_Security_ValidateFrame(); SafetyMonitorTask polls them. */
        }
    }
}

/* ---------------------------------------------------------------------- */
/* MotorControlTask -- maps validated commands to ramped PWM duty.        */
/* ---------------------------------------------------------------------- */
static void ACC_MotorControlTask(void *pv)
{
    (void)pv;
    ACC_Command_t cmd;

    for (;;)
    {
        if (s_link_ok)
        {
            if (xQueueReceive(xCommandQueue, &cmd, pdMS_TO_TICKS(ACC_MOTOR_CONTROL_PERIOD_MS)) == pdPASS)
            {
                ACC_Motor_SetTarget(cmd);
            }
            ACC_Motor_Update();
        }
        else
        {
            /* Fail-safe active (SafetyMonitorTask already forced a stop):
             * hold at zero and don't let a stale target ramp back up. */
            ACC_Motor_ForceStop();
            vTaskDelay(pdMS_TO_TICKS(ACC_MOTOR_CONTROL_PERIOD_MS));
        }

        ACC_DisplaySnapshot_t src = {0};
        ACC_Motor_GetStatus(&src.motor_duty_pct, &src.motor_forward);
        ACC_Display_UpdateSnapshot_ApplyField(ACC_ApplyMotor, &src);
    }
}

/* ---------------------------------------------------------------------- */
/* SafetyMonitorTask -- fail-safe watchdog + IWDG refresh (PRD §3.5).     */
/* ---------------------------------------------------------------------- */
static void ACC_SafetyMonitorTask(void *pv)
{
    (void)pv;
    uint32_t prev_crc_faults = 0U;
    uint32_t prev_mac_faults = 0U;
    uint32_t prev_replay_faults = 0U;
    TickType_t fault_last_seen_tick = 0;

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        bool link_ok = (now - s_last_valid_frame_tick) <= pdMS_TO_TICKS(ACC_LINK_TIMEOUT_MS);

        if (!link_ok)
        {
            /* Covers every §3.5 fail-safe trigger at once: no frames,
             * CRC-failing frames, MAC-failing frames, or stale/replayed
             * counters all show up as "no valid frame recently". */
            ACC_Motor_ForceStop();
        }
        s_link_ok = link_ok;
        ACC_Display_UpdateSnapshot_ApplyField(ACC_ApplyLink, &link_ok);

        /* Security-fault LED/OLED indicator: latch "fault seen" for a
         * short hold window whenever a fault counter has moved, so a
         * brief blip stays visible rather than flickering for one cycle. */
        bool fault_now = (s_security_state.crc_fault_count != prev_crc_faults) ||
                          (s_security_state.mac_fault_count != prev_mac_faults) ||
                          (s_security_state.replay_fault_count != prev_replay_faults);
        prev_crc_faults    = s_security_state.crc_fault_count;
        prev_mac_faults    = s_security_state.mac_fault_count;
        prev_replay_faults = s_security_state.replay_fault_count;

        if (fault_now)
        {
            fault_last_seen_tick = now;
        }
        bool fault_active = (now - fault_last_seen_tick) <= pdMS_TO_TICKS(ACC_SEC_FAULT_HOLD_MS);
        s_security_fault_now = fault_active;
        ACC_Display_UpdateSnapshot_ApplyField(ACC_ApplySecFault, &fault_active);

        HAL_IWDG_Refresh(&hiwdg);

        vTaskDelay(pdMS_TO_TICKS(ACC_SAFETY_TASK_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- */
/* LedStatusTask -- distinct blink patterns per health state.             */
/* ---------------------------------------------------------------------- */
static void ACC_LedStatusTask(void *pv)
{
    (void)pv;
    static const uint8_t pattern_healthy[]   = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* slow steady blink   */
    static const uint8_t pattern_link_lost[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0}; /* fast blink          */
    static const uint8_t pattern_sec_fault[] = {1, 0, 1, 0, 0, 0, 0, 0, 0, 0}; /* distinct double-blink */
    uint8_t idx = 0U;

    for (;;)
    {
        const uint8_t *pattern;
        uint8_t len;

        if (s_security_fault_now)
        {
            pattern = pattern_sec_fault;
            len = (uint8_t)sizeof(pattern_sec_fault);
        }
        else if (!s_link_ok)
        {
            pattern = pattern_link_lost;
            len = (uint8_t)sizeof(pattern_link_lost);
        }
        else
        {
            pattern = pattern_healthy;
            len = (uint8_t)sizeof(pattern_healthy);
        }

        HAL_GPIO_WritePin(ACC_STATUS_LED_GPIO_Port, ACC_STATUS_LED_Pin,
                           (pattern[idx % len] != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        idx = (uint8_t)((idx + 1U) % len);

        vTaskDelay(pdMS_TO_TICKS(ACC_LED_TASK_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- */
/* DisplayTask -- lowest priority, redraws OLED from the mailbox.         */
/* ---------------------------------------------------------------------- */
static void ACC_DisplayTask(void *pv)
{
    (void)pv;
    ACC_Display_Init(&s_u8g2, &hi2c1);

    for (;;)
    {
        ACC_DisplaySnapshot_t snap;
        if (xQueuePeek(xDisplayMailbox, &snap, 0) == pdPASS)
        {
            ACC_Display_Draw(&s_u8g2, &snap);
        }
        vTaskDelay(pdMS_TO_TICKS(ACC_DISPLAY_TASK_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- */
/* Public init                                                            */
/* ---------------------------------------------------------------------- */
void ACC_Tasks_Init(void)
{
    ACC_Security_Init(&s_security_state);

    xRxFrameQueue   = xQueueCreate(ACC_RX_FRAME_QUEUE_DEPTH, sizeof(ACC_RawFrame_t));
    xSecFrameQueue  = xQueueCreate(ACC_SEC_FRAME_QUEUE_DEPTH, sizeof(ACC_CanFrame_t));
    xCommandQueue   = xQueueCreate(ACC_COMMAND_QUEUE_DEPTH, sizeof(ACC_Command_t));
    xDisplayMailbox = xQueueCreate(1, sizeof(ACC_DisplaySnapshot_t));

    configASSERT(xRxFrameQueue != NULL);
    configASSERT(xSecFrameQueue != NULL);
    configASSERT(xCommandQueue != NULL);
    configASSERT(xDisplayMailbox != NULL);

    ACC_DisplaySnapshot_t initial_snap = {0};
    initial_snap.last_command = ACC_CMD_STOP;
    (void)xQueueOverwrite(xDisplayMailbox, &initial_snap);

    (void)xTaskCreate(ACC_CanRxTask, "CanRxTask", ACC_TASK_STACK_CAN_RX, NULL,
                       ACC_TASK_PRIO_CAN_RX, NULL);
    (void)xTaskCreate(ACC_SecurityValidateTask, "SecValidate", ACC_TASK_STACK_SECURITY, NULL,
                       ACC_TASK_PRIO_SECURITY, NULL);
    (void)xTaskCreate(ACC_MotorControlTask, "MotorCtrl", ACC_TASK_STACK_MOTOR, NULL,
                       ACC_TASK_PRIO_MOTOR, NULL);
    (void)xTaskCreate(ACC_SafetyMonitorTask, "SafetyMon", ACC_TASK_STACK_SAFETY, NULL,
                       ACC_TASK_PRIO_SAFETY, NULL);
    (void)xTaskCreate(ACC_LedStatusTask, "LedStatus", ACC_TASK_STACK_LED, NULL,
                       ACC_TASK_PRIO_LED, NULL);
    (void)xTaskCreate(ACC_DisplayTask, "Display", ACC_TASK_STACK_DISPLAY, NULL,
                       ACC_TASK_PRIO_DISPLAY, NULL);
}

/* ---------------------------------------------------------------------- */
/* FreeRTOS hooks (enabled via the 3 FreeRTOSConfig.h edits, see README)  */
/* ---------------------------------------------------------------------- */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* This board's whole job is fail-safe correctness -- if a task's stack
     * has overflowed we can no longer trust any of its state, so force the
     * motor off directly (bypassing the mutex, since task state is already
     * suspect) and spin flashing the LED until the IWDG resets us. */
    __disable_irq();
    HAL_GPIO_WritePin(MOTOR_IN1_GPIO_Port, MOTOR_IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_GPIO_Port, MOTOR_IN2_Pin, GPIO_PIN_RESET);
    for (;;)
    {
        HAL_GPIO_TogglePin(ACC_STATUS_LED_GPIO_Port, ACC_STATUS_LED_Pin);
        for (volatile uint32_t i = 0; i < 200000U; i++) { }
    }
}

void vApplicationMallocFailedHook(void)
{
    /* Queue/semaphore creation failing at startup is a build-config bug,
     * not a runtime condition to recover from -- fail loudly and stop. */
    __disable_irq();
    for (;;)
    {
        HAL_GPIO_TogglePin(ACC_STATUS_LED_GPIO_Port, ACC_STATUS_LED_Pin);
        for (volatile uint32_t i = 0; i < 50000U; i++) { }
    }
}
