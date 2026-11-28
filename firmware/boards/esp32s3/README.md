# ESP32-S3 reference integration

**Experimental, signing-only, not hardware-validated.** This is a demo, not a
provisioned secure-boot product. It does not enable secure boot, flash encryption,
disable ROM download/debug access, or burn eFuses.

The two projects are [suf2-boot](suf2-boot/README.md), containing the custom
second-stage bootloader and USB recovery application, and [blink](blink/README.md),
the signed example application. They reuse the portable receiver, installer and
virtual disk in [firmware/suf2](../../suf2/). Hardware and crypto adapters remain
in the board integration; no board unit-test suites are added.

## Setup and build

Requires an ESP32-S3 with at least 4 MB flash, CMake 3.16+, Ninja, Python with
`cryptography`, and the pinned ESP-IDF tools:

```sh
git submodule update --init --recursive firmware/lib/esp-idf
git submodule update --init firmware/lib/tinyusb
firmware/lib/esp-idf/install.sh esp32s3
tools/firmware/build.sh esp32s3
```

The runner exports ESP-IDF for its process, generates local development keys if
needed, builds both projects and signs `build/esp32s3/blink/blink.bin` into
`blink.suf2`. Build output, generated key/configuration headers and `sdkconfig`
remain in ignored `build/esp32s3/{suf2-boot,blink}/`. An existing `IDF_PATH` is
honored; the repository pins ESP-IDF v5.5.6 and standalone TinyUSB 0.19.0.
`build.sh all` now includes all three chips and therefore requires both SDKs.

`--no-sign`, `--clean`, `--key` and extra CMake arguments work as on RP2.
For a custom signing key, also pass the matching public key with
`-- -DSUF2_TRUSTED_KEYS=/absolute/path/public.pem`. `SUF2_PRODUCT_ID` is exactly
32 hex digits; the development value represents `SUF2ESP32S3` with zero padding.
`SUF2_PRODUCTION=ON` requires a non-default product ID, provisioned public keys
outside `host/shared`, and `--no-sign`; sign releases offline. This build guard
does not make the device secure or provision hardware security.

## Native image profile

Unlike RP2's flat XIP image, an ESP-IDF application BIN is a native segmented
container. This experimental integration signs the **complete BIN**, including
its entry point, segment headers, checksum and appended digest. Do not sign a
merged flash image, the ELF, or a flat ELF/HEX conversion for this board.

The manifest's `load_address` is **`0x42000000`, the IROM execution-mapping
window**, not physical partition offset `0x140000`. ESP-IDF interprets the signed
segment headers to map IROM/DROM and load RAM segments. SUF2 hashes the entire
container; it does not flatten, rebase, or discard any segments.

This native-container interpretation is a platform-specific experimental profile,
not a claim of flat-image conformance to the draft specification. The wire format
and host packers are unchanged. It needs specification/conformance review before
being promoted to a supported production profile.

For any host packer, use the same product ID and `--base 0x42000000`, with the
native application BIN as input:

```sh
tools/host/run_python.sh pack --input build/esp32s3/blink/blink.bin \
  --output my-blink.suf2 --key host/shared/local/signing-private.pem \
  --product-id 53554632455350333253330000000000 --base 0x42000000
```

## Flash layout

The fixed layout is in [partitions.csv](partitions.csv):

| Region | Physical flash offset | Size |
| --- | --- | --- |
| Custom second-stage bootloader | `0x000000` | up to `0x20000` |
| Partition table | `0x020000` | 4 KiB |
| NVS / PHY data | `0x021000` / `0x027000` | 24 / 4 KiB |
| SUF2 USB recovery application | `0x030000` | 1 MiB |
| META journal and commit | `0x130000` | 4 KiB |
| Authenticated application (`test` subtype) | `0x140000` | 1 MiB |
| Staging | `0x240000` | 1 MiB |

Both app partitions are 64 KiB aligned so IDF's native mapping is valid. No OTA
slots or OTA-selection metadata are used. Flash callbacks only erase/program
META, candidate and staging, and verify programmed bytes by reading them back.
The updater never overwrites the bootloader, partition table or recovery app.

## Flash and try

Connect UART for initial provisioning and logging, and native USB OTG
(GPIO19 D-, GPIO20 D+) for the SUF2BOOT drive. Use the board's USB OTG connector,
not a USB-UART-only connector. An external LED and resistor on GPIO2 are the
default blink output; the DevKit's addressable RGB LED is not used.

```sh
# Check commands first; this does not flash.
tools/firmware/flash.sh esp32s3 --port /dev/ttyUSB0 --blink --dry-run

# Initial provisioning, only when you explicitly intend to write hardware:
tools/firmware/flash.sh esp32s3 --port /dev/ttyUSB0 --blink

# Already in recovery mode:
tools/firmware/flash.sh esp32s3 --app-only build/esp32s3/blink/blink.suf2
```

The serial method flashes only the **recovery project's** bootloader, partition
table and recovery application, then optionally copies a signed update to the
native USB drive. **Never run `idf.py flash` in the blink project**: IDF's printed
default flash command would replace the recovery app and custom bootloader.

With no committed image, the loader exposes SUF2BOOT. Copy `blink.suf2`; the
updater verifies, journals, installs, verifies the installed bytes and commits.
The next boot checks the retained manifest and image again. Type `u` on the
blink UART console to request recovery on the next software reset.

The demo uses IDF's reserved eight-byte custom RTC FAST region for that one-shot
request, excluded from IDF's retained-state CRC. Both projects use the same
reservation and run their main task on CPU0. GPIO0/BOOT held at
reset selects the ROM downloader, not SUF2 recovery. Reflashing the recovery
project intentionally preserves META and the installed app: a valid committed
app still boots, so request recovery before copying another update.

On rejection the updater stays available and reports the error over UART and
`STATUS.TXT`. Eject the drive before retrying a failed copy. Interrupted installs
are reverified and resumed by the recovery application; an incomplete journal
requires a fresh copy. An older correctly signed application remains acceptable.

## Validation and limits

CI cross-builds both projects, checks partition/image sizes and flash arguments,
checks key-table consistency, verifies `blink.suf2` with the native SUF2 harness,
compares its authenticated payload to the complete BIN, checks native ESP image
metadata, and checks production-key rejection. Portable Ceedling and existing
native/RP2 simulation suites remain separate.

USB enumeration, UART recovery, physical flash writes, reset paths and power-cut
recovery on ESP32-S3 hardware are **unverified**. The ROM SHA/micro-ecc bootloader
port is cross-built, not exercised by the host Mbed TLS harness.

The recovery app and custom bootloader are trusted parts of the demo's bootstrap
boundary, installed by serial provisioning. Without deliberately provisioned
hardware secure boot and protected recovery/debug paths, an attacker can replace
them or their keys. Package authentication is not a hardware root of trust.
Encryption and anti-rollback are not implemented.
