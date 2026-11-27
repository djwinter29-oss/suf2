# Why SUF2?

UF2 makes firmware easy to copy onto many microcontrollers. It does not say whether an update was approved by the device maker. SUF2 is a draft profile for adding that check to a UF2 workflow.

## When SUF2 may fit

SUF2 may suit a project that wants to keep distributing UF2 files and share one set of package and receiver rules across its tools and devices. It assumes the platform already has, or will provide, a trusted key and a loader that enforces verification.

If a project already uses a secure-update system that meets its needs, changing formats just to use SUF2 may not be worthwhile. SUF2 is also not a complete bootloader or recovery solution.

## What SUF2 adds

A SUF2 package includes a signed manifest: a fixed-size description of the firmware, including its cryptographic hashes, product identity and intended execution address. The device checks the signature using a key it already trusts, then checks the firmware against the signed hash. Authentication is required; encryption is not.

Optional AES-256-GCM encryption hides firmware contents while the package is in transit. The installed image is plaintext. The manifest remains visible, including the plaintext hash, which can be compared against a known or guessed image.

The profile also defines receiver behavior for common UF2 transfer conditions: blocks may arrive out of order or more than once, and unrelated blocks may be mixed into a transfer. This gives package producers and compatible receivers shared rules to follow.

## Why use UF2?

UF2 uses 512-byte blocks and is familiar in microcontroller workflows, including copying a file to a device that presents itself as a drive. Keeping that block format means existing UF2-oriented workflows can be retained, while SUF2 defines what a compatible device must verify.

An existing UF2 bootloader will not enforce SUF2 just because a file contains SUF2 blocks. A compatible receiver must recognize the profile, verify the signature and apply the device's policy. SUF2 marks its blocks with UF2's `not main flash` flag as a compatibility hint; older bootloaders may ignore it.

## How SUF2 relates to other projects

These projects solve related but different parts of firmware delivery and boot security. They are examples, not a ranking, and their capabilities depend on platform and configuration.

| Project | What it provides | How it relates to SUF2 |
| --- | --- | --- |
| [UF2](https://github.com/microsoft/uf2) | The block format and file-based update workflow. | SUF2 uses UF2 blocks and adds a signed package format and receiver rules. |
| [TinyUF2](https://github.com/adafruit/tinyuf2) | UF2 bootloader workflows for supported microcontrollers. | TinyUF2 addresses the device-side UF2 workflow. A device would need SUF2-aware verification to accept SUF2 packages securely. |
| [MCUboot](https://github.com/mcu-tools/mcuboot) | A secure-boot and update framework with its own image format and host tools. | MCUboot is a broader boot/update framework. SUF2 instead focuses on authenticated packages carried as UF2; projects already using MCUboot should weigh the value of keeping their existing format and tooling. |
| [wolfBoot](https://github.com/wolfSSL/wolfBoot) | A secure-boot and firmware-update solution for supported platforms. | Like MCUboot, it addresses boot and update behavior. SUF2's scope is the UF2 package and receiver contract, not a replacement bootloader. |
| [Raspberry Pi Pico encrypted-bootloader example](https://github.com/raspberrypi/pico-examples/tree/master/bootloaders/encrypted) | A signed and encrypted UF2 example for RP2350 using the chip's ROM secure-boot facilities. | It demonstrates a platform-specific approach. SUF2 aims to describe a package profile that can be used across platforms, with boot enforcement still supplied by each platform. |

SUF2's intended contribution is this shared UF2 package-and-receiver contract: required signature verification, optional encryption in transit and defined handling of transfer behavior. It does not claim that these capabilities are unique.

## Why a fixed manifest?

The v0.1 draft supports one image and fixes the manifest at 256 bytes so it fits in one UF2 block. It uses one signature scheme and fixed algorithms, rather than allowing each package to select them. This keeps the format constrained, but it does not make the package smaller: each 512-byte UF2 block carries 256 bytes of image content. A large package is therefore about twice the image size, plus one manifest block and padding.

The trade-off is a simpler, narrower package contract in exchange for a new format and its own producer and receiver support. v0.1 has no multi-image support or algorithm negotiation; changing the layout or algorithms requires a new format version. Wrapping an existing image format could reuse its tooling, while still needing rules for carrying and receiving it over UF2. Projects with a working secure-update format may reasonably prefer to keep it.

## Limits

- SUF2 uses established cryptographic algorithms; it does not introduce a new one.
- Encryption protects image contents in transit, not while stored on the device.
- A valid signature does not prevent replay of an older signed image. v0.1 has no anti-rollback counter.
- Package authentication is not the same as secure boot. The platform must protect the loader and trusted key, enforce its own policy, and ensure the bytes checked are the bytes that run.
- UF2 metadata and the manifest remain visible, even when image contents are encrypted.

## Project status

SUF2 is a design-stage proposal. The format and receiver behavior are drafts and have not received an independent security review. See the [specification](suf2-specification.md), [security design](security/security-design.md), [design guidelines](security/design-guidelines.md) and [conformance catalogue](conformance.md) for details.
