# SUF2 (Secured UF2)

> **Status: draft design with prototype host tools and firmware.** RP2040/RP2350 and ESP32-S3 integrations are build/host-validated only, and nothing is frozen or security-reviewed. Do not use it to protect real products.

SUF2 is a profile of [UF2](https://github.com/microsoft/uf2) that makes authentication mandatory for firmware updates, with optional encryption of the update in transit.

Draft documents are in [docs/](docs/README.md): threat model, trust chain, specification, design guidelines, conformance catalogue and [why SUF2](docs/why-suf2.md).

## Host tools

[host/](host/README.md) contains Python and .NET command-line packers and a
browser dashboard deployable to GitHub Pages. Each converts BIN, HEX or ELF
firmware into a signed SUF2 package. Browser processing is local; firmware and
keys are not sent to a server. These first prototypes support signing only,
not optional encryption. [Shared examples](host/shared/README.md) document
PEM public keys and certificates and generate private test keys locally.

## Firmware

[firmware/](firmware/README.md) contains the portable C verifier, installer and
virtual drive, with RP2040/RP2350 Pico SDK loaders and an experimental
[ESP32-S3 ESP-IDF integration](firmware/boards/esp32s3/README.md). Crypto and hardware
ports are supplied by each integration. Libraries are git submodules in `firmware/lib`.

## Goals, in priority order

1. **Specification.** Define how a SUF2 package uses UF2 block headers, flags and family IDs, and what the payload carries (manifest, signature, optional encryption metadata). The spec also states the **threat model** and the **secure design rules**: what SUF2 protects against, what it does not, and which trust chain a platform must provide.
2. **Library.** A portable verifier/receiver in C, plus host tooling to build and sign packages. It exists so integrators don't have to re-develop the security-sensitive parts.
3. **Demo.** A Raspberry Pi Pico (RP2040 first) example so users can try signed updates quickly.

## Design direction (subject to change)

- UF2 block framing is preserved. A SUF2-aware receiver is required; legacy UF2 bootloaders cannot enforce SUF2.
- Signing is mandatory, using ECDSA P-256 / SHA-256. Encryption is optional (AES-256-GCM, pre-provisioned keys). Only established crypto libraries are used.
- Encryption in v0.1 is **transport-only**: the update is decrypted on install and the signed plaintext image is stored. Encryption at rest needs platform support (on-the-fly decryption) and is deferred.
- The signed manifest is kept with the installed image so that it can be verified on every boot, not only at update time.
- No anti-rollback in v0.1: an older, correctly signed image is accepted.

## Trust boundary

An authenticated file is not an enforced update. Enforcement requires a platform root of trust and a protected bootloader and policy. The generic model is in [trust-chain.md](docs/reference/trust-chain.md); candidate board-specific notes are in [platform-integration.md](docs/implementers/platform-integration.md).

The project aims to provide the specification and reusable verification code. Each platform integration documents its own chain of trust:

- **RP2040:** no hardware secure boot, so the demo gives authenticated updating and tamper detection, not protection against someone with flash or BOOTSEL access.
- **RP2350 and others:** can anchor trust in hardware (e.g. OTP keys). Such configuration is platform-specific and often irreversible, so the demo never automates it.

## Non-goals (v0.1)

Anti-rollback, encryption at rest, a new cryptographic primitive, and a hardware root of trust provided by the library itself.

## Why a UF2 profile?

[MCUboot](https://github.com/mcu-tools/mcuboot), [wolfBoot](https://github.com/wolfSSL/wolfBoot) and [TinyUF2](https://github.com/adafruit/tinyuf2) address related parts of secure firmware updates. SUF2 focuses on an authenticated package profile for the UF2 workflow. See [Why SUF2?](docs/why-suf2.md) for the motivation, trade-offs and limits.

## License

BSD-3-Clause. See [LICENSE](LICENSE).
