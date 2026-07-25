/**
 * @file    can_security.h
 * @brief   CRC-8/SMBUS integrity check and truncated AES-128-CMAC (RFC 4493)
 *          authentication for the ACC CAN frame (SecOC-style).
 *
 * This is a self-contained, dependency-free implementation (no mbedTLS
 * required) per PRD §2.4's approved fallback: "tiny-AES-c + a hand-rolled
 * CMAC wrapper is an acceptable substitute". The AES-128 core here is a
 * standard FIPS-197 implementation; the CMAC layer implements RFC 4493
 * specialised for the fixed 4-byte message this protocol always MACs
 * (bytes [command, distance_cm, counter, crc8]), which keeps the code
 * short and removes an entire class of variable-length-message bugs.
 *
 * Truncation point and byte order match PRD §2.4 exactly: first 4 bytes
 * (32 bits) of the 128-bit CMAC tag, in the order AES-CMAC produces them.
 */
#ifndef CAN_SECURITY_H
#define CAN_SECURITY_H

#include <stdint.h>
#include <stddef.h>

#define CANSEC_AES_KEY_LEN     16U   /**< AES-128 key length in bytes */
#define CANSEC_AES_BLOCK_LEN   16U   /**< AES block size in bytes */
#define CANSEC_CMAC_TRUNC_LEN  4U    /**< Truncated MAC length per PRD §2.4 */
#define CANSEC_MACINPUT_LEN    4U    /**< [command, distance_cm, counter, crc8] */

/**
 * @brief  Compute CRC-8/SMBUS (poly 0x07, init 0xFF, no reflection) over
 *         a buffer.
 * @param  data Pointer to input bytes.
 * @param  len  Number of bytes.
 * @return CRC-8 value.
 */
uint8_t CANSEC_Crc8(const uint8_t *data, size_t len);

/**
 * @brief  Compute the truncated AES-128-CMAC (first 4 bytes of the 128-bit
 *         RFC 4493 tag) over a fixed 4-byte message.
 * @param  key       16-byte AES-128 key (the shared PSK).
 * @param  msg4      Exactly CANSEC_MACINPUT_LEN (4) input bytes:
 *                    [command, distance_cm, counter, crc8].
 * @param  out_mac4  Output buffer, at least CANSEC_CMAC_TRUNC_LEN (4) bytes.
 */
void CANSEC_ComputeTruncatedCmac(const uint8_t key[CANSEC_AES_KEY_LEN],
                                  const uint8_t msg4[CANSEC_MACINPUT_LEN],
                                  uint8_t out_mac4[CANSEC_CMAC_TRUNC_LEN]);

#endif /* CAN_SECURITY_H */
