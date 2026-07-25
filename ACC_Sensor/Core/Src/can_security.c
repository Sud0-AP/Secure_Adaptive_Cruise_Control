/**
 * @file    can_security.c
 * @brief   See can_security.h. Standalone AES-128 (FIPS-197) + AES-CMAC
 *          (RFC 4493) + CRC-8/SMBUS. No external crypto library dependency.
 */
#include "can_security.h"
#include <string.h>

/* ---------------------------------------------------------------------- *
 *  CRC-8, poly 0x07, init 0xFF, no input/output reflection.
 *
 *  NOTE: PRD §2.3 labels this "CRC-8/SMBUS" but specifies init=0xFF. The
 *  textbook CRC-8/SMBUS catalog entry actually uses init=0x00 (check value
 *  0xF4 for ASCII "123456789"); with init=0xFF as specified here, the check
 *  value for "123456789" is 0xFB instead. This is intentional and must NOT
 *  be "corrected" back to init=0x00 -- Board A and Board B must compute
 *  bit-for-bit identical CRC8 values, and the PRD's init=0xFF is the
 *  authoritative, shared spec both boards implement.
 * ---------------------------------------------------------------------- */

uint8_t CANSEC_Crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFU;
    for (size_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint8_t)((crc & 0x80U) ? ((uint8_t)(crc << 1) ^ 0x07U) : (uint8_t)(crc << 1));
        }
    }
    return crc;
}

/* ---------------------------------------------------------------------- *
 *  AES-128 core (FIPS-197). Encrypt-only: CMAC never needs AES decrypt.
 * ---------------------------------------------------------------------- */

#define AES_NB 4U   /* columns (32-bit words) in state, fixed at 4 for AES */
#define AES_NK 4U   /* key length in 32-bit words, 4 for AES-128 */
#define AES_NR 10U  /* number of rounds, 10 for AES-128 */

typedef uint8_t aes_state_t[4][4];

/** Standard FIPS-197 forward S-box. */
static const uint8_t s_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/** Round constants for AES-128 key expansion (10 rounds). */
static const uint8_t s_rcon[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36
};

static inline uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x & 0x80U) ? 0x1BU : 0x00U));
}

/**
 * @brief Expand a 16-byte AES-128 key into 11 round keys (176 bytes).
 * @param key       16-byte input key.
 * @param round_key Output buffer, 176 bytes (11 * 16).
 */
static void aes128_key_expansion(const uint8_t key[16], uint8_t round_key[176])
{
    uint8_t temp[4];

    memcpy(round_key, key, 16U);

    for (uint32_t i = AES_NK; i < AES_NB * (AES_NR + 1U); i++)
    {
        const uint32_t prev = (i - 1U) * 4U;
        temp[0] = round_key[prev + 0U];
        temp[1] = round_key[prev + 1U];
        temp[2] = round_key[prev + 2U];
        temp[3] = round_key[prev + 3U];

        if ((i % AES_NK) == 0U)
        {
            /* RotWord */
            const uint8_t t0 = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t0;

            /* SubWord */
            temp[0] = s_sbox[temp[0]];
            temp[1] = s_sbox[temp[1]];
            temp[2] = s_sbox[temp[2]];
            temp[3] = s_sbox[temp[3]];

            temp[0] = (uint8_t)(temp[0] ^ s_rcon[(i / AES_NK) - 1U]);
        }

        const uint32_t cur = i * 4U;
        const uint32_t base = (i - AES_NK) * 4U;
        round_key[cur + 0U] = (uint8_t)(round_key[base + 0U] ^ temp[0]);
        round_key[cur + 1U] = (uint8_t)(round_key[base + 1U] ^ temp[1]);
        round_key[cur + 2U] = (uint8_t)(round_key[base + 2U] ^ temp[2]);
        round_key[cur + 3U] = (uint8_t)(round_key[base + 3U] ^ temp[3]);
    }
}

static void aes_add_round_key(uint8_t round, aes_state_t state, const uint8_t round_key[176])
{
    for (uint8_t c = 0U; c < 4U; c++)
    {
        for (uint8_t r = 0U; r < 4U; r++)
        {
            state[r][c] = (uint8_t)(state[r][c] ^ round_key[(round * 16U) + (c * 4U) + r]);
        }
    }
}

static void aes_sub_bytes(aes_state_t state)
{
    for (uint8_t r = 0U; r < 4U; r++)
    {
        for (uint8_t c = 0U; c < 4U; c++)
        {
            state[r][c] = s_sbox[state[r][c]];
        }
    }
}

static void aes_shift_rows(aes_state_t state)
{
    uint8_t t;

    /* Row 1: shift left by 1 */
    t = state[1][0];
    state[1][0] = state[1][1]; state[1][1] = state[1][2];
    state[1][2] = state[1][3]; state[1][3] = t;

    /* Row 2: shift left by 2 */
    t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
    t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;

    /* Row 3: shift left by 3 (== shift right by 1) */
    t = state[3][3];
    state[3][3] = state[3][2]; state[3][2] = state[3][1];
    state[3][1] = state[3][0]; state[3][0] = t;
}

static void aes_mix_columns(aes_state_t state)
{
    for (uint8_t c = 0U; c < 4U; c++)
    {
        const uint8_t a0 = state[0][c];
        const uint8_t a1 = state[1][c];
        const uint8_t a2 = state[2][c];
        const uint8_t a3 = state[3][c];

        state[0][c] = (uint8_t)(aes_xtime(a0) ^ (uint8_t)(aes_xtime(a1) ^ a1) ^ a2 ^ a3);
        state[1][c] = (uint8_t)(a0 ^ aes_xtime(a1) ^ (uint8_t)(aes_xtime(a2) ^ a2) ^ a3);
        state[2][c] = (uint8_t)(a0 ^ a1 ^ aes_xtime(a2) ^ (uint8_t)(aes_xtime(a3) ^ a3));
        state[3][c] = (uint8_t)((uint8_t)(aes_xtime(a0) ^ a0) ^ a1 ^ a2 ^ aes_xtime(a3));
    }
}

/**
 * @brief Encrypt exactly one 16-byte block with AES-128 (ECB, single block).
 * @param round_key Expanded key schedule (176 bytes) from aes128_key_expansion().
 * @param block     In/out 16-byte block, encrypted in place.
 */
static void aes128_encrypt_block(const uint8_t round_key[176], uint8_t block[16])
{
    aes_state_t state;
    for (uint8_t c = 0U; c < 4U; c++)
    {
        for (uint8_t r = 0U; r < 4U; r++)
        {
            state[r][c] = block[(c * 4U) + r];
        }
    }

    aes_add_round_key(0U, state, round_key);

    for (uint8_t round = 1U; round < AES_NR; round++)
    {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(round, state, round_key);
    }

    /* Final round: no MixColumns */
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(AES_NR, state, round_key);

    for (uint8_t c = 0U; c < 4U; c++)
    {
        for (uint8_t r = 0U; r < 4U; r++)
        {
            block[(c * 4U) + r] = state[r][c];
        }
    }
}

/* ---------------------------------------------------------------------- *
 *  AES-CMAC (RFC 4493), specialised for a fixed 4-byte message.
 * ---------------------------------------------------------------------- */

static const uint8_t CONST_RB = 0x87U; /* per RFC 4493 */

/** Left-shift a 16-byte block by 1 bit, MSB-first. */
static void cmac_leftshift_block(const uint8_t in[16], uint8_t out[16])
{
    uint8_t overflow = 0U;
    for (int8_t i = 15; i >= 0; i--)
    {
        const uint8_t next_overflow = (uint8_t)((in[i] & 0x80U) >> 7);
        out[i] = (uint8_t)((uint8_t)(in[i] << 1) | overflow);
        overflow = next_overflow;
    }
}

static void cmac_xor_block(const uint8_t a[16], const uint8_t b[16], uint8_t out[16])
{
    for (uint8_t i = 0U; i < 16U; i++)
    {
        out[i] = (uint8_t)(a[i] ^ b[i]);
    }
}

void CANSEC_ComputeTruncatedCmac(const uint8_t key[CANSEC_AES_KEY_LEN],
                                  const uint8_t msg4[CANSEC_MACINPUT_LEN],
                                  uint8_t out_mac4[CANSEC_CMAC_TRUNC_LEN])
{
    uint8_t round_key[176];
    aes128_key_expansion(key, round_key);

    /* --- Subkey generation (RFC 4493 section 2.3) --- */
    uint8_t zero_block[16] = {0};
    uint8_t l_block[16];
    memcpy(l_block, zero_block, 16U);
    aes128_encrypt_block(round_key, l_block); /* L = AES-128(K, 0^128) */

    uint8_t k1[16];
    cmac_leftshift_block(l_block, k1);
    if ((l_block[0] & 0x80U) != 0U)
    {
        k1[15] = (uint8_t)(k1[15] ^ CONST_RB);
    }

    uint8_t k2[16];
    cmac_leftshift_block(k1, k2);
    if ((k1[0] & 0x80U) != 0U)
    {
        k2[15] = (uint8_t)(k2[15] ^ CONST_RB);
    }

    /* --- MAC of a single, incomplete (4-byte) last block ---
     * Per RFC 4493: since Mlen (4) < block size (16), n = 1 and the block
     * is "not complete", so:
     *   M_last = padding(M_1) XOR K2
     *   X      = 0^128            (no prior blocks, n == 1)
     *   Y      = X XOR M_last
     *   T      = AES-128(K, Y)
     * padding(M_1) = M_1 || 0x80 || 0x00...  (pad to 16 bytes)
     */
    uint8_t padded[16] = {0};
    memcpy(padded, msg4, CANSEC_MACINPUT_LEN);
    padded[CANSEC_MACINPUT_LEN] = 0x80U; /* remaining bytes already zero */

    uint8_t m_last[16];
    cmac_xor_block(padded, k2, m_last);

    uint8_t y[16];
    cmac_xor_block(zero_block, m_last, y); /* X is all-zero, so Y == M_last, kept explicit for clarity */

    uint8_t tag[16];
    memcpy(tag, y, 16U);
    aes128_encrypt_block(round_key, tag); /* T = AES-128(K, Y) */

    memcpy(out_mac4, tag, CANSEC_CMAC_TRUNC_LEN); /* truncate: first 4 bytes, per PRD §2.4 */
}
