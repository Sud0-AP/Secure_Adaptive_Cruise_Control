/**
 * @file    can_driver.c
 * @brief   See can_driver.h.
 */
#include "can_driver.h"
#include "can_security.h"
#include "security_config.h"
#include <string.h>

extern CAN_HandleTypeDef hcan1; /**< Generated and initialised by CubeMX in main.c */

/** Single-writer rolling counter (PRD §2.5): only CANDRV_SendFrame() ever
 *  touches this, and it is only ever called from CanTxTask, so no mutex
 *  is required. */
static uint8_t s_tx_counter = 0U;

HAL_StatusTypeDef CANDRV_Init(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef filter = {0};

    /* Accept-all filter on bank 0, mask mode, so both 0x100 (this board's
     * own echo, if any loopback config were enabled) and the reserved
     * 0x101 heartbeat pass through if RX0 is ever used later. */
    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000U;
    filter.FilterIdLow = 0x0000U;
    filter.FilterMaskIdHigh = 0x0000U;
    filter.FilterMaskIdLow = 0x0000U;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;

    if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_CAN_Start(hcan) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef CANDRV_SendFrame(ACC_Command_t command, uint8_t distance_cm)
{
    ACC_CanFrame_t frame;
    frame.command = (uint8_t)command;
    frame.distance_cm = distance_cm;
    frame.counter = s_tx_counter;

    /* CRC8 over bytes [command, distance_cm, counter] -- PRD §2.3 */
    frame.crc8 = CANSEC_Crc8((const uint8_t *)&frame, 3U);

    /* Truncated CMAC over bytes [command, distance_cm, counter, crc8] -- PRD §2.4 */
    const uint8_t mac_input[4] = {frame.command, frame.distance_cm, frame.counter, frame.crc8};
    CANSEC_ComputeTruncatedCmac(ACC_PSK, mac_input, frame.mac);

    /* Counter increments once per transmit attempt, success or not, and
     * never gets reused -- PRD §2.5 mandates this even for retries. */
    s_tx_counter++; /* uint8_t wraps 255 -> 0 naturally */

    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)
    {
        return HAL_BUSY; /* all 3 mailboxes full; caller/HealthMonitorTask observes this via return value */
    }

    CAN_TxHeaderTypeDef header;
    header.StdId = ACC_CAN_ID_DATA_FRAME;
    header.ExtId = 0U;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = ACC_CAN_DLC;
    header.TransmitGlobalTime = DISABLE;

    uint32_t tx_mailbox;
    return HAL_CAN_AddTxMessage(&hcan1, &header, (uint8_t *)&frame, &tx_mailbox);
}

uint8_t CANDRV_GetLastCounter(void)
{
    /* s_tx_counter has already been incremented past the value used in the
     * last frame, so report counter-1 (with wraparound) to reflect what
     * was actually on the wire. */
    return (uint8_t)(s_tx_counter - 1U);
}
