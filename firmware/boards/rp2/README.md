# SUF2 RP2 loader

One CMake project builds the loader and blink example for both RP2040 and
RP2350. Select the chip with `SUF2_BOARD`; use separate build directories:

```sh
cmake -S firmware/boards/rp2 -B build/rp2040 -G Ninja -DSUF2_BOARD=rp2040
cmake --build build/rp2040
cmake -S firmware/boards/rp2 -B build/rp2350 -G Ninja -DSUF2_BOARD=rp2350
cmake --build build/rp2350
```

| Chip | Default board | Platform | Flash | App vector offset |
| --- | --- | --- | --- | --- |
| RP2040 | `pico` | `rp2040` | 2 MB | `0x100` |
| RP2350 | `pico2` | `rp2350-arm-s` | 4 MB | `0` |

The default chip is RP2040. For another board, set `PICO_BOARD` and, if needed,
`SUF2_FLASH_SIZE`. The board must match the selected chip. RP2350 RISC-V builds
are not supported. After upgrading from the old board folders, run
`tools/firmware/build.sh all --clean` to recreate their build caches.

RP2040 has no hardware secure boot. The RP2350 build does not enable secure
boot or program OTP. See the [security limits](../../README.md#security-limits).

| Path | Contents |
| --- | --- |
| `CMakeLists.txt` | chip selection, SDK defaults, loader and example targets |
| `suf2_rp2.cmake` | flash layout, key table, `suf2_rp2_add_loader()` and `suf2_rp2_add_app()` |
| `pico_sdk_import.cmake` | uses the pico-sdk submodule unless `PICO_SDK_PATH` is set |
| [suf2-boot/](suf2-boot/README.md) | bootloader, flash/USB adapters, crypto port, app helper and simulation tests |
| [blink/](blink/README.md) | example application that runs under the loader |

Build either target independently after configuring:

```sh
cmake --build build/rp2040 --target suf2_loader
cmake --build build/rp2040 --target blink
```

Output paths stay unchanged: `build/<board>/suf2_loader.*` for the loader and
`build/<board>/blink/` for the application.

How it works, and how to build, is in the [firmware README](../../README.md).
The reusable installer/journal and FAT16 drive live in
[firmware/suf2](../../suf2); their public contracts are in `include/suf2/`.
