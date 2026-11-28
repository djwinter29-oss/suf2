# SUF2 loader for RP2040

CMake project that builds the [shared RP2 loader](../rp2-common) and the
blink example for the Raspberry Pi Pico (2 MB flash).

```sh
cmake -S firmware/boards/rp2040 -B build/rp2040 -G Ninja
cmake --build build/rp2040
```

For another RP2040 board, set `-DPICO_BOARD=<board>` and, if its flash is not
2 MB, `-DSUF2_FLASH_SIZE=<bytes>`.

The RP2040 has no secure boot: see the security limits in the
[firmware README](../../README.md#security-limits).
