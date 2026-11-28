# SUF2 firmware

> **Status: prototype.** Built and tested on the host (unit tests and a flash
> simulation) but not yet tried on real boards. Do not ship it in products.

This folder holds the device side of SUF2: a portable verifier library, and a
USB loaders for Raspberry Pi RP2040/RP2350 and an experimental ESP32-S3 integration.

```
firmware/
├── suf2/                 portable C library: receive, verify, boot-check
│   ├── include/suf2/     firmware-facing API and porting contracts
│   ├── src/              verifier, staged installer and virtual FAT16 drive (no heap or platform code)
│   └── tests/            Ceedling unit tests and native crypto tests
├── boards/
│   ├── rp2/              one CMake project for both Raspberry Pi chips
│   │   ├── suf2-boot/    bootloader, app helper, crypto port and simulation tests
│   │   └── blink/        example application
│   └── esp32s3/          shared ESP-IDF configuration and partition layout
│       ├── suf2-boot/    custom second-stage bootloader, USB recovery app and ports
│       └── blink/        native ESP-IDF example application
└── lib/                  git submodules: pico-sdk, mbedtls, esp-idf, tinyusb
```

## Getting the libraries

The libraries are git submodules, pinned to release tags (pico-sdk 2.3.1,
Mbed TLS 3.6.8). Only TinyUSB is needed from the SDK's own submodules:

```sh
git submodule update --init firmware/lib/pico-sdk firmware/lib/mbedtls
git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb
```

You also need CMake 3.13 or newer, Ninja or Make, Python 3 with
`cryptography`, a host C compiler for the tests and `arm-none-eabi-gcc` for
the RP2 boards. ESP32-S3 setup is described below. Ceedling unit tests also need Ruby.

## The library

`suf2.h` defines the receiver/verifier API. You give it a staging backend (write a
256-byte chunk, read bytes back) and a policy (product ID, the one load
address the device accepts, the largest image, the trusted keys). Then:

1. Feed every 512-byte sector the host writes to `suf2_session_write`. It
   ignores anything that is not a SUF2 block.
2. When the session is `SUF2_STATE_COMPLETE`, call `suf2_verify_candidate`.
   It runs the checks in the order the [specification](../docs/spec/suf2-specification.md)
   requires.
3. Just before installing, call `suf2_verify_staged` again, and after
   installing, `suf2_verify_boot` on the installed copy. Run
   `suf2_verify_boot` on every boot too.

The public headers in `suf2/include/suf2/` define the firmware-facing API
(`suf2.h`, including storage callbacks) and the porting contract
(`suf2_crypto.h`, SHA-256, P-256 verification and streaming AES-GCM). Two reusable helpers also
have public APIs:

- [suf2_store.h](suf2/include/suf2/suf2_store.h): staged NOR-flash installation,
  retained-manifest journal and commit record. It supports 4 KiB erase sectors
  and 256-byte programming units; provide read, erase and verified-program
  callbacks, disjoint region offsets, and an erase bitmap. Initialization
  validates the layout. The caller owns all buffers and state.
- [suf2_disk.h](suf2/include/suf2/suf2_disk.h): virtual FAT16 drive with
  `SUF2INFO.TXT` and `STATUS.TXT`. Provide text renderers and a complete-sector
  receive callback. The USB stack stays outside the library.

These headers contain no platform, TinyUSB or Mbed TLS headers. Both helpers
must be initialized before use, and calls on each context must be serialized.
Partial disk writes must arrive in order without interleaving.

The core's embedded CMake target has no crypto implementation or Mbed TLS
dependency. The [RP2 example port](boards/rp2/suf2-boot/port/README.md) owns the Mbed TLS
backend, its minimal configuration, and its build wiring. Another platform
provides the crypto functions and the storage callbacks in its own
integration. Native tests reuse the RP2 example's crypto port.

The library requires signatures for all packages. With a provisioned AES-256 key,
it also accepts encrypted packages: signature, stored digest, GCM tag and plaintext
digest must all pass before installation changes flash. Missing keys return
`SUF2_E_NO_DECRYPTION_KEY`; an invalid tag returns `SUF2_E_GCM_TAG`.
The installer keeps ciphertext staging unchanged and decrypts in a second pass
into the application slot, then boot-verifies and commits. Interrupted installs
recheck staging before resuming. Every boot checks the signature and installed
plaintext digest without requiring an AES key. This is transport encryption,
not encryption at rest; the host packers remain signing-only.

### Unit tests with Ceedling

The [Ceedling project](suf2/project.yml) uses Unity assertions and CMock for
the crypto contract. It runs on the host without Pico SDK, Mbed TLS, hardware,
or signing keys. Ceedling is intentionally unpinned; there is no Bundler setup.

Install Ruby 3.3, then install or update to the latest Ceedling release:

```sh
gem install ceedling --no-document
```

The runner uses the latest installed Ceedling gem through Ruby, without requiring
the `ceedling` executable on `PATH`. Re-run the install command to update locally;
CI installs the latest release on each run. Test runs do not automatically
download or update dependencies.

From the repository root:

```sh
tools/firmware/run_unit_tests.sh                 # all unit tests
tools/firmware/run_unit_tests.sh test:suf2_store  # one module
tools/firmware/run_unit_tests.sh clobber         # clear generated test outputs
tools/firmware/build.sh --test-only              # Ceedling + native + simulation
```

Tests are in `suf2/tests/unit/`, and generated mocks, executables and JUnit
reports go into ignored `build/ceedling-suf2/`. The tests exercise manifest
parsing, crypto-error propagation and cleanup, storage bounds and erase
tracking, and virtual-drive transfers.

CMock does not verify cryptographic algorithms: the native tests below use
the real Mbed TLS backend, and the RP2 simulation checks installation and
power-loss recovery. All three suites run in firmware CI; failures are not
silently skipped.

### Native crypto tests

```sh
cmake -S firmware/suf2 -B build/suf2-native -G Ninja
cmake --build build/suf2-native
ctest --test-dir build/suf2-native --output-on-failure
```

## The RP2 loader

### Building

The quick way:

```sh
tools/firmware/build.sh rp2040          # or rp2350
tools/firmware/build.sh rp2350 --test   # also run the library and simulation tests
tools/firmware/build.sh --help
```

It fetches missing submodules, creates local test keys if there are none,
and signs the example app with them. By hand, the same build is:

```sh
python3 test-keys/generate-test-keys.py        # once: local test keys
cmake -S firmware/boards/rp2 -B build/rp2040 -G Ninja -DSUF2_BOARD=rp2040 \
      -DSUF2_SIGNING_KEY=$PWD/test-keys/local/signing-private.pem
cmake --build build/rp2040
```

For RP2350, use the same source folder with `-DSUF2_BOARD=rp2350` and
`-B build/rp2350`. Keep separate build directories for the two chips.
The script accepts `rp2040`, `rp2350`, `esp32s3`, or `all`; `all` includes
ESP32-S3 and requires its installed SDK tools. After upgrading from the old RP2
source folders, use `--clean` on each selected board to recreate its build cache.
The build produces:

| File | What it is |
| --- | --- |
| `suf2_loader.uf2` | the loader, for the chip's own BOOTSEL drive |
| `suf2_loader.elf` / `.bin` / `.hex` | the same, for a debug probe |
| `blink/blink.suf2` | the example app, signed with your test key |

The first configure downloads and builds `picotool`, which the SDK needs to
make `.uf2` files. Set `-DSUF2_LOADER_UF2=OFF` to skip it, or install
picotool 2.3 yourself.

### Trying it

1. Hold BOOTSEL, plug the board in and copy `suf2_loader.uf2` to the RPI-RP2
   (or RP2350) drive.
2. With no application installed, the loader shows a drive called
   **SUF2BOOT**. `SUF2INFO.TXT` lists the product ID, the application address
   and the trusted key IDs. `STATUS.TXT` shows progress and the last error.
3. Copy `blink.suf2` to SUF2BOOT. The loader checks it, installs it, checks
   the installed copy and reboots into it. The LED blinks.
4. Open the board's USB serial port and type `u`. The app reboots into the
   loader, ready for the next update.

[tools/firmware/flash.sh](../tools/firmware/flash.sh) does steps 1 and 3 for you. It waits for the drive to
appear, matching its exact label, and copies the file:

```sh
tools/firmware/flash.sh rp2040 --blink                  # loader by BOOTSEL drive, then blink
tools/firmware/flash.sh rp2350 --method swd --blink     # loader through a Debug Probe (OpenOCD)
tools/firmware/flash.sh rp2040 --method picotool        # loader with picotool
tools/firmware/flash.sh --app-only my_app.suf2          # app only, loader already running
tools/firmware/flash.sh rp2040 --blink --dry-run        # show the commands, write nothing
```

If a package is refused, the LED double-flashes and the drive reappears after
about a second. `STATUS.TXT` says why.

### Keys and product ID

Development builds trust `test-keys/local/signing-public.pem` if you have
generated one. Otherwise they trust the example key in `test-keys/examples`,
whose private key is not published, so nothing will install.

| CMake option | Default | Meaning |
| --- | --- | --- |
| `SUF2_TRUSTED_KEYS` | local test key | `;`-separated PEM public keys or X.509 certificates |
| `SUF2_DECRYPTION_KEYS` | local AES JWK if present, otherwise empty | `;`-separated AES-256 octet JWKs; explicit empty value provisions no AES keys |
| `SUF2_PRODUCT_ID` | ASCII `SUF2RP2040` / `SUF2RP2350` | 32 hex digits; must match the packer's `--product-id` |
| `SUF2_SIGNING_KEY` | unset | private key used to pack the example app at build time |
| `SUF2_PRODUCTION` | `OFF` | refuse test keys and the default product ID |
| `SUF2_LOADER_SIZE` | `0x10000` | flash reserved for the loader |
| `SUF2_FLASH_SIZE` | 2 MB / 4 MB | flash size of your board |
| `SUF2_USB_VID`, `SUF2_USB_PID` | `0x1209`, `0x0001` | the pid.codes test IDs; use your own for products |

With `SUF2_PRODUCTION=ON` the build stops if a key comes from `test-keys` or legacy `host/shared`,
if a certificate says "TEST ONLY", if the product ID is the default, or if
`SUF2_SIGNING_KEY` is set. Sign releases offline with the host tools.

`tools/firmware/suf2_keys_header.py` turns the keys into the C table the
loader is built with.
`tools/firmware/suf2_aes_keys_header.py` builds a separate owner-only AES header.
Signing private keys are never compiled into the loader. The RP2 loader reserves
4 KiB of stack for the verification/decryption paths and retains its 64 KiB flash region.

For example, select decryption keys with:

```sh
tools/firmware/build.sh rp2040 -- -DSUF2_DECRYPTION_KEYS=/protected/device-key.jwk
tools/firmware/build.sh esp32s3 -- -DSUF2_DECRYPTION_KEYS=/protected/device-key.jwk
```

Use [test-keys/](../test-keys/README.md) only for development. Production defaults
never pick up test AES keys. Generated headers and loader/recovery binaries with
AES keys contain secrets: do not commit or publish them. CI published artifacts
explicitly use `-DSUF2_DECRYPTION_KEYS=`. Embedded AES keys require platform readout
and debug protection; this demo does not provision those protections.

### Flash layout

| Region | RP2040 (2 MB) | RP2350 (4 MB) |
| --- | --- | --- |
| Loader | `0x000000`, 64 KB | `0x000000`, 64 KB |
| META (journal and commit) | `0x010000`, 4 KB | `0x010000`, 4 KB |
| Application slot | `0x011000`, 988 KB | `0x011000`, 2012 KB |
| Staging | `0x109000`, 988 KB | `0x209000`, 2012 KB |

The application is linked to run at `0x10011000` (XIP address), and packages
must use that load address. Staging is the same size as the slot, so the
running application is never touched until a complete package has been
verified.

### What the loader does

At reset:

1. If an app asked for update mode (a one-shot flag in watchdog scratch
   registers), go to update mode.
2. If META holds a journal but no commit, an install was interrupted. Check
   staging against the journalled manifest again and finish the install.
3. If META holds a committed install, run `suf2_verify_boot` on the
   application and jump to it.
4. Otherwise go to update mode.

In update mode it is a USB drive. Each written sector goes to the SUF2
session and, if it is a new block, into staging. Once the package is complete
it is verified, then installed:

1. verify staging again;
2. erase META and write the journal (the manifest);
3. copy staging into the application slot, sector by sector;
4. boot-verify the copy;
5. write the commit record and reboot.

A power cut during steps 3 to 5 is finished on the next boot. A cut during
step 2, which takes a few milliseconds, leaves no journal: the old app no
longer boots, the loader stays in update mode, and you copy the file again.
The loader itself is never written.

The RP2 `flash_store.c` now supplies only hardware flash callbacks and wiring;
the journal and install flow live in `suf2/src/suf2_store.c`. Similarly,
`msc_disk.c` supplies TinyUSB callbacks, while `suf2/src/suf2_disk.c` renders
the volume and routes complete sectors. Board-specific USB descriptors,
boot-vector checks, update-request handling and the main loop stay in RP2.

The flash simulation in `boards/rp2/suf2-boot/tests` runs these adapters and the reusable
storage/drive code against simulated NOR flash, including a power cut at every
install step:

```sh
cmake -S firmware/boards/rp2/suf2-boot/tests -B build/rp2-sim -G Ninja
cmake --build build/rp2-sim
ctest --test-dir build/rp2-sim --output-on-failure
```

### Writing your own application

In your CMake project, after `suf2_rp2.cmake` is included:

```cmake
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE pico_stdlib)
suf2_rp2_add_app(my_app)
```

`suf2_rp2_add_app` links the app to the application slot and adds
`suf2_app.h`. Call `suf2_app_request_update()` to reboot into update mode.
Pack the ELF with any host tool, using the same product ID and
`--base 0x10011000`:

```sh
suf2 pack --input my_app.elf --output my_app.suf2 --key release-private.pem \
          --product-id <32 hex digits> --base 0x10011000
```

On RP2040 the slot starts with the app's 256-byte boot2 copy, and the
vector table is at `0x10011100`. On RP2350 the vector table is at the start
of the slot.

## The ESP32-S3 integration

See the [ESP32-S3 board guide](boards/esp32s3/README.md) for SDK setup, partition
layout, native USB wiring, keys, signing and recovery:

```sh
git submodule update --init --recursive firmware/lib/esp-idf
git submodule update --init firmware/lib/tinyusb
firmware/lib/esp-idf/install.sh esp32s3
tools/firmware/build.sh esp32s3
tools/firmware/flash.sh esp32s3 --port /dev/ttyUSB0 --blink --dry-run
```

It has separate `suf2-boot/` and `blink/` ESP-IDF projects. The custom second-stage
bootloader verifies committed SUF2 metadata and the installed image on every
boot, then exposes only one authorized partition to IDF's fallback search.
The recovery application reuses the portable store/disk and the RP2 Mbed TLS
backend source; the bootloader uses ROM SHA and micro-ecc.

This is an experimental native-container profile: sign the complete ESP-IDF
application BIN with execution-window base `0x42000000`, not a physical partition
offset or flat ELF/HEX conversion. It is not a flat-image conformance claim.
Never use the blink project's default flash command to replace the loader.

## Security limits

Read these before relying on the loader.

- **RP2040 has no secure boot.** Anyone with BOOTSEL, SWD or the flash chip
  can replace the loader and its keys. The loader gives authenticated updates
  and tamper detection, not protection against physical access.
- **RP2350 secure boot is not configured.** The loader runs in Arm secure
  mode, but nothing makes the ROM check the loader, and BOOTSEL stays open.
  Locking a device needs OTP programming, which is irreversible; it is your
  decision and is not automated here. See
  [platform integration](../docs/implementers/platform-integration.md).
- **Signed only.** Encrypted packages are refused.
- **ESP32-S3 hardware security is not configured.** No eFuses are burned. ROM
  download, debug and flash access can replace the bootstrap chain and keys;
  the build rejects automatic provisioning and validation-bypass configurations.
- **No anti-rollback.** An older, correctly signed package installs.
- **Test USB IDs.** The default VID/PID are for testing only.

## License

BSD-3-Clause. pico-sdk and TinyUSB are BSD/MIT licensed; Mbed TLS is
Apache-2.0 or GPL-2.0-or-later. ESP-IDF is Apache-2.0 and includes components
with their own licenses, including BSD-2-Clause micro-ecc.
