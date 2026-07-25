/**
 * @file    oled_display.c
 * @brief   See oled_display.h.
 */
#include "oled_display.h"
#include "can_driver.h"      /* ACC_Command_t */
#include "hcsr04_driver.h"   /* not strictly needed here, kept for the
                                 sentinel-value convention documented in
                                 oled_display.h's snapshot struct */
#include <string.h>

#define OLED_CMD_CONTROL_BYTE   0x00U
#define OLED_DATA_CONTROL_BYTE  0x40U
#define OLED_COL_OFFSET         2U /**< SH1106 132-col internal buffer, 128-px module offset */

static I2C_HandleTypeDef *s_hi2c = NULL;
static uint8_t s_framebuffer[OLED_PAGES][OLED_WIDTH_PX]; /* [page][column] = 8 vertical px */

/** Compact 5x7 font, column-major, bit0 = top pixel of the 7-pixel-tall
 *  glyph. Round-trip verified (glyph design -> column bytes -> rendered
 *  back to the intended shape) before this table was written into the
 *  driver -- see the design notes in the project chat log if regenerating. */
typedef struct
{
    char ch;
    uint8_t cols[5];
} oled_glyph_t;

static const oled_glyph_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x00, 0x36, 0x00, 0x00}},
    {'#', {0x14, 0x7F, 0x14, 0x7F, 0x14}},
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x41}},
    {'D', {0x7F, 0x41, 0x41, 0x41, 0x3E}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'I', {0x41, 0x41, 0x7F, 0x41, 0x41}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x0C, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
};
#define OLED_FONT_GLYPH_COUNT (sizeof(s_font) / sizeof(s_font[0]))
#define OLED_CHAR_ADVANCE_PX  6U /* 5 glyph columns + 1 spacing column */

static HAL_StatusTypeDef oled_write_cmd(uint8_t cmd)
{
    return HAL_I2C_Mem_Write(s_hi2c, OLED_I2C_ADDR_8BIT, OLED_CMD_CONTROL_BYTE,
                              I2C_MEMADD_SIZE_8BIT, &cmd, 1U, HAL_MAX_DELAY);
}

static const oled_glyph_t *oled_find_glyph(char c)
{
    for (size_t i = 0U; i < OLED_FONT_GLYPH_COUNT; i++)
    {
        if (s_font[i].ch == c)
        {
            return &s_font[i];
        }
    }
    return &s_font[0]; /* fall back to space for any unsupported character */
}

/**
 * @brief  Draw one character into the framebuffer at (page, col_px).
 *         Only touches the single page the glyph's 7 rows fit within --
 *         callers must not straddle a page boundary.
 */
static void oled_draw_char(uint8_t page, uint8_t col_px, char c)
{
    if (page >= OLED_PAGES)
    {
        return;
    }
    const oled_glyph_t *glyph = oled_find_glyph((char)((c >= 'a' && c <= 'z') ? (c - 32) : c));
    for (uint8_t i = 0U; i < 5U; i++)
    {
        const uint16_t col = (uint16_t)col_px + i;
        if (col < OLED_WIDTH_PX)
        {
            s_framebuffer[page][col] = glyph->cols[i];
        }
    }
}

/** Draw a left-aligned string; truncates silently at the screen edge. */
static void oled_draw_string(uint8_t page, uint8_t col_px, const char *str)
{
    uint16_t x = col_px;
    for (const char *p = str; *p != '\0'; p++)
    {
        if (x + 5U > OLED_WIDTH_PX)
        {
            break;
        }
        oled_draw_char(page, (uint8_t)x, *p);
        x = (uint16_t)(x + OLED_CHAR_ADVANCE_PX);
    }
}

/** Fill a solid horizontal bar spanning two pages (16px tall) from x=0 to
 *  x=width_px, used for the distance bar graph. */
static void oled_draw_bar(uint8_t page_top, uint16_t width_px)
{
    if (width_px > OLED_WIDTH_PX)
    {
        width_px = OLED_WIDTH_PX;
    }
    for (uint16_t x = 0U; x < OLED_WIDTH_PX; x++)
    {
        const uint8_t filled = (x < width_px) ? 0xFFU : 0x00U;
        s_framebuffer[page_top][x] = filled;
        if ((page_top + 1U) < OLED_PAGES)
        {
            s_framebuffer[page_top + 1U][x] = filled;
        }
    }
}

static void oled_clear_framebuffer(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

/** Push the entire framebuffer to the panel, one page at a time. */
static void oled_flush(void)
{
    for (uint8_t page = 0U; page < OLED_PAGES; page++)
    {
        (void)oled_write_cmd((uint8_t)(0xB0U | page));                            /* set page address */
        (void)oled_write_cmd((uint8_t)(0x00U | (OLED_COL_OFFSET & 0x0FU)));        /* col addr low nibble */
        (void)oled_write_cmd((uint8_t)(0x10U | ((OLED_COL_OFFSET >> 4U) & 0x0FU))); /* col addr high nibble */

        (void)HAL_I2C_Mem_Write(s_hi2c, OLED_I2C_ADDR_8BIT, OLED_DATA_CONTROL_BYTE,
                                 I2C_MEMADD_SIZE_8BIT, s_framebuffer[page],
                                 OLED_WIDTH_PX, HAL_MAX_DELAY);
    }
}

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    static const uint8_t init_seq[] = {
        0xAEU,                   /* display off */
        0x40U,                   /* start line 0 */
        0x81U, 0x80U,             /* contrast */
        0xA1U,                   /* segment remap */
        0xA6U,                   /* normal (non-inverted) display */
        0xA8U, 0x3FU,             /* multiplex ratio = 64 */
        0xC8U,                   /* COM scan direction remapped */
        0xD3U, 0x00U,             /* display offset = 0 */
        0xD5U, 0x80U,             /* clock divide / osc frequency */
        0xD9U, 0x22U,             /* pre-charge period */
        0xDAU, 0x12U,             /* COM pins hardware config */
        0xDBU, 0x40U,             /* VCOMH deselect level */
        0xADU, 0x8BU,             /* DC-DC enable (SH1106-specific) */
        0xAFU                    /* display ON */
    };

    for (size_t i = 0U; i < sizeof(init_seq); i++)
    {
        if (oled_write_cmd(init_seq[i]) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    oled_clear_framebuffer();
    oled_flush();
    return HAL_OK;
}

/** Human-readable command names for the "Cmd:" line -- keep in sync with
 *  ACC_Command_t in can_driver.h. */
static const char *oled_command_name(uint8_t command)
{
    switch ((ACC_Command_t)command)
    {
        case ACC_CMD_STOP:         return "STOP";
        case ACC_CMD_DECELERATE:   return "DECELERATE";
        case ACC_CMD_MAINTAIN:     return "MAINTAIN";
        case ACC_CMD_ACCELERATE:   return "ACCELERATE";
        case ACC_CMD_SENSOR_FAULT: return "FAULT";
        default:                   return "UNKNOWN";
    }
}

/** Format an unsigned value (0-999) as a fixed-width, zero-padded decimal
 *  string into out[4] (3 digits + terminator). Avoids pulling in sprintf. */
static void oled_format_u3(uint16_t value, char out[4])
{
    if (value > 999U)
    {
        value = 999U;
    }
    out[0] = (char)('0' + (value / 100U));
    out[1] = (char)('0' + ((value / 10U) % 10U));
    out[2] = (char)('0' + (value % 10U));
    out[3] = '\0';
}

void OLED_RenderSnapshot(const ACC_DisplaySnapshot_t *snapshot)
{
    oled_clear_framebuffer();

    oled_draw_string(0U, 0U, "SENSOR NODE");

    if (snapshot->sensor_fault)
    {
        oled_draw_string(3U, 0U, "SENSOR FAULT");
        oled_flush();
        return;
    }

    char dist_str[4];
    oled_format_u3(snapshot->distance_cm, dist_str);
    char dist_line[24] = "DIST:";
    strcat(dist_line, dist_str);
    strcat(dist_line, " CM");
    oled_draw_string(2U, 0U, dist_line);

    char cmd_line[24] = "CMD:";
    strcat(cmd_line, oled_command_name(snapshot->command));
    oled_draw_string(3U, 0U, cmd_line);

    char tx_str[4];
    oled_format_u3(snapshot->tx_counter, tx_str);
    char status_line[24] = "TX#:";
    strcat(status_line, tx_str);
    strcat(status_line, snapshot->can_tx_ok ? " CAN:OK" : " CAN:ERR");
    oled_draw_string(4U, 0U, status_line);

    /* Distance bar: 0-200cm mapped to 0-128px, clamped defensively. */
    uint16_t clamped = snapshot->distance_cm;
    if (clamped > ACC_DIST_CLAMP_MAX_CM)
    {
        clamped = ACC_DIST_CLAMP_MAX_CM;
    }
    const uint16_t bar_width_px = (uint16_t)(((uint32_t)clamped * OLED_WIDTH_PX) / ACC_DIST_CLAMP_MAX_CM);
    oled_draw_bar(6U, bar_width_px);

    oled_flush();
}
