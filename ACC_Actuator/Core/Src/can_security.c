/**
 ******************************************************************************
 * @file    can_security.c
 * @brief   ACC CAN frame validation: CRC8 integrity, AES-128-CMAC
 *          authentication, rolling-counter anti-replay.
 ******************************************************************************
 */
#include "can_security.h"
#include "security_config.h"

#include "mbedtls/cipher.h"
#include "mbedtls/cmac.h"

void ACC_Security_Init(ACC_SecurityState_t *state)
{
    state->last_accepted_counter = ACC_COUNTER_SENTINEL_NONE;
    state->crc_fault_count       = 0U;
    state->mac_fault_count       = 0U;
    state->replay_fault_count    = 0U;
}

uint8_t ACC_CRC8_Compute(const uint8_t *data, size_t len)
{
    uint8_t crc = ACC_CRC8_INIT;

    for (size_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1) ^ ACC_CRC8_POLY);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

bool ACC_MAC_Verify(const uint8_t *data4, const uint8_t *received_mac)
{
    uint8_t tag[16] = {0};
    const mbedtls_cipher_info_t *cipher_info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);

    if (cipher_info == NULL)
    {
        return false;
    }

    int ret = mbedtls_cipher_cmac(cipher_info, ACC_PSK, (size_t)(ACC_PSK_LEN * 8U),
                                   data4, ACC_MAC_INPUT_LEN, tag);
    if (ret != 0)
    {
        /* Crypto call itself failed (should not happen with valid params) --
         * treat as a verification failure, never as an implicit pass. */
        return false;
    }

    /* Constant-time compare: OR all byte diffs together instead of an
     * early-return, so timing does not leak which byte first mismatched. */
    uint8_t diff = 0U;
    for (uint8_t i = 0U; i < ACC_MAC_LEN; i++)
    {
        diff |= (uint8_t)(tag[i] ^ received_mac[i]);
    }
    return (diff == 0U);
}

bool ACC_Counter_IsFresh(const ACC_SecurityState_t *state, uint8_t counter)
{
    if (state->last_accepted_counter == ACC_COUNTER_SENTINEL_NONE)
    {
        /* First frame ever accepted -- nothing to compare against. */
        return true;
    }

    uint8_t last = (uint8_t)state->last_accepted_counter;
    /* Modulo-256 forward distance from last accepted counter to this one.
     * A distance of 0 means "same frame again" (reject). A distance in
     * (0, window] means "newer, within tolerance" (accept). A larger
     * distance is treated as old/replayed rather than a huge forward
     * jump, which is what makes this an effective anti-replay window. */
    uint8_t forward_distance = (uint8_t)(counter - last);
    return (forward_distance > 0U) && (forward_distance <= ACC_COUNTER_FORWARD_WINDOW);
}

void ACC_Counter_Accept(ACC_SecurityState_t *state, uint8_t counter)
{
    state->last_accepted_counter = (uint32_t)counter;
}

bool ACC_Security_ValidateFrame(ACC_SecurityState_t *state,
                                 const ACC_CanFrame_t *frame,
                                 ACC_Command_t *out_cmd,
                                 uint8_t *out_distance,
                                 uint8_t *out_counter)
{
    /* 1) CRC8 -- cheap corruption filter, NOT a security check. */
    uint8_t computed_crc = ACC_CRC8_Compute((const uint8_t *)frame, ACC_CRC8_INPUT_LEN);
    if (computed_crc != frame->crc8)
    {
        state->crc_fault_count++;
        return false;
    }

    /* 2) CMAC -- the actual authenticity check. */
    if (!ACC_MAC_Verify((const uint8_t *)frame, frame->mac))
    {
        state->mac_fault_count++;
        return false;
    }

    /* 3) Rolling counter -- anti-replay, checked only once the frame is
     *    known authentic (no point rate-limiting on counter for garbage
     *    frames that already failed CRC/MAC). */
    if (!ACC_Counter_IsFresh(state, frame->counter))
    {
        state->replay_fault_count++;
        return false;
    }

    /* All checks passed: commit the new counter and hand back the payload. */
    ACC_Counter_Accept(state, frame->counter);
    *out_cmd      = (ACC_Command_t)frame->command;
    *out_distance = frame->distance_cm;
    *out_counter  = frame->counter;
    return true;
}
