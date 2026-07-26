# Actuator Node (Board B) — integration notes

These files are meant to drop into your existing CubeMX project (the one
configured per the CubeMX settings we went through). A few things live
**outside** these source files and need doing by hand:

## 1. CubeMX pin labels (GPIO User Labels)

The code references these symbolic names, which come from CubeMX's
"User Label" field on each pin (Pinout view → click pin → set label).
Set these so `main.h` defines matching `..._GPIO_Port` / `..._Pin` macros:

| Pin  | User Label        |
|------|--------------------|
| PB0  | `MOTOR_IN1`        |
| PB1  | `MOTOR_IN2`        |
| PC13 | `ACC_STATUS_LED`   |

If you label them differently, just find/replace the corresponding macro
names in `motor_driver.c` and `app_tasks.c`.

## 2. FreeRTOSConfig.h — 3 edits to your existing file

Not included as a full file since you're reusing your RTOS-lab
`FreeRTOSConfig.h`. Apply these three edits only:

```c
#define configMAX_PRIORITIES            6   /* was 5 */
#define configCHECK_FOR_STACK_OVERFLOW   2   /* was 0 */
#define configUSE_MALLOC_FAILED_HOOK     1   /* was 0 */
```

The corresponding hooks (`vApplicationStackOverflowHook`,
`vApplicationMallocFailedHook`) are already implemented in `app_tasks.c`.

**NVIC dependency:** in CubeMX's NVIC panel, set `CAN1_RX0_IRQn` preemption
priority numerically **≥ 5**. It calls `xQueueSendFromISR`, and anything
numerically lower than `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (5,
already set in your config) will hard-fault on first entry.

## 3. Libraries to vendor

- **mbedTLS** — needs `aes.c`, `cmac.c`, `cipher.c`, `cipher_wrap.c`,
  `platform_util.c` (or pull the whole `library/` + `include/mbedtls/`
  via CubeMX Middleware if your CubeMX version offers it). A minimal
  `mbedtls_config.h` enabling `MBEDTLS_AES_C`, `MBEDTLS_CMAC_C`,
  `MBEDTLS_CIPHER_C` is enough — no TLS/X.509 needed.
- **u8g2** — vendor `csrc/` and `cppsrc/` (C-only, so just `csrc/`) plus
  `u8g2.h`/`u8x8.h` from the u8g2 repo. No STM32-specific port needed;
  `oled_display.c` implements the two required callbacks directly against
  HAL.

## 4. Build — add to your include paths / sources

```
Inc/security_config.h
Inc/can_security.h      Src/can_security.c
Inc/can_driver.h        Src/can_driver.c
Inc/motor_driver.h      Src/motor_driver.c
Inc/oled_display.h      Src/oled_display.c
Inc/app_tasks.h         Src/app_tasks.c
Src/main.c   (merge the marked USER CODE sections into your generated main.c —
              don't overwrite the whole file, your MX_*_Init() calls stay as-is)
```

## 5. Suggested bring-up order

1. Flash with CAN/motor code stubbed out, confirm OLED shows the static
   layout (proves I2C1 + u8g2 bridge work).
2. Confirm `LedStatusTask` shows the **fast** (link-lost) pattern with
   Board A absent — proves the fail-safe path and LED task run even with
   no CAN traffic at all.
3. Bring up Board A, confirm frames validate (`Cmd:`/`Dist:`/`Rx#:` update
   on the OLED, LED goes to slow steady blink).
4. Deliberately break something on Board A's key or counter to confirm
   `Sec: FAULT` shows and the motor stays at 0% — this is the actual point
   of the demo.
