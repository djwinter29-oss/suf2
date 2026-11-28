# Third-party libraries

Git submodules, pinned to release tags.

| Submodule | Version | Used for |
| --- | --- | --- |
| `pico-sdk` | 2.3.1 | RP2040/RP2350 builds; only its `lib/tinyusb` submodule is needed |
| `mbedtls` | 3.6.8 | SHA-256 and ECDSA P-256 verification in the default crypto backend |
| `esp-idf` | v5.5.6 | ESP32-S3 builds, flash/boot support, ROM SHA, micro-ecc and application Mbed TLS |
| `tinyusb` | 0.19.0 | ESP32-S3 native USB MSC; independent of Pico SDK's own TinyUSB pin |

```sh
git submodule update --init firmware/lib/pico-sdk firmware/lib/mbedtls
git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb
```

Mbed TLS's own `framework` submodule is not needed.

For ESP32-S3, initialize ESP-IDF's nested dependencies and the standalone USB
stack, then install the toolchain before building:

```sh
git submodule update --init --recursive firmware/lib/esp-idf
git submodule update --init firmware/lib/tinyusb
firmware/lib/esp-idf/install.sh esp32s3
```
