# Shared RP2 loader

Code and CMake used by both the [RP2040](../rp2040) and [RP2350](../rp2350)
builds.

| Path | Contents |
| --- | --- |
| `suf2_rp2.cmake` | flash layout, key table, `suf2_rp2_add_loader()` and `suf2_rp2_add_app()` |
| `pico_sdk_import.cmake` | uses the pico-sdk submodule unless `PICO_SDK_PATH` is set |
| `loader/` | boot checks, install journal, USB drive |
| `app/` | `suf2_app.h`: lets an application reboot into update mode |
| `examples/blink/` | example application |
| `tests/` | host simulation of the flash and USB drive code |

How it works, and how to build, is in the [firmware README](../../README.md).
