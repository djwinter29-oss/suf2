# SUF2 related work and positioning

Notes gathered during project definition. Verify details against each project before quoting them.

## 1. Existing projects

| Project | License | Relevant capability |
| --- | --- | --- |
| [UF2](https://github.com/microsoft/uf2) | MIT | The block format and mass-storage update workflow. No authentication. |
| [TinyUF2](https://github.com/adafruit/tinyuf2) | MIT | UF2 mass-storage bootloader for several MCUs, including application-requested entry into UF2 mode. |
| [MCUboot](https://github.com/mcu-tools/mcuboot) | Apache-2.0 | Signed images, encrypted images (AES-CTR with key wrapping), security counter, `imgtool`. |
| [wolfBoot](https://github.com/wolfSSL/wolfBoot) | GPLv3 (commercial option) | Authenticated boot and update, encrypted partitions, an RP2350 port. |
| [pico-examples `bootloaders/encrypted`](https://github.com/raspberrypi/pico-examples/tree/master/bootloaders/encrypted) | BSD-3-Clause | Signed, encrypted UF2 on RP2350 using ROM secure boot and OTP keys. Not RP2040. |
| Small community projects | various | Signing helpers or secure-boot demos for Pico boards. Not a specification. |

## 2. What already exists

Signed firmware, encrypted firmware, per-boot verification and UF2 mass-storage receivers all exist separately. SUF2 does not claim a new cryptographic capability.

## 3. The gap SUF2 targets

- A written **profile**: how UF2 flags, family ID and payload carry an authenticated package, with mandatory authentication by design.
- An explicit **threat model and trust-chain guide** that states honestly what a UF2 workflow can and cannot protect.
- A small **portable receiver/verifier library** that hardens the UF2-specific hazards (arbitrary order, duplicates, partial writes, mixed files) so integrators do not redevelop them.
- A **quick-try demo** on Pico, including RP2040 where there is no ROM secure boot.

A repository search did not find a project that provides this combination. The search was not exhaustive, so uniqueness is not asserted.

## 4. Reuse versus a new format

| Option | Pros | Cons |
| --- | --- | --- |
| Carry an MCUboot image (header, TLVs, signature) inside UF2 records | Existing tooling, review history, key and image conventions | MCUboot's format is broader and more complex; its ecosystem assumes an MCUboot bootloader; UF2 hazards (ordering, mixing) still need a profile |
| New fixed manifest (current draft) | Small, auditable, one parser, tailored to UF2 | New format needs independent review; no existing tooling |
| Adopt wolfBoot | Mature, includes Pico 2 support | GPLv3 conflicts with this repository's BSD-3-Clause license for direct reuse |

**Current leaning:** keep the draft fixed manifest for v0.1 as the specification's subject, but decide before freezing whether wrapping an MCUboot image is a better fit. This is an open question in [spec.md](spec.md).

## 5. Licensing

The repository is BSD-3-Clause. MCUboot (Apache-2.0), TinyUF2 (MIT) and Mbed TLS (Apache-2.0 or GPL-2.0-or-later, selectable) are compatible dependencies. Do not copy GPLv3 code.

## 6. To verify before freezing

- Upstream UF2 semantics for the `not main flash` flag and family IDs, and how bootloaders treat unknown family IDs.
- The RP2040 and RP2350 ROM behavior when handed a UF2 file with an unknown family ID.
- Whether any existing project already defines a UF2-based mandatory-signature profile.
