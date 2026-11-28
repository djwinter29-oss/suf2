# SUF2 firmware

> **Status: prototype.** Built and tested on the host (unit tests and a flash
> simulation) but not yet tried on real boards. Do not ship it in products.

This folder holds the device side of SUF2: a portable verifier library, and a
USB loader for the Raspberry Pi RP2040 and RP2350 that uses it.

```
firmware/
├── suf2/                 portable C library: receive, verify, boot-check
│   ├── include/suf2/     public API (suf2.h) and crypto backend interface
│   ├── src/              the library (no heap, no platform code)
│   ├── port/             Mbed TLS crypto backend and its config
│   └── tests/            native tests, driven from Python
├── boards/
│   ├── rp2-common/       loader, app helper, example app and CMake shared by both chips
│   ├── rp2040/           CMake project for RP2040 (Pico)
│   └── rp2350/           CMake project for RP2350 (Pico 2, Arm secure mode)
└── lib/                  git submodules: pico-sdk, mbedtls
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
the boards.

## The library

`suf2.h` is the whole interface. You give it a staging backend (write a
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

Crypto goes through `suf2_crypto.h` (SHA-256 and P-256 verify). The default
backend uses Mbed TLS with a minimal config; swap it for a hardware
accelerator by providing those four functions.

The library accepts signed packages only. An encrypted package passes the
signature check and is then refused with `SUF2_E_NO_DECRYPTION_KEY`, because
no decryption backend exists yet.

Native tests (AddressSanitizer and UBSan on):

```sh
cmake -S firmware/suf2 -B build/suf2-native -G Ninja
cmake --build build/suf2-native
ctest --test-dir build/suf2-native --output-on-failure
```

## The RP2 loader

### Building

The quick way:

```sh
firmware/build.sh rp2040          # or rp2350, or all
firmware/build.sh all --test      # also run the library and simulation tests
firmware/build.sh --help
```

It fetches missing submodules, creates local test keys if there are none,
and signs the example app with them. By hand, the same build is:

```sh
python3 host/shared/generate-test-keys.py        # once: local test keys
cmake -S firmware/boards/rp2040 -B build/rp2040 -G Ninja \
      -DSUF2_SIGNING_KEY=$PWD/host/shared/local/signing-private.pem
cmake --build build/rp2040
```

Use `firmware/boards/rp2350` and `build/rp2350` for the RP2350. The build
produces:

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

`firmware/flash.sh` does steps 1 and 3 for you. It waits for the drive to
appear, matching its exact label, and copies the file:

```sh
firmware/flash.sh rp2040 --blink                  # loader by BOOTSEL drive, then blink
firmware/flash.sh rp2350 --method swd --blink     # loader through a Debug Probe (OpenOCD)
firmware/flash.sh rp2040 --method picotool        # loader with picotool
firmware/flash.sh --app-only my_app.suf2          # app only, loader already running
firmware/flash.sh rp2040 --blink --dry-run        # show the commands, write nothing
```

If a package is refused, the LED double-flashes and the drive reappears after
about a second. `STATUS.TXT` says why.

### Keys and product ID

Development builds trust `host/shared/local/signing-public.pem` if you have
generated one. Otherwise they trust the example key in `host/shared/examples`,
whose private key is not published, so nothing will install.

| CMake option | Default | Meaning |
| --- | --- | --- |
| `SUF2_TRUSTED_KEYS` | local test key | `;`-separated PEM public keys or X.509 certificates |
| `SUF2_PRODUCT_ID` | ASCII `SUF2RP2040` / `SUF2RP2350` | 32 hex digits; must match the packer's `--product-id` |
| `SUF2_SIGNING_KEY` | unset | private key used to pack the example app at build time |
| `SUF2_PRODUCTION` | `OFF` | refuse test keys and the default product ID |
| `SUF2_LOADER_SIZE` | `0x10000` | flash reserved for the loader |
| `SUF2_FLASH_SIZE` | 2 MB / 4 MB | flash size of your board |
| `SUF2_USB_VID`, `SUF2_USB_PID` | `0x1209`, `0x0001` | the pid.codes test IDs; use your own for products |

With `SUF2_PRODUCTION=ON` the build stops if a key comes from `host/shared`,
if a certificate says "TEST ONLY", if the product ID is the default, or if
`SUF2_SIGNING_KEY` is set. Sign releases offline with the host tools.

`tools/firmware/suf2_keys_header.py` turns the keys into the C table the
loader is built with.

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

The flash simulation in `boards/rp2-common/tests` runs the real storage and
USB drive code against simulated NOR flash, including a power cut at every
install step:

```sh
cmake -S firmware/boards/rp2-common/tests -B build/rp2-sim -G Ninja
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
- **No anti-rollback.** An older, correctly signed package installs.
- **Test USB IDs.** The default VID/PID are for testing only.

## License

BSD-3-Clause. pico-sdk and TinyUSB are BSD/MIT licensed; Mbed TLS is
Apache-2.0 or GPL-2.0-or-later.
