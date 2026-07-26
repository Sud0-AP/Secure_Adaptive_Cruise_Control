# Sensor Node — Integration Notes

## 0. One CubeMX step I owe you — IWDG was never configured

Definition of Done requires an IWDG-based watchdog, and `HealthMonitorTask`
calls `HAL_IWDG_Refresh(&hiwdg)`, but **IWDG was never added to the CubeMX
walkthrough** across our earlier messages. You need to add it before this
compiles (a missing `hiwdg` extern will fail to link).

**CubeMX → Pinout & Configuration → System Core → IWDG**
- Enable IWDG.
- Prescaler: `/32`. LSI is ~32 kHz nominal (per your default-clock-tree
  screenshot), so `32000 / 32 = 1000 Hz` counter clock.
- Reload/Down-counter value: `1000` → 1000 counts / 1000 Hz ≈ **1 second
  timeout**. (LSI accuracy is loose, ±various % depending on temperature —
  1s vs. the 500ms `HealthMonitorTask` refresh period gives comfortable
  margin either way.)
- This auto-generates `MX_IWDG_Init()` and the `hiwdg` handle in `main.c` —
  nothing else to wire up.

## 1. Files to add to the project

```
Core/Inc/security_config.h
Core/Inc/can_security.h
Core/Src/can_security.c
Core/Inc/can_driver.h
Core/Src/can_driver.c
Core/Inc/hcsr04_driver.h
Core/Src/hcsr04_driver.c
Core/Inc/oled_display.h
Core/Src/oled_display.c
Core/Inc/app_tasks.h
Core/Src/app_tasks.c
```

No mbedTLS/tiny-AES-c dependency needed — `can_security.c` is a
self-contained AES-128 + RFC 4493 CMAC implementation, verified against
the official RFC 4493 test vectors and a PyCryptodome cross-check before
being handed to you (see the design-session notes if you want to re-run
that verification yourself; the two check values are reproduced as a unit
test in `test_can_security.c`, which is host-buildable with plain `gcc` —
not part of the firmware build, just a sanity harness you can keep or
delete).

## 2. `FreeRTOSConfig.h` — apply these changes to your lab template

```c
/* was 5, we use priority levels 1-5 which needs indices 0..5 */
#define configMAX_PRIORITIES            ( 6 )

/* was 0 -- cheap insurance while bringing up 5 new tasks with hand-picked
 * stack sizes; catches overflow immediately instead of silent corruption */
#define configCHECK_FOR_STACK_OVERFLOW  ( 2 )

/* Required: your port.c defines vPortSVCHandler / xPortPendSVHandler /
 * xPortSysTickHandler, but startup_stm32f407xx.s's vector table still
 * references the CMSIS names SVC_Handler / PendSV_Handler / SysTick_Handler.
 * Without this remap, the scheduler hangs (Default_Handler) the instant
 * vTaskStartScheduler() issues its first SVC. Place near the top of the
 * file, after the header guard. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* Required for the ISR-safe API calls in hcsr04_driver.c / any future CAN
 * RX ISR. CubeMX would normally compute/inject these automatically via
 * the X-CUBE-FREERTOS panel; doing this manually means you own it.
 * configPRIO_BITS=4 matches the F407's 4 NVIC priority bits. */
#define configPRIO_BITS                               4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
```

**Action required in CubeIDE after this:** open TIM2's and CAN1's NVIC
priority settings (Pinout & Configuration → NVIC, or directly in the
generated `MX_TIM2_Init`/`MX_CAN1_Init` if you set priorities by hand) and
confirm both interrupts are set to a priority **numerically ≥ 5** (i.e.
lower urgency than the syscall threshold). CubeMX's default NVIC priority
for newly-enabled interrupts is usually already in a safe range, but this
is the one thing worth eyeballing since nothing will auto-enforce it here
the way the X-CUBE-FREERTOS panel would have.

## 3. `stm32f4xx_it.c` — one addition

Add the input-capture callback trampoline (HAL calls this automatically
from `TIM2_IRQHandler` → `HAL_TIM_IRQHandler`, no manual IRQ wiring needed
beyond what CubeMX already generated):

```c
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    HCSR04_IC_CaptureCallback(htim);
}
```

Also confirm `assert_failed()` exists somewhere in the project (you
enabled `USE_FULL_ASSERT` in `stm32f4xx_hal_conf.h`, which requires it).
If your CubeIDE template didn't stub one in automatically:

```c
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    __disable_irq();
    while (1) { /* trap here on a HAL parameter assertion failure */ }
}
```

## 4. `main.c` — initialisation order

After all `MX_*_Init()` calls (GPIO, TIM2, CAN1, I2C1, IWDG), **before**
`vTaskStartScheduler()`:

```c
HCSR04_Init(&htim2);

if (CANDRV_Init(&hcan1) != HAL_OK) { Error_Handler(); }

if (OLED_Init(&hi2c1) != HAL_OK) { Error_Handler(); }

if (!APP_Tasks_Init()) { Error_Handler(); }

vTaskStartScheduler();

/* Should never reach here */
while (1) { }
```

## 5. Quick pre-flight checklist before first flash

- [X] IWDG enabled in CubeMX (§0 above), project regenerated.
- [X] `FreeRTOSConfig.h` patched (§2), especially the handler remap —
      this is the one silent-hang failure mode if skipped.
- [X] `HAL_TIM_IC_CaptureCallback` trampoline added (§3).
- [X] `assert_failed()` present (§3).
- [X] TIM2 configured for **both-edges** input capture (already done on
      your side).
- [X] Echo divider node verified with a multimeter reads ~3.3V, not 5V,
      before powering the HC-SR04 (from earlier in our conversation —
      worth one last check before first power-up).
- [X] `security_config.h` is bit-for-bit identical to whatever Board B's
      PRD generates — this is the #1 way two independently-built boards
      fail to authenticate each other.
