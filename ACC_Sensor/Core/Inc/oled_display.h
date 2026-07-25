/**
 * @file    oled_display.h
 * @brief   Minimal SH1106 128x64 I2C driver + text rendering for the
 *          Sensor Node status screen (PRD §3.5).
 *
 * Deliberately NOT built on u8g2: pulling in the full u8g2 source tree and
 * its u8x8 byte/GPIO callback plumbing is a lot of surface area to get
 * right without hardware-in-the-loop testing. This driver instead
 * implements exactly what the PRD's display layout needs -- a page-buffer
 * framebuffer, a compact 5x7 bitmap font (verified by round-trip render
 * before shipping), and a horizontal bar primitive -- over blocking
 * HAL_I2C_Mem_Write, matching PRD §3.2's guidance that blocking I2C from
 * a low-priority task is fine at this refresh rate. If a nicer font or
 * graphics primitives are wanted later, this module's OLED_* functions
 * are the seam to swap in u8g2 without touching app_tasks.c.
 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define OLED_I2C_ADDR_8BIT   0x78U /**< confirmed against this board's ADDR-select resistor */
#define OLED_WIDTH_PX        128U
#define OLED_HEIGHT_PX       64U
#define OLED_PAGES           (OLED_HEIGHT_PX / 8U) /* 8 */

/** Latest system snapshot for the display, per PRD §3.4's "mailbox" pattern. */
typedef struct
{
    uint16_t distance_cm;     /**< HCSR04_TIMEOUT_SENTINEL if invalid */
    uint8_t  command;         /**< ACC_Command_t value */
    uint8_t  tx_counter;      /**< last transmitted frame's counter */
    bool     can_tx_ok;       /**< last CAN transmit attempt succeeded */
    bool     sensor_fault;    /**< 3+ consecutive HC-SR04 timeouts */
} ACC_DisplaySnapshot_t;

/**
 * @brief  Initialise the SH1106 controller (display on, normal orientation,
 *         page addressing mode) and clear the framebuffer.
 * @param  hi2c Pointer to the I2C1 handle.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Render one full snapshot to the OLED per the PRD §3.5 layout
 *         (title, distance, command name, Tx counter + CAN health, and a
 *         distance bar graph), or a "SENSOR FAULT" screen when
 *         snapshot->sensor_fault is set.
 * @param  snapshot Latest values to draw.
 */
void OLED_RenderSnapshot(const ACC_DisplaySnapshot_t *snapshot);

#endif /* OLED_DISPLAY_H */
