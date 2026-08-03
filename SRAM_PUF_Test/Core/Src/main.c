/*
 * main.c - runs AFTER the PUF dump has already happened in Reset_Handler.
 */

#include <stdint.h>

#define RCC_AHB1ENR   (*(volatile uint32_t *)(0x40023800UL + 0x30))
#define GPIOD_MODER   (*(volatile uint32_t *)(0x40020C00UL + 0x00))
#define GPIOD_ODR     (*(volatile uint32_t *)(0x40020C00UL + 0x14))

#define USART2_SR     (*(volatile uint32_t *)(0x40004400UL + 0x00))
#define USART2_DR     (*(volatile uint32_t *)(0x40004400UL + 0x04))

static void delay(volatile uint32_t n) { while (n--) { __asm__("nop"); } }

static void uart_send_str(const char *s)
{
    while (*s) {
        while (!(USART2_SR & (1UL << 7))) { }
        USART2_DR = (uint8_t)*s++;
    }
}

int main(void)
{
    RCC_AHB1ENR |= (1UL << 3); /* GPIOD clock */
    GPIOD_MODER &= ~(0x3UL << (12 * 2));
    GPIOD_MODER |=  (0x1UL << (12 * 2)); /* PD12 output */

    uart_send_str("BOOT_COMPLETE\n");

    while (1) {
        GPIOD_ODR ^= (1UL << 12);
        delay(800000);
    }
}
