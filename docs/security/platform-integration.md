# SUF2 platform integration notes

These are preliminary notes for applying the generic [secure boot trust-chain requirements](../reference/trust-chain.md) to candidate SUF2 demo platforms. They are not evidence of a verified-boot claim. Each claim must be checked against the selected chip documentation, actual device configuration and integration tests.

## RP2040

- The RP2040 has no ROM secure-boot mechanism. BOOTSEL, SWD and external-flash access can allow replacement of the loader or its trust material.
- A demo can require signed updates and detect changes to an image at boot, provided the loader itself is intact. This does not protect against an attacker who can replace the loader or alter the boot path.
- The bootstrap `secure-pico.uf2` installs the loader through stock BOOTSEL; that installation does not make the loader immutable.

## RP2350

- ROM secure boot can authenticate a boot image using keys anchored in OTP. That is a platform-level mechanism with a signature scheme separate from SUF2's package signature.
- A verified-boot claim for a SUF2 application also depends on a trusted verifier checking the installed image on every boot, protecting the trust anchor and boot policy against the stated attacker, and ensuring every boot path that attacker can reach provides equivalent authorization and integrity guarantees or is disabled. The checked bytes must remain unchanged until execution. ROM verification alone does not establish these properties.
- OTP programming is irreversible. The demo MUST NOT automate it. Provisioning must be a documented, deliberate process.

## Other platforms

ESP32, STM32 and other candidate platforms need their own evidence against the requirements in [trust-chain.md](../reference/trust-chain.md#2-requirements-for-a-verified-boot-claim). Product-specific configuration matters as much as the chip's available security features.
