/**
 ******************************************************************************
 * @file    can_driver.h
 * @brief   CAN1 init (filter for ACC IDs) and ISR-deferred reception.
 *
 * The RX0 interrupt callback here does the absolute minimum: read the
 * message out of the peripheral FIFO and push the raw bytes onto a queue.
 * All parsing and crypto happens later, in task context (CanRxTask /
 * SecurityValidateTask), per PRD §3.4 and Definition-of-Done.
 ******************************************************************************
 */
#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include "can.h"           /* CubeMX-generated CAN1 handle (hcan1) */
#include "can_security.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Raw CAN frame as handed from ISR to CanRxTask via xRxFrameQueue.
 *        Deliberately untyped/unparsed -- parsing happens in task context.
 */
typedef struct
{
    uint32_t id;              /*!< Standard 11-bit CAN ID          */
    uint8_t  dlc;              /*!< Data length code, 0-8           */
    uint8_t  data[ACC_CAN_DLC]; /*!< Raw payload bytes               */
} ACC_RawFrame_t;

/**
 * @brief Configure the CAN1 RX filter for the ACC data/heartbeat IDs,
 *        activate the RX0 "message pending" notification, and start CAN1.
 * @param hcan Pointer to the CubeMX-generated CAN1 handle.
 * @return HAL_OK on success, propagated HAL error otherwise.
 */
HAL_StatusTypeDef ACC_CanDriver_Init(CAN_HandleTypeDef *hcan);

#endif /* CAN_DRIVER_H */
