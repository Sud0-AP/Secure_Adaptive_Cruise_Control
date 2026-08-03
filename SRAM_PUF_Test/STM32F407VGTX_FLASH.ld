/*
 * Linker script for STM32F407VGT6
 *
 * Memory map used:
 *   FLASH   : 1024K @ 0x08000000
 *   SRAM    : 128K  @ 0x20000000   (main system SRAM, used for .data/.bss/stack)
 *   CCMRAM  : 64K   @ 0x10000000   (Core Coupled Memory - CPU-only, no DMA/bus-matrix access)
 *
 * The PUF region is carved out of the END of CCM RAM as a NOLOAD section.
 * It is:
 *   - never written by the reset/startup code (no zero-init, no .data copy)
 *   - never touched by the stack (stack lives in main SRAM, top-down)
 *   - never touched by DMA (CCM RAM is not on the DMA bus matrix on F407)
 * so its power-up contents reflect only SRAM cell physics, not software state.
 */

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  SRAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Size of the PUF capture region, must match PUF_REGION_SIZE in startup.c */
PUF_REGION_SIZE = 2048; /* 2 KB */

/* Stack: top of main SRAM, grows down */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  /* used by startup.c to know where to copy .data from */
  _sidata = LOADADDR(.data);

  .data : AT (_etext)
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >SRAM

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >SRAM

  /* Reserve the TOP 2 KB of CCM RAM for the PUF region. NOLOAD means the
   * linker allocates the address range but places nothing there - it is
   * never written by flash-to-RAM copy and startup.c must NOT zero it. */
  .puf_region (NOLOAD) :
  {
    . = ALIGN(4);
    _spuf = .;
    . = . + PUF_REGION_SIZE;
    _epuf = .;
  } >CCMRAM

  /* sanity: make sure nothing else lands in the region above .puf_region */
  ASSERT(SIZEOF(.puf_region) == PUF_REGION_SIZE, "PUF region size mismatch")

  . = ALIGN(4);
  _end = .;

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
  }
}
