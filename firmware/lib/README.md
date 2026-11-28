# Third-party libraries

Git submodules, pinned to release tags.

| Submodule | Version | Used for |
| --- | --- | --- |
| `pico-sdk` | 2.3.1 | RP2040/RP2350 builds; only its `lib/tinyusb` submodule is needed |
| `mbedtls` | 3.6.8 | SHA-256 and ECDSA P-256 verification in the default crypto backend |

```sh
git submodule update --init firmware/lib/pico-sdk firmware/lib/mbedtls
git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb
```

Mbed TLS's own `framework` submodule is not needed.
