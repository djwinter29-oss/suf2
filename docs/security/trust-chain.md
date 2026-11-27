# SUF2 trust chain and platform requirements

A valid signature proves who made a file. It does not by itself make a device enforce anything. Enforcement depends on a chain of trust that the platform, not the SUF2 library, provides. This document states what each platform integration MUST document and provide.

## 1. The chain

```
immutable or locked root  ->  SUF2 loader  ->  trusted public key  ->  signed manifest  ->  application bytes
```

Each link is trusted only because the previous one verified it, or because it cannot be modified. If an attacker can replace any link, everything after it is untrusted.

## 2. Requirements on a platform integration

An integration that claims SUF2 enforcement MUST:

1. **Protect the loader.** The loader that runs first after reset cannot be replaced by the attacker's capability set (see the threat model). Typically this is ROM-verified, lock-bit-protected, or in write-protected flash.
2. **Anchor the key.** The public key, or a hash of it, is held in OTP, in the protected loader image, or in other write-protected storage. Private keys never reach the device.
3. **Leave no bypass boot path.** Debug access, alternate boot sources, recovery modes and USB boot ROM modes either cannot start unverified code or are accepted and documented as bypasses.
4. **Verify on every boot.** Verify the retained signed manifest and the actual installed bytes before executing the application.
5. **Control execution memory.** Verified code is the code that runs: staging and application memory is not writable by untrusted code between verification and execution where the hardware allows it.
6. **Own the destination.** Flash addresses and board identity come from local configuration, never from the package.
7. **Document the result.** State exactly which attacker capabilities from the threat model are and are not defeated.

## 3. Claim levels

Integrations SHOULD use one of these labels.

| Level | Meaning |
| --- | --- |
| **Authenticated update** | Updates through the loader must be signed. A physical attacker can still replace the loader or image. |
| **Verified boot** | As above, plus a locked loader and anchored key, so unsigned flash contents do not run. |
| **Verified boot with confidentiality at rest** | As above, plus on-the-fly decryption or equivalent. Not defined by v0.1. |

## 4. Platform notes

### RP2040

- No ROM secure boot. BOOTSEL mass storage, SWD debug and external-flash replacement can overwrite the loader and key.
- Achievable level: **authenticated update**, plus tamper detection at boot against accidental or casual modification.
- The bootstrap `secure-pico.uf2` installs the loader through stock BOOTSEL. This does not make enforcement immutable.

### RP2350

- ROM secure boot can verify the boot image with keys anchored in OTP. This uses its own signature scheme; SUF2 signatures are separate and are not interpreted by ROM.
- Achievable level: **verified boot** if the loader is ROM-verified and boot paths are locked as the RP2350 documentation describes.
- OTP programming is irreversible. The demo MUST NOT automate it. Provisioning is a documented, deliberate manual process.

### Other platforms (future)

ESP32 (secure boot, flash encryption), STM32 (readout protection, secure boot) and similar parts each need their own section covering the seven requirements above.

## 5. Integration checklist template

For each platform, document:

- Boot sequence and which stage is the first to run.
- Where the loader lives and what protects it.
- Where the public key or hash lives and what protects it.
- Debug and recovery paths, and whether each is disabled, locked or open.
- Flash layout: loader, metadata, application, staging, journal.
- Maximum application size and how it was measured.
- Claim level and the attacker capabilities it does and does not defeat.

## 6. Key lifecycle (guidance)

- Generate signing keys offline; keep private keys out of the repository and out of command-line arguments.
- Provision only public keys on devices.
- Using multiple provisioned public keys is allowed. Signed rotation and revocation are deferred beyond v0.1.
- Test keys are labelled, and production configurations MUST NOT accept them.
