# ESP32-S3 signed blink example

Build with `tools/firmware/build.sh esp32s3`; see the [board guide](../README.md).
The app toggles an external LED on GPIO2 every 500 ms. Change
`CONFIG_SUF2_BLINK_GPIO` in the blink project's ESP-IDF configuration for another
suitable GPIO. Do not choose pins used for USB, flash or board peripherals.

Type `u` on UART to set the one-shot RTC request and restart into SUF2 recovery.
This helper uses IDF's reserved eight-byte custom RTC FAST region on CPU0;
keep the shared reservation and CRC-exclusion settings.

Install **`blink.suf2` through SUF2BOOT**. The native `blink.bin` is the signing
input, not a loader-provisioning file. Never flash this project's generated
bootloader or run its default `idf.py flash` command: that replaces the SUF2
bootstrap chain.
