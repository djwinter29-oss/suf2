# SUF2 platform integration notes

These are preliminary notes for applying the generic [secure boot trust-chain requirements](../reference/trust-chain.md) to candidate SUF2 demo platforms. They are not evidence of a verified-boot claim. Each claim must be checked against the selected chip documentation, actual device configuration and integration tests.

Reference integrations are in [firmware/](../../firmware/README.md). They show the update and boot checks; they do not provision chip security features.

## RP2040

- The RP2040 has no ROM secure-boot mechanism. BOOTSEL, SWD and external-flash access can allow replacement of the loader or its trust material.
- A demo can require signed updates and detect changes to an image at boot, provided the loader itself is intact. This does not protect against an attacker who can replace the loader or alter the boot path.
- The loader (`suf2_loader.uf2`) is installed through stock BOOTSEL. That does not make the loader immutable, and BOOTSEL stays available afterwards.

## RP2350

- ROM secure boot can authenticate a boot image using keys anchored in OTP. That is a platform-level mechanism with a signature scheme separate from SUF2's package signature.
- A verified-boot claim for a SUF2 application also depends on a trusted verifier checking the installed image on every boot, protecting the trust anchor and boot policy against the stated attacker, and ensuring every boot path that attacker can reach provides equivalent authorization and integrity guarantees or is disabled. The checked bytes must remain unchanged until execution. ROM verification alone does not establish these properties.
- OTP programming is irreversible. The demo MUST NOT automate it. Provisioning must be a documented, deliberate process.

## ESP32-S3

- The [ESP-IDF demo](../../firmware/boards/esp32s3/README.md) has a custom
  second-stage bootloader and a separate USB recovery application. Only a
  committed image that passes SUF2 boot verification and native image validation,
  or the trusted recovery application, is made visible to IDF's loader.
  Other factory/test/OTA fallback entries are removed; deep-sleep boot does not
  skip SUF2 verification.
- The full native application BIN, including signed segment headers, is the
  authenticated payload. The manifest base identifies the IROM execution window
  `0x42000000`, not the physical partition offset. This is an experimental native
  container profile requiring specification/conformance review, not a claim of
  flat-image conformance.
- Secure boot and flash encryption are not enabled or provisioned; eFuse burning
  is irreversible and is not automated. ROM download, debugging and external
  flash remain outside the demo's package-authentication guarantee.
- Cross-builds and host package checks do not exercise the ROM crypto port,
  native USB, physical flash writes or reset/power-loss behavior. Those require
  hardware evidence before making a verified-boot claim.

## Other platforms

Other ESP32 variants, STM32 and other candidate platforms need their own evidence against the requirements in [trust-chain.md](../reference/trust-chain.md#2-requirements-for-a-verified-boot-claim). Product-specific configuration matters as much as the chip's available security features.
