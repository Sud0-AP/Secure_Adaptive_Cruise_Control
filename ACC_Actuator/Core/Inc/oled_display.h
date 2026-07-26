/**
 ******************************************************************************
 * @file    oled_display.h
 * @brief   u8g2 <-> HAL I2C1 bridge for the SH1106 128x64 OLED, plus the
 *          Actuator Node status layout (PRD §3.6).
 ******************************************************************************
 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "i2c.h" /* CubeMX-generated I2C1 handle (hi2c1) */
#include "u8g2.h"
#include "app_tasks.h" /* ACC_DisplaySnapshot_t */
#include <stdint.h>

/**
 * @brief Initialize the u8g2 instance for the confirmed SH1106 module
 *        (XFP1116-07A Y, 128x64) over HAL I2C1.
 * @param u8g2 u8g2 instance to initialize (storage owned by caller).
 * @param hi2c Pointer to the CubeMX-generated I2C1 handle.
 */
void ACC_Display_Init(u8g2_t *u8g2, I2C_HandleTypeDef *hi2c);

/**
 * @brief Redraw the full status layout from a display snapshot.
 * @param u8g2 Initialized u8g2 instance.
 * @param snap Latest snapshot read from xDisplayMailbox.
 */
void ACC_Display_Draw(u8g2_t *u8g2, const ACC_DisplaySnapshot_t *snap);

#endif /* OLED_DISPLAY_H */
