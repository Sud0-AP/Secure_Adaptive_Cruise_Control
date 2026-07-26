/**
 * @file    security_config.h
 * @brief   Shared pre-shared key (PSK) for AES-128-CMAC. IDENTICAL on the
 *          Sensor Node and Actuator Node -- copy this exact file to both
 *          projects, or Board B's MAC verification will never match.
 *
 * DEMO KEY ONLY. Do not reuse in any real product. Regenerate with:
 *   openssl rand -hex 16
 * and keep this file out of any public repo.
 */
#ifndef SECURITY_CONFIG_H
#define SECURITY_CONFIG_H

#include <stdint.h>

static const uint8_t ACC_PSK[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

#endif /* SECURITY_CONFIG_H */
