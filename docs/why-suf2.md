# Why SUF2?

UF2 makes firmware easy to copy onto many microcontrollers, often by dragging a file onto a device that appears as a USB drive. A project may want to keep that update process while ensuring that only firmware approved by its maker can run. SUF2 is a draft profile for adding that check to UF2.

## When SUF2 may fit

SUF2 may suit a project that wants to keep distributing UF2 files and share one set of package and receiver rules across its tools and devices. It assumes the platform already has, or will provide, a trusted key and a loader that enforces verification.

If a project already uses a secure-update system that meets its needs, changing formats just to use SUF2 may not be worthwhile. SUF2 is also not a complete bootloader or recovery solution.

## What SUF2 adds

A SUF2 package includes a signed manifest: a fixed-size description of the firmware, including its hashes, product identity and intended execution address. The device checks the signature with a key it already trusts, then checks the firmware against the signed digest. Authentication is required; encryption is optional. A SUF2 receiver must not fall back to installing ordinary unsigned UF2 firmware.

Before every application start, the loader verifies the retained manifest's signature and checks that the installed firmware's hash matches the hash recorded in the manifest. The platform remains responsible for protecting the loader and trusted key, and for ensuring the bytes checked are the bytes that run.

Optional AES-256-GCM encryption hides the firmware contents in transit; the installed image is plaintext. The manifest remains visible, including its plaintext hash, which can confirm a known or guessed image.

The receiver rules cover blocks arriving out of order or more than once, as well as unrelated blocks mixed into a transfer.

## Why use UF2?

UF2 uses 512-byte blocks and is familiar in microcontroller workflows, including copying a file to a device that presents itself as a drive. Keeping that block format means existing UF2-oriented workflows can be retained, while SUF2 defines what a compatible device must verify.

An existing UF2 bootloader will not enforce SUF2 just because a file contains SUF2 blocks. A compatible receiver must recognize the profile, verify the signature and apply the device's policy. SUF2 marks its blocks with UF2's `not main flash` flag as a compatibility hint; older bootloaders may ignore it.

## How SUF2 relates to other projects

These projects solve related but different parts of firmware delivery and boot security. They are examples, not a ranking, and their capabilities depend on platform and configuration.

| Project | What it provides | How it relates to SUF2 |
| --- | --- | --- |
| [UF2](https://github.com/microsoft/uf2) | The block format and file-based update workflow. | SUF2 uses UF2 blocks and adds a signed package format and receiver rules. |
| [TinyUF2](https://github.com/adafruit/tinyuf2) | UF2 bootloader workflows for supported microcontrollers. | TinyUF2 addresses the device-side UF2 workflow. A device would need SUF2-aware verification to accept SUF2 packages securely. |
| [MCUboot](https://github.com/mcu-tools/mcuboot) | A secure-boot and update framework with its own image format and host tools. | MCUboot defines an image format within its boot/update framework; SUF2 defines how authenticated firmware is packaged and received over UF2. |
| [wolfBoot](https://github.com/wolfSSL/wolfBoot) | A secure-boot and firmware-update solution for supported platforms. | wolfBoot supplies a bootloader and update framework; SUF2 defines the UF2 package and required checks, leaving bootloader implementation and recovery to the platform. |
| [Raspberry Pi Pico encrypted-bootloader example](https://github.com/raspberrypi/pico-examples/tree/master/bootloaders/encrypted) | A signed and encrypted UF2 example for RP2350 using the chip's ROM secure-boot facilities. | It demonstrates a platform-specific approach. SUF2 aims to describe a package profile that can be used across platforms, with boot enforcement still supplied by each platform. |

SUF2 defines a UF2 package, handling for out-of-order, duplicate and mixed blocks, and checks required before the application starts. It is a package and device-behavior profile, not a replacement bootloader.

## Why a fixed manifest?

SUF2 uses its own manifest rather than wrapping an existing signed-image format such as MCUboot's. MCUboot is a credible alternative; a UF2 profile could carry its image format and reuse its tools. SUF2 instead defines a dedicated manifest and receiver contract for the UF2 workflow. The manifest is 256 bytes—the same size as an image chunk—so the complete manifest fits in one UF2 block. v0.1 allows no optional metadata extensions. The trade-off is a dedicated fixed layout rather than reuse of an existing image format and its tooling.

The cost is limited flexibility: v0.1 supports one image and a fixed set of algorithms, and requires its own producer and receiver tooling. Each image block carries 256 bytes of image in a 512-byte UF2 block, so a large package is about twice the image size, plus a manifest block and padding. Changing the layout or algorithms requires a new format version.

## Limits

- Encryption protects image contents in transit, not while stored on the device.
- A valid signature does not prevent replay of an older signed image. v0.1 has no anti-rollback counter.
- Package authentication is not the same as secure boot. The platform must protect the loader and trusted key, enforce its own policy, and ensure the bytes checked are the bytes that run.
- UF2 metadata and the manifest remain visible, even when image contents are encrypted.

## Project status

SUF2 is a design-stage proposal. The format and receiver behavior are drafts and have not received an independent security review. See the [specification](spec/suf2-specification.md), [security design](security/security-design.md), [design guidelines](implementers/design-guidelines.md) and [conformance catalogue](spec/conformance.md) for details.
