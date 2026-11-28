# SUF2 loader for RP2350

CMake project that builds the [shared RP2 loader](../rp2-common) and the
blink example for the Raspberry Pi Pico 2 (4 MB flash, Arm cores in secure
mode).

```sh
cmake -S firmware/boards/rp2350 -B build/rp2350 -G Ninja
cmake --build build/rp2350
```

For another RP2350 board, set `-DPICO_BOARD=<board>` and, if its flash is not
4 MB, `-DSUF2_FLASH_SIZE=<bytes>`. RISC-V builds are not supported.

This build does not enable RP2350 secure boot or program OTP. See the
security limits in the [firmware README](../../README.md#security-limits).
