# ESP32-S3 SUF2 boot and recovery

See the [board guide](../README.md) for setup, image semantics, layout and limits.

- `bootloader_components/main/` overrides ESP-IDF's second-stage entry point.
  It checks the expected factory/candidate partition positions, consumes a
  one-shot update request, and verifies committed SUF2 metadata and installed
  bytes before parsing the native application image.
- Only the selected authorized candidate or the trusted recovery application is
  passed to IDF as a factory partition. Other factory/test/OTA fallback entries
  are removed. The candidate's declared partition length is bounded by its
  signed image length. There is no deep-sleep fast-boot path.
- `main/` is the USB MSC recovery application. It reuses `suf2_store` and
  `suf2_disk`, resumes pending installs, serializes receiver/store access with a
  FreeRTOS mutex, and reports errors on UART and the virtual drive.
- `port/` contains bounded flash access, aligned bootloader read adapters,
  ROM SHA/micro-ecc verification and the RTC update-request helper.

The recovery application reuses the existing RP2 Mbed TLS source with IDF's
Mbed TLS library, rather than maintaining a duplicate backend. The second-stage
bootloader uses IDF's ROM SHA implementation and micro-ecc to avoid a heap-based
crypto port. Both stages include the same generated public-key table and product
policy. The portable public APIs remain in `firmware/suf2/include/suf2/`.
