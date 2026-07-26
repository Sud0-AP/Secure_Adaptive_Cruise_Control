/**
 ******************************************************************************
 * @file    can_driver.c
 * @brief   CAN1 init and ISR-deferred reception implementation.
 ******************************************************************************
 */
#include "can_driver.h"
#include "app_tasks.h"   /* xRxFrameQueue */

#include "FreeRTOS.h"
#include "queue.h"

/**
 * @brief Configure an explicit ID filter (PRD §3.2 step 4) rather than
 *        accepting all traffic -- both better practice and less ISR load.
 */
static HAL_StatusTypeDef ACC_CanDriver_ConfigFilter(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef filter_config = {0};

    /* Two-ID list filter (0x100 data frame, 0x101 heartbeat), 32-bit scale,
     * standard identifiers left-aligned per CAN filter register layout. */
    filter_config.FilterIdHigh         = (uint16_t)(ACC_CAN_ID_DATA << 5U);
    filter_config.FilterIdLow          = 0x0000U;
    filter_config.FilterMaskIdHigh     = (uint16_t)(ACC_CAN_ID_HEARTBEAT << 5U);
    filter_config.FilterMaskIdLow      = 0x0000U;
    filter_config.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter_config.FilterBank           = 0U;
    filter_config.FilterMode           = CAN_FILTERMODE_IDLIST;
    filter_config.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter_config.FilterActivation     = CAN_FILTER_ENABLE;
    filter_config.SlaveStartFilterBank = 14U; /* default split, CAN2 unused here */

    return HAL_CAN_ConfigFilter(hcan, &filter_config);
}

HAL_StatusTypeDef ACC_CanDriver_Init(CAN_HandleTypeDef *hcan)
{
    HAL_StatusTypeDef status = ACC_CanDriver_ConfigFilter(hcan);
    if (status != HAL_OK)
    {
        return status;
    }

    status = HAL_CAN_Start(hcan);
    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
}

/**
 * @brief HAL weak callback override -- fires in interrupt context whenever
 *        a message lands in RX FIFO0. Reads it out and defers everything
 *        else to CanRxTask via xRxFrameQueue. Never call blocking/parsing
 *        code here.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    ACC_RawFrame_t raw_frame;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, raw_frame.data) != HAL_OK)
    {
        return; /* Nothing usable to hand off. */
    }

    raw_frame.id  = rx_header.StdId;
    raw_frame.dlc = (uint8_t)rx_header.DLC;

    /* From-ISR variant only, per PRD code-quality requirement. Queue is
     * sized (depth 8) to absorb bursts faster than CanRxTask drains it;
     * if it's ever truly full we drop the frame rather than block the ISR. */
    (void)xQueueSendFromISR(xRxFrameQueue, &raw_frame, &higher_priority_task_woken);

    portYIELD_FROM_ISR(higher_priority_task_woken);
}
