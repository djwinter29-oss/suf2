# Blink example

An application linked for the SUF2 application slot. It blinks the board LED;
typing `u` on its USB serial port requests reboot into update mode.

Configure through the [RP2 project](../README.md), then build the app:

```sh
cmake --build build/rp2040 --target blink
```

The ELF is `build/<board>/blink/blink.elf`. When `SUF2_SIGNING_KEY` is set,
the build also produces `build/<board>/blink/blink.suf2`.

The [build script](../../../../tools/firmware/build.sh) selects the chip and
signs the example with local test keys by default. See the
[firmware guide](../../../README.md#writing-your-own-application) for your
own application's integration.
