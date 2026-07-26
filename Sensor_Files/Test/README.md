# Host-side crypto unit test

Not part of the STM32 firmware build -- this is a plain-gcc sanity check
for `can_security.c` (AES-128 + RFC 4493 CMAC + CRC8), verified against
the official RFC 4493 test vectors and a PyCryptodome cross-check before
this firmware was handed over.

```
gcc -Wall -Wextra -std=c99 -O2 ../Core/Src/can_security.c test_can_security.c -I ../Core/Inc -o test_can_security
./test_can_security
```

Expected output:
```
[CRC8] got=0xFB expected=0xFB -> PASS
[CMAC-4byte] got=21da611d expected=21da611d -> PASS

ALL TESTS PASSED
```
