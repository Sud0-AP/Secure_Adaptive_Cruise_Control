/*
 * startup.c - STM32F407VGT6 reset entry + SRAM PUF raw dump
 *
 * ORDER OF OPERATIONS ON RESET (this is the whole point of the experiment):
 *   1. CPU loads SP from vector[0], PC from vector[1] (Reset_Handler) - hardware, before any code runs.
 *   2. Reset_Handler runs FIRST, before SystemInit, before .data copy, before .bss zeroing.
 *   3. Reset_Handler's very first action is puf_raw_dump(): bring up USART2 on the
 *      reset-default HSI 16 MHz clock (no PLL, no SystemInit yet) and blast out the raw,
 *      untouched contents of the .puf_region (CCM RAM) byte-for-byte.
 *   4. Only AFTER the dump is complete do we do normal startup: copy .data, zero .bss
 *      (the linker script keeps .puf_region OUT of .bss so it is never zeroed), then call main().
 *
 * This guarantees the bytes you capture over UART are the SRAM cells' power-up state,
 * not something overwritten by C runtime init, interrupts, or stack usage.
 *
 * No HAL/CMSIS dependency - direct register access, so this drops into any toolchain.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Minimal direct register access (no CMSIS needed)                         */
/* ------------------------------------------------------------------------ */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OSPEEDR   (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

#define USART2_BASE     0x40004400UL
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0C))

#define RCC_AHB1ENR_GPIOAEN   (1UL << 0)
#define RCC_APB1ENR_USART2EN  (1UL << 17)

#define USART_SR_TXE    (1UL << 7)
#define USART_SR_TC     (1UL << 6)
#define USART_CR1_UE    (1UL << 13)
#define USART_CR1_TE    (1UL << 3)

/* PA2 = USART2_TX, alternate function AF7. Board uses HSI (16 MHz) at this
 * point (default reset clock, PLL not enabled yet). BRR value below gives
 * 115200 baud from a 16 MHz APB1 clock (APB1 prescaler = /1 at reset):
 *   USARTDIV = 16000000 / (16 * 115200) = 8.6805...
 *   mantissa = 8, fraction = round(0.6805 * 16) = 11 (0xB)
 *   BRR = (8 << 4) | 11 = 0x8B
 * If your board's crystal/HSI trim differs, actual baud will be close enough
 * for a UART to lock onto at 115200; if you see garbage, try 9600 first to
 * confirm wiring, then come back and recompute BRR for your exact clock.
 */
#define USART2_BRR_115200_AT_16MHZ  0x683UL

/* ------------------------------------------------------------------------ */
/* PUF region - MUST match the linker script's .puf_region size exactly     */
/* ------------------------------------------------------------------------ */
#define PUF_REGION_SIZE 2048u

/* Symbols provided by the linker script marking the CCM RAM PUF region */
extern uint8_t _spuf;
extern uint8_t _epuf;

/* Symbols provided by the linker script for .data/.bss init */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
extern uint32_t _estack;

static void uart_init_raw(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA2 -> alternate function mode (MODER bits [5:4] = 10) */
    GPIOA_MODER &= ~(0x3UL << (2 * 2));
    GPIOA_MODER |=  (0x2UL << (2 * 2));
    GPIOA_OSPEEDR |= (0x3UL << (2 * 2)); /* high speed */
    /* AF7 = USART2, goes in AFRL nibble for pin 2 (bits [11:8]) */
    GPIOA_AFRL &= ~(0xFUL << (4 * 2));
    GPIOA_AFRL |=  (0x7UL << (4 * 2));

    USART2_BRR = USART2_BRR_115200_AT_16MHZ;
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE;
}

static void uart_send_byte(uint8_t b)
{
    while (!(USART2_SR & USART_SR_TXE)) { }
    USART2_DR = b;
}

static void uart_send_str(const char *s)
{
    while (*s) uart_send_byte((uint8_t)*s++);
}

static void uart_flush(void)
{
    while (!(USART2_SR & USART_SR_TC)) { }
}

/*
 * Frame format sent once, immediately on every reset:
 *   "PUFSTART\n"
 *   <PUF_REGION_SIZE raw bytes, exactly>
 *   "\nPUFEND\n"
 * No compression, no re-formatting - the host script consumes raw bytes so
 * nothing in the toolchain can quietly "clean up" a bit for you.
 */
static void puf_raw_dump(void)
{
    uart_init_raw();
    uart_send_str("PUFSTART\n");

    uint8_t *p = &_spuf;
    for (uint32_t i = 0; i < PUF_REGION_SIZE; i++) {
        uart_send_byte(p[i]);
    }

    uart_send_str("\nPUFEND\n");
    uart_flush();
}

/* ------------------------------------------------------------------------ */
/* Standard Cortex-M4 boot follows, AFTER the dump                          */
/* ------------------------------------------------------------------------ */
extern int main(void);

void Reset_Handler(void)
{
    /* STEP 1: dump the untouched PUF region FIRST, before touching any RAM
     * initialization. This is deliberately the very first C statement. */
    puf_raw_dump();

    /* STEP 2: now do normal startup - copy .data from flash, zero .bss.
     * .puf_region is a separate NOLOAD section (see linker script) and is
     * NOT part of .bss, so this loop never touches it. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    /* Not calling SystemInit()/enabling the PLL on purpose: we stay on the
     * reset-default HSI 16 MHz clock so the UART baud rate used for the PUF
     * dump is identical to the baud rate main() uses. Add clock setup here
     * later if your application needs full speed - just recompute BRR. */

    main();

    while (1) { }
}

/* ------------------------------------------------------------------------ */
/* Minimal fault handlers - infinite loop so a crash is visibly stuck       */
/* rather than silently vectoring off into garbage.                        */
/* ------------------------------------------------------------------------ */
void Default_Handler(void) { while (1) { } }

void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* ------------------------------------------------------------------------ */
/* Vector table - only the entries we need populated, rest point at         */
/* Default_Handler so an unexpected IRQ doesn't run off into flash 0x0.     */
/* ------------------------------------------------------------------------ */
typedef void (*isr_ptr)(void);

__attribute__((section(".isr_vector")))
const isr_ptr vector_table[] = {
    (isr_ptr)&_estack,      /* 0: initial stack pointer */
    Reset_Handler,           /* 1: reset */
    NMI_Handler,              /* 2 */
    HardFault_Handler,        /* 3 */
    MemManage_Handler,        /* 4 */
    BusFault_Handler,         /* 5 */
    UsageFault_Handler,       /* 6 */
    0, 0, 0, 0,                /* 7-10: reserved */
    SVC_Handler,               /* 11 */
    DebugMon_Handler,          /* 12 */
    0,                          /* 13: reserved */
    PendSV_Handler,             /* 14 */
    SysTick_Handler,            /* 15 */
    /* IRQ0.. left out on purpose - not used by this experiment. Extend the
     * table here (following the STM32F407 reference manual vector table)
     * if you add peripherals that need interrupts later. */
};
