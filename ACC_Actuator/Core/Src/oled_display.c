/**
 ******************************************************************************
 * @file    oled_display.c
 * @brief   u8g2 HAL bridge + Actuator Node status layout implementation.
 ******************************************************************************
 */
#include "oled_display.h"
#include <stdio.h>
#include <string.h>

#define ACC_OLED_I2C_ADDR_8BIT (0x78U) /*!< SH1106 default 8-bit write address */
#define ACC_OLED_I2C_TIMEOUT_MS (100U)

static I2C_HandleTypeDef *s_hi2c = NULL;

/**
 * @brief u8g2 byte-layer callback bridging to HAL_I2C_Master_Transmit.
 *        Buffers bytes between START/END_TRANSFER messages, then issues a
 *        single blocking I2C transaction -- adequate for a 128x64
 *        monochrome display refreshed a few times a second (PRD §3.2 step 5).
 */
static uint8_t ACC_U8x8_ByteHwI2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[32];
    static uint8_t buf_idx = 0U;
    uint8_t *data;

    switch (msg)
    {
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            while (arg_int > 0U)
            {
                if (buf_idx < sizeof(buffer))
                {
                    buffer[buf_idx++] = *data;
                }
                data++;
                arg_int--;
            }
            break;

        case U8X8_MSG_BYTE_INIT:
        case U8X8_MSG_BYTE_SET_DC:
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0U;
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            (void)HAL_I2C_Master_Transmit(s_hi2c, ACC_OLED_I2C_ADDR_8BIT, buffer, buf_idx,
                                           ACC_OLED_I2C_TIMEOUT_MS);
            break;

        default:
            return 0U;
    }
    return 1U;
}

/**
 * @brief u8g2 GPIO/delay callback. No dedicated pins to toggle since we use
 *        hardware I2C, so only the millisecond delay path matters.
 */
static uint8_t ACC_U8x8_GpioAndDelay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
        case U8X8_MSG_DELAY_MILLI:
            HAL_Delay(arg_int);
            break;
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
        case U8X8_MSG_GPIO_I2C_CLOCK:
        case U8X8_MSG_GPIO_I2C_DATA:
        default:
            break;
    }
    return 1U;
}

static const char *ACC_CommandToString(ACC_Command_t cmd)
{
    switch (cmd)
    {
        case ACC_CMD_STOP:         return "STOP";
        case ACC_CMD_DECELERATE:   return "DECELERATE";
        case ACC_CMD_MAINTAIN:     return "MAINTAIN";
        case ACC_CMD_ACCELERATE:   return "ACCELERATE";
        case ACC_CMD_SENSOR_FAULT: return "SENSOR FLT";
        default:                   return "UNKNOWN";
    }
}

void ACC_Display_Init(u8g2_t *u8g2, I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2, U8G2_R0, ACC_U8x8_ByteHwI2c, ACC_U8x8_GpioAndDelay);
    u8g2_SetI2CAddress(u8g2, ACC_OLED_I2C_ADDR_8BIT);
    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);
    u8g2_ClearBuffer(u8g2);
    u8g2_SendBuffer(u8g2);
}

void ACC_Display_Draw(u8g2_t *u8g2, const ACC_DisplaySnapshot_t *snap)
{
    char line[32];

    u8g2_ClearBuffer(u8g2);

    u8g2_SetFont(u8g2, u8g2_font_7x13B_tf);
    u8g2_DrawStr(u8g2, 0, 11, "ACTUATOR NODE");

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    snprintf(line, sizeof(line), "Cmd:  %s", ACC_CommandToString(snap->last_command));
    u8g2_DrawStr(u8g2, 0, 26, line);

    snprintf(line, sizeof(line), "Dist: %03u cm   Rx#: %u", snap->distance_cm, snap->rx_counter);
    u8g2_DrawStr(u8g2, 0, 38, line);

    snprintf(line, sizeof(line), "Motor: %u%% %s", snap->motor_duty_pct,
              snap->motor_forward ? "FWD" : "OFF");
    u8g2_DrawStr(u8g2, 0, 50, line);

    /* Fail-safe visibility is the whole point of this display (PRD §3.6):
     * make LOST/FAULT explicit rather than just showing Motor: 0% silently. */
    snprintf(line, sizeof(line), "Link: %-4s Sec: %s",
              snap->link_ok ? "OK" : "LOST",
              snap->security_fault ? "FAULT" : "OK");
    u8g2_DrawStr(u8g2, 0, 62, line);

    u8g2_SendBuffer(u8g2);
}
