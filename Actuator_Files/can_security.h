/**
 ******************************************************************************
 * @file    can_security.h
 * @brief   ACC CAN protocol definitions + CRC8/CMAC/anti-replay verification.
 *
 * Implements PRD §2 (CAN frame spec) and §2.3-2.5 (integrity, authentication,
 * anti-replay). This module is pure logic (no HAL calls) so it can be
 * exercised in a host-side unit test if desired.
 ******************************************************************************
 */
#ifndef CAN_SECURITY_H
#define CAN_SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------------- */
/* CAN IDs / frame geometry (PRD §2.1, §2.6)                              */
/* ---------------------------------------------------------------------- */
#define ACC_CAN_ID_DATA         (0x100U)  /*!< Sensor -> Actuator, primary data */
#define ACC_CAN_ID_HEARTBEAT    (0x101U)  /*!< Actuator -> Sensor, optional     */
#define ACC_CAN_DLC             (8U)      /*!< Fixed DLC for both frame types   */

/* ---------------------------------------------------------------------- */
/* CRC8/SMBUS parameters (PRD §2.3)                                       */
/* ---------------------------------------------------------------------- */
#define ACC_CRC8_POLY            (0x07U)
#define ACC_CRC8_INIT            (0xFFU)
#define ACC_CRC8_INPUT_LEN       (3U)     /*!< bytes [command, distance, counter] */

/* ---------------------------------------------------------------------- */
/* CMAC parameters (PRD §2.4)                                             */
/* ---------------------------------------------------------------------- */
#define ACC_MAC_LEN               (4U)    /*!< truncated MAC length, bytes       */
#define ACC_MAC_INPUT_LEN         (4U)    /*!< bytes [command, distance, counter, crc8] */
#define ACC_PSK_LEN               (16U)   /*!< AES-128 key length, bytes         */

/* ---------------------------------------------------------------------- */
/* Rolling counter anti-replay parameters (PRD §2.5)                      */
/* ---------------------------------------------------------------------- */
#define ACC_COUNTER_FORWARD_WINDOW  (32U) /*!< max forward jump still "fresh"    */
/*! Sentinel meaning "no frame accepted yet" -- wider than uint8_t so it can
 *  never collide with a real 0-255 counter value. */
#define ACC_COUNTER_SENTINEL_NONE  (0xFFFFFFFFU)

/**
 * @brief ACC command enum. Must match Board A bit-for-bit (PRD §2.2).
 */
typedef enum {
    ACC_CMD_STOP         = 0x00U,  /*!< distance < 30 cm  : emergency proximity stop */
    ACC_CMD_DECELERATE   = 0x01U,  /*!< 30 <= distance < 80 cm : slow down           */
    ACC_CMD_MAINTAIN     = 0x02U,  /*!< 80 <= distance < 150 cm : hold current speed */
    ACC_CMD_ACCELERATE   = 0x03U,  /*!< distance >= 150 cm : accelerate toward cruise*/
    ACC_CMD_SENSOR_FAULT = 0xFFU   /*!< Sensor board reporting echo timeouts         */
} ACC_Command_t;

/**
 * @brief Wire layout of the primary ACC data frame (PRD §2.1). Packed so a
 *        received 8-byte CAN payload can be reinterpreted directly.
 */
typedef struct __attribute__((packed)) {
    uint8_t command;        /*!< Byte 0 - ACC_Command_t                 */
    uint8_t distance_cm;    /*!< Byte 1 - 0-200, clamped to plank length */
    uint8_t counter;        /*!< Byte 2 - rolling monotonic, wraps 255->0*/
    uint8_t crc8;           /*!< Byte 3 - CRC-8/SMBUS over bytes 0-2      */
    uint8_t mac[ACC_MAC_LEN]; /*!< Bytes 4-7 - truncated CMAC over bytes 0-3 */
} ACC_CanFrame_t;

/**
 * @brief Persistent security/validation state, owned by SecurityValidateTask.
 */
typedef struct {
    uint32_t last_accepted_counter; /*!< ACC_COUNTER_SENTINEL_NONE = none yet */
    uint32_t crc_fault_count;       /*!< diagnostics: CRC mismatches          */
    uint32_t mac_fault_count;       /*!< diagnostics: CMAC mismatches         */
    uint32_t replay_fault_count;    /*!< diagnostics: stale/replayed counters */
} ACC_SecurityState_t;

/**
 * @brief Initialize security state to "no frame accepted yet".
 * @param state State object to initialize.
 */
void ACC_Security_Init(ACC_SecurityState_t *state);

/**
 * @brief Compute CRC-8/SMBUS (poly 0x07, init 0xFF, no reflection).
 * @param data Pointer to input bytes.
 * @param len  Number of bytes to process.
 * @return     Computed 8-bit CRC.
 */
uint8_t ACC_CRC8_Compute(const uint8_t *data, size_t len);

/**
 * @brief Recompute AES-128-CMAC over 4 input bytes and constant-time-compare
 *        the first ACC_MAC_LEN bytes against a received truncated MAC.
 * @param data4        Pointer to the 4 bytes [command, distance, counter, crc8].
 * @param received_mac Pointer to the 4 received MAC bytes.
 * @return true if the MAC matches, false otherwise (or on internal crypto error).
 */
bool ACC_MAC_Verify(const uint8_t *data4, const uint8_t *received_mac);

/**
 * @brief Check whether a received counter is "newer" than the last accepted
 *        one, accounting for 8-bit wraparound, within a forward window.
 * @param state   Current security state (read-only).
 * @param counter Received counter value.
 * @return true if the frame should be considered fresh.
 */
bool ACC_Counter_IsFresh(const ACC_SecurityState_t *state, uint8_t counter);

/**
 * @brief Record a counter value as accepted. Call only after CRC+MAC pass.
 * @param state   Security state to update.
 * @param counter Counter value to record.
 */
void ACC_Counter_Accept(ACC_SecurityState_t *state, uint8_t counter);

/**
 * @brief Full validation pipeline for a received frame: CRC8 -> CMAC ->
 *        counter freshness, in that order (PRD §2.3-2.5). Updates fault
 *        counters on any failure and updates last_accepted_counter only
 *        when every check passes.
 * @param state        Security state (updated on success or failure).
 * @param frame        Received frame to validate.
 * @param[out] out_cmd      Validated command (valid only if return is true).
 * @param[out] out_distance Validated distance_cm (valid only if return is true).
 * @param[out] out_counter  Validated counter (valid only if return is true).
 * @return true if the frame is authentic and fresh; false if it must be
 *         discarded.
 */
bool ACC_Security_ValidateFrame(ACC_SecurityState_t *state,
                                 const ACC_CanFrame_t *frame,
                                 ACC_Command_t *out_cmd,
                                 uint8_t *out_distance,
                                 uint8_t *out_counter);

#endif /* CAN_SECURITY_H */
