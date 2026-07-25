/**
 * @file    can_driver.h
 * @brief   ACC CAN frame definition, build, and transmit for the Sensor Node.
 *
 * ============================================================================
 * CAN FRAME LAYOUT (authoritative source: PRD §2, self-documented here so
 * this code stands alone without the PRD):
 *
 *   ID 0x100 (standard 11-bit), DLC 8, 500 kbps, Sensor -> Actuator
 *
 *   Byte | Name          | Meaning
 *   -----|---------------|----------------------------------------------
 *     0  | command       | ACC_Command_t enum
 *     1  | distance_cm   | Measured distance, clamped 0-200
 *     2  | counter       | 8-bit rolling monotonic counter, wraps 255->0
 *     3  | crc8          | CRC-8 (poly 0x07, init 0xFF) over bytes 0-2
 *    4-7 | mac[4]        | First 4 bytes of AES-128-CMAC over bytes 0-3
 * ============================================================================
 */
#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/** ACC command enum -- must match Board B bit-for-bit (PRD §2.2). */
typedef enum
{
    ACC_CMD_STOP         = 0x00U, /**< distance < 30 cm: emergency proximity stop */
    ACC_CMD_DECELERATE   = 0x01U, /**< 30 <= distance < 80 cm: slow down */
    ACC_CMD_MAINTAIN     = 0x02U, /**< 80 <= distance < 150 cm: hold current speed */
    ACC_CMD_ACCELERATE   = 0x03U, /**< distance >= 150 cm: accelerate toward cruise */
    ACC_CMD_SENSOR_FAULT = 0xFFU  /**< 3+ consecutive HC-SR04 echo timeouts */
} ACC_Command_t;

/** Distance thresholds, cm (PRD §2.2). */
#define ACC_DIST_STOP_MAX_CM        30U
#define ACC_DIST_DECEL_MAX_CM       80U
#define ACC_DIST_MAINTAIN_MAX_CM    150U
#define ACC_DIST_CLAMP_MAX_CM       200U /**< plank length; clamp readings above this */

/** CAN identifiers / framing (PRD §2.1, §2.6). */
#define ACC_CAN_ID_DATA_FRAME   0x100U /**< Sensor -> Actuator, this board transmits */
#define ACC_CAN_ID_HEARTBEAT    0x101U /**< Actuator -> Sensor, reserved, not implemented here */
#define ACC_CAN_DLC              8U

/** Wire-format frame, exactly as transmitted (PRD §2.1). */
typedef struct __attribute__((packed))
{
    uint8_t command;     /* Byte 0 */
    uint8_t distance_cm; /* Byte 1 */
    uint8_t counter;     /* Byte 2 */
    uint8_t crc8;         /* Byte 3 */
    uint8_t mac[4];       /* Bytes 4-7 */
} ACC_CanFrame_t;

/**
 * @brief  Initialise the CAN1 driver: configure an accept-all filter and
 *         start the peripheral. Call once after MX_CAN1_Init().
 * @param  hcan Pointer to the CAN1 handle.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef CANDRV_Init(CAN_HandleTypeDef *hcan);

/**
 * @brief  Build and transmit one ACC data frame on ID 0x100.
 *
 * Increments the internal rolling counter exactly once per call (even on
 * a transmit failure the counter must not be reused for a retry -- per
 * PRD §2.5, callers must not re-invoke this to "resend" the same reading;
 * the next scheduled sample naturally carries the next counter value).
 *
 * @param  command      ACC command to transmit.
 * @param  distance_cm  Distance in cm, already clamped to 0-200 by the caller.
 * @return HAL_OK if the frame was queued into a CAN1 Tx mailbox; HAL_BUSY
 *         if all three mailboxes are full; HAL_ERROR on a HAL-level failure.
 */
HAL_StatusTypeDef CANDRV_SendFrame(ACC_Command_t command, uint8_t distance_cm);

/**
 * @brief  Read the counter value used in the most recently transmitted
 *         frame (for the OLED "Tx#:" field). Undefined (0) before the
 *         first successful call to CANDRV_SendFrame().
 */
uint8_t CANDRV_GetLastCounter(void);

#endif /* CAN_DRIVER_H */
