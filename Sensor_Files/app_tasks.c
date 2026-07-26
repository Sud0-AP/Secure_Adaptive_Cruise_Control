/**
 * @file    app_tasks.c
 * @brief   See app_tasks.h.
 */
#include "app_tasks.h"
#include "hcsr04_driver.h"
#include "can_driver.h"
#include "oled_display.h"
#include "stm32f4xx_hal.h"
#include <string.h>

extern IWDG_HandleTypeDef hiwdg; /**< Requires IWDG enabled in CubeMX (~1s timeout), generated in main.c */

SemaphoreHandle_t xEchoCaptureSem = NULL;
QueueHandle_t xDistanceQueue = NULL;
QueueHandle_t xTxRequestQueue = NULL;
QueueHandle_t xDisplayMailbox = NULL;

/** Last-known-good timestamps, written by the producing tasks and read by
 *  HealthMonitorTask. Each field has exactly one writer, so plain volatile
 *  reads/writes are sufficient on Cortex-M (32-bit aligned access is
 *  atomic) without a mutex. */
static volatile TickType_t s_last_sensor_ok_tick = 0U;
static volatile TickType_t s_last_can_tx_ok_tick = 0U;
static volatile bool s_system_fault = false;

/**
 * @brief  Read-modify-write helper for the display mailbox: peeks the
 *         current snapshot (or zeroes it if nothing has been posted yet),
 *         applies the caller's field updates, then overwrites the single
 *         mailbox slot.
 *
 * Multiple tasks call this (DistanceProcessingTask, CanTxTask), each only
 * ever setting its own fields (see call sites) -- there is a small race
 * window between the peek and the overwrite if two producers happen to
 * interleave, but per PRD §3.4 this mailbox exists purely for a
 * best-effort diagnostic display where "stale reads are fine"; a
 * momentarily-overwritten field self-corrects on the next update from its
 * owning task, so a full mutex is deliberately not used here.
 */
static void app_display_mailbox_update(void (*apply)(ACC_DisplaySnapshot_t *snap, const void *ctx), const void *ctx)
{
    ACC_DisplaySnapshot_t snap;
    if (xQueuePeek(xDisplayMailbox, &snap, 0U) != pdTRUE)
    {
        memset(&snap, 0, sizeof(snap));
    }
    apply(&snap, ctx);
    (void)xQueueOverwrite(xDisplayMailbox, &snap);
}

/* ---------------------------------------------------------------------- *
 *  UltrasonicCaptureTask -- priority 5 (highest)
 * ---------------------------------------------------------------------- */
static void UltrasonicCaptureTask(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        uint16_t distance_cm;
        const bool ok = HCSR04_MeasureDistanceCm(&distance_cm, pdMS_TO_TICKS(HCSR04_ECHO_TIMEOUT_MS));

        const uint16_t sample = ok ? distance_cm : (uint16_t)APP_DISTANCE_TIMEOUT_SENTINEL;
        (void)xQueueSend(xDistanceQueue, &sample, 0U); /* never block the highest-priority task on a full queue */

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_ULTRASONIC_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- *
 *  DistanceProcessingTask -- priority 3
 * ---------------------------------------------------------------------- */

struct dist_update_ctx { uint16_t distance_cm; ACC_Command_t command; bool sensor_fault; };
static void dist_apply(ACC_DisplaySnapshot_t *snap, const void *ctx_v)
{
    const struct dist_update_ctx *ctx = (const struct dist_update_ctx *)ctx_v;
    snap->distance_cm = ctx->distance_cm;
    snap->command = (uint8_t)ctx->command;
    snap->sensor_fault = ctx->sensor_fault;
}

static ACC_Command_t app_classify_distance(uint16_t distance_cm)
{
    if (distance_cm < ACC_DIST_STOP_MAX_CM)       { return ACC_CMD_STOP; }
    if (distance_cm < ACC_DIST_DECEL_MAX_CM)      { return ACC_CMD_DECELERATE; }
    if (distance_cm < ACC_DIST_MAINTAIN_MAX_CM)   { return ACC_CMD_MAINTAIN; }
    return ACC_CMD_ACCELERATE;
}

static void DistanceProcessingTask(void *argument)
{
    (void)argument;

    /* Small moving-average filter over the last 3 valid samples, per PRD §3.4. */
    uint16_t history[3] = {0U, 0U, 0U};
    uint8_t history_count = 0U;
    uint8_t history_idx = 0U;
    uint8_t consecutive_timeouts = 0U;

    for (;;)
    {
        uint16_t raw_sample;
        if (xQueueReceive(xDistanceQueue, &raw_sample, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        ACC_Command_t command;
        uint16_t filtered_cm = 0U;
        bool sensor_fault = false;

        if (raw_sample == APP_DISTANCE_TIMEOUT_SENTINEL)
        {
            consecutive_timeouts++;
        }
        else
        {
            consecutive_timeouts = 0U;
            s_last_sensor_ok_tick = xTaskGetTickCount();

            uint16_t clamped = raw_sample;
            if (clamped > ACC_DIST_CLAMP_MAX_CM) { clamped = ACC_DIST_CLAMP_MAX_CM; }

            history[history_idx] = clamped;
            history_idx = (uint8_t)((history_idx + 1U) % 3U);
            if (history_count < 3U) { history_count++; }

            uint32_t sum = 0U;
            for (uint8_t i = 0U; i < history_count; i++) { sum += history[i]; }
            filtered_cm = (uint16_t)(sum / history_count);
        }

        if (consecutive_timeouts >= APP_CONSECUTIVE_TIMEOUTS_FOR_FAULT)
        {
            command = ACC_CMD_SENSOR_FAULT;
            sensor_fault = true;
        }
        else if (raw_sample == APP_DISTANCE_TIMEOUT_SENTINEL)
        {
            /* Fewer than the fault threshold: keep publishing the last known
             * good command/distance rather than injecting a bogus reading. */
            continue;
        }
        else
        {
            command = app_classify_distance(filtered_cm);
        }

        const ACC_TxRequest_t req = { .command = command, .distance_cm = (uint8_t)filtered_cm };
        (void)xQueueSend(xTxRequestQueue, &req, 0U);

        struct dist_update_ctx ctx = { .distance_cm = filtered_cm, .command = command, .sensor_fault = sensor_fault };
        app_display_mailbox_update(dist_apply, &ctx);
    }
}

/* ---------------------------------------------------------------------- *
 *  CanTxTask -- priority 4
 * ---------------------------------------------------------------------- */

struct tx_update_ctx { uint8_t tx_counter; bool can_tx_ok; };
static void tx_apply(ACC_DisplaySnapshot_t *snap, const void *ctx_v)
{
    const struct tx_update_ctx *ctx = (const struct tx_update_ctx *)ctx_v;
    snap->tx_counter = ctx->tx_counter;
    snap->can_tx_ok = ctx->can_tx_ok;
}

static void CanTxTask(void *argument)
{
    (void)argument;
    ACC_TxRequest_t req;

    for (;;)
    {
        if (xQueueReceive(xTxRequestQueue, &req, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        const HAL_StatusTypeDef status = CANDRV_SendFrame(req.command, req.distance_cm);
        const bool tx_ok = (status == HAL_OK);
        if (tx_ok)
        {
            s_last_can_tx_ok_tick = xTaskGetTickCount();
        }

        struct tx_update_ctx ctx = { .tx_counter = CANDRV_GetLastCounter(), .can_tx_ok = tx_ok };
        app_display_mailbox_update(tx_apply, &ctx);
    }
}

/* ---------------------------------------------------------------------- *
 *  HealthMonitorTask -- priority 2
 * ---------------------------------------------------------------------- */
static void HealthMonitorTask(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    /* Seed both timestamps at boot so we don't report a false fault before
     * the first sensor reading / CAN Tx has had a chance to happen. */
    s_last_sensor_ok_tick = xTaskGetTickCount();
    s_last_can_tx_ok_tick = xTaskGetTickCount();

    for (;;)
    {
        HAL_IWDG_Refresh(&hiwdg);

        const TickType_t now = xTaskGetTickCount();
        const bool sensor_stale = (now - s_last_sensor_ok_tick) > pdMS_TO_TICKS(APP_SENSOR_STALE_MS);
        const bool can_stale = (now - s_last_can_tx_ok_tick) > pdMS_TO_TICKS(APP_CAN_STALE_MS);
        s_system_fault = sensor_stale || can_stale;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_HEALTH_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- *
 *  LedStatusTask -- priority 1
 * ---------------------------------------------------------------------- */
static void LedStatusTask(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    bool led_on = false;
    uint8_t tick_count = 0U;

    for (;;)
    {
        /* Healthy: slow blink, toggle every 3 periods (600ms). Fault: fast
         * blink, toggle every period (200ms). */
        const uint8_t toggle_every = s_system_fault ? 1U : 3U;

        tick_count++;
        if (tick_count >= toggle_every)
        {
            tick_count = 0U;
            led_on = !led_on;
            HAL_GPIO_WritePin(APP_LED_GPIO_Port, APP_LED_Pin, led_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_LED_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- *
 *  DisplayTask -- priority 1 (lowest active, alongside LED)
 * ---------------------------------------------------------------------- */
static void DisplayTask(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        ACC_DisplaySnapshot_t snapshot;
        if (xQueuePeek(xDisplayMailbox, &snapshot, 0U) == pdTRUE)
        {
            OLED_RenderSnapshot(&snapshot);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_DISPLAY_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------- *
 *  Init
 * ---------------------------------------------------------------------- */
/* ---------------------------------------------------------------------- *
 *  FreeRTOS hooks
 * ---------------------------------------------------------------------- */

/**
 * @brief  Required because FreeRTOSConfig.h sets configCHECK_FOR_STACK_OVERFLOW=2:
 *         FreeRTOS calls this the instant a task's stack canary/watermark
 *         indicates an overflow. Trapping here (rather than trying to
 *         "recover") is deliberate -- a corrupted stack means the task's
 *         other memory is untrustworthy too, so continuing risks silently
 *         propagating bad data (e.g. onto the CAN bus).
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName; /* inspect in a debugger to see which task overflowed */
    __disable_irq();
    while (1)
    {
        /* trap here on stack overflow */
    }
}

bool APP_Tasks_Init(void)
{    xEchoCaptureSem = xSemaphoreCreateBinary();
    xDistanceQueue = xQueueCreate(4U, sizeof(uint16_t));
    xTxRequestQueue = xQueueCreate(4U, sizeof(ACC_TxRequest_t));
    xDisplayMailbox = xQueueCreate(1U, sizeof(ACC_DisplaySnapshot_t));

    if ((xEchoCaptureSem == NULL) || (xDistanceQueue == NULL) ||
        (xTxRequestQueue == NULL) || (xDisplayMailbox == NULL))
    {
        return false;
    }

    /* Seed the mailbox with a valid all-zero snapshot so DisplayTask's
     * first xQueuePeek() before any producer has run doesn't skip a frame. */
    ACC_DisplaySnapshot_t initial_snapshot = {0};
    (void)xQueueOverwrite(xDisplayMailbox, &initial_snapshot);

    /* xTaskCreate() returns pdPASS (1) on success, or a negative
     * errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY-style code on failure -- NOT
     * guaranteed to be 0 -- so results are compared individually rather
     * than combined with a bitwise AND, which could silently mask a
     * failure depending on the exact negative value's bit pattern. */
    bool ok = true;
    ok = ok && (xTaskCreate(UltrasonicCaptureTask, "Ultrasonic", APP_STACK_WORDS_ULTRASONIC, NULL, APP_TASK_PRIO_ULTRASONIC, NULL) == pdPASS);
    ok = ok && (xTaskCreate(DistanceProcessingTask, "DistProc", APP_STACK_WORDS_DIST_PROC, NULL, APP_TASK_PRIO_DIST_PROC, NULL) == pdPASS);
    ok = ok && (xTaskCreate(CanTxTask, "CanTx", APP_STACK_WORDS_CAN_TX, NULL, APP_TASK_PRIO_CAN_TX, NULL) == pdPASS);
    ok = ok && (xTaskCreate(HealthMonitorTask, "Health", APP_STACK_WORDS_HEALTH, NULL, APP_TASK_PRIO_HEALTH, NULL) == pdPASS);
    ok = ok && (xTaskCreate(LedStatusTask, "Led", APP_STACK_WORDS_LED, NULL, APP_TASK_PRIO_LED, NULL) == pdPASS);
    ok = ok && (xTaskCreate(DisplayTask, "Display", APP_STACK_WORDS_DISPLAY, NULL, APP_TASK_PRIO_DISPLAY, NULL) == pdPASS);

    return ok;
}
