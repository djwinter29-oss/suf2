# SUF2 security design

This page maps each threat in the [threat model](../reference/threat-model.md) to SUF2 v0.1's design, including what it deliberately leaves to the platform, integrator or a future version. The generic boot trust model is in [trust-chain.md](../reference/trust-chain.md); preliminary board notes are in [platform-integration.md](../implementers/platform-integration.md). The wire format is in [suf2-specification.md](../spec/suf2-specification.md), implementation guidance is in [design-guidelines.md](../implementers/design-guidelines.md), and planned tests are in [conformance.md](../spec/conformance.md).

## What SUF2 v0.1 covers

SUF2 is a firmware package format and bootloader verification profile. Version 0.1:

- **Mandatory authentication:** every package carries an ECDSA P-256 / SHA-256 signature over a manifest, which holds the image digests and product identity. There is no unsigned mode.
- **Optional transit encryption:** AES-256-GCM protects image contents on the way to the device. Installed firmware is stored in plaintext.
- **UF2-block transport:** packages are carried as UF2 blocks. An integration may deliver those blocks over USB mass storage, serial, network or radio; the delivery protocol itself is not specified.
- **Per-boot verification:** the bootloader keeps the signed manifest and re-verifies the installed image against it at every boot.
- **Root of trust from the platform:** SUF2 does not protect the bootloader or trust anchor itself; the platform must satisfy the generic requirements in [trust-chain.md](../reference/trust-chain.md).

Not in v0.1: anti-rollback, key revocation and rotation, encryption at rest, multi-image updates, runtime isolation of the application, provisioning of the trust anchor, and authenticated bootloader updates.

## Update lifecycle in SUF2 v0.1

The generic [secure boot lifecycle](../reference/secure-boot-lifecycle.md) separates authorization, installation and boot eligibility, then follows the boot decision through handoff. SUF2 uses the staged approach described in [design guidelines sections 2-4](../implementers/design-guidelines.md#2-stage-verify-then-install):

- Receive the complete candidate into staging, then check its signature, digests, local policy and, when encrypted, its GCM tag.
- Re-verify staging before destructive writes. Install under a journal, treating journal fields as untrusted resumption input.
- Before overwriting an eligible image, invalidate its eligibility or ensure trusted boot-time verification rejects changed bytes; recovery must remain independent of an image being overwritten.
- Check the installed bytes before marking the image bootable, then verify the retained manifest and installed image again on every boot.
- On failure, do not run an invalid application. Recovery accepts another authorized package; it does not bypass authorization.

SUF2 does not promise to preserve the previous application through every interruption. Recovery is the fallback when no eligible image remains; a verified-boot claim additionally depends on the platform protections in [trust-chain.md](../reference/trust-chain.md).

## Objectives

How SUF2 v0.1 answers the [security objectives](../reference/threat-model.md#security-objectives):

| Objective | v0.1 answer |
| --- | --- |
| O1 Authenticity | **Provided** for updates through the bootloader; for boot, only when the platform protects the loader and anchor |
| O2 Integrity | **Provided** (detection), with the platform limits on U8 and P3 below |
| O3 Applicability | **Provided:** signed product ID; flash destination from local configuration |
| O4 Freshness | **Not provided** |
| O5 Key lifecycle | **Not provided:** several keys may be provisioned, but none can be revoked |
| O6 Confidentiality in transit | **Optional:** image contents only; metadata stays visible |
| O7 Confidentiality at rest | **Not provided** |
| O8 Safe installation | **Provided** for rejected packages; for interruption, recovery mode is the guarantee, not the old image |
| O9 Robustness | **Designed:** fixed layout and bounded state; needs fuzzing evidence |
| O10 Root-of-trust protection | **Platform** responsibility |
| O11 Runtime containment | **Not provided** |
| O12 Availability | **Partial:** the device can always be recovered through update mode, but an attacker on the update interface can deny service |
| O13 Device-secret confidentiality | **Partial:** the loader does not export protected secrets, but key extraction and side-channel resistance depend on the platform |
| O14 Host-tool robustness | **External:** host-tool package parsing and hardening are outside the device profile |

## How each threat is treated

Treatment values:

- **Mitigated:** SUF2 requires a mechanism intended to defeat the threat. Nothing has been tested yet.
- **Partial:** SUF2 reduces the risk or detects the attack, but cannot prevent it alone.
- **Platform:** depends on the platform guarantees in [trust-chain.md](../reference/trust-chain.md).
- **Accepted:** known and not addressed in v0.1.
- **External:** belongs to systems outside the bootloader, such as the signing pipeline or delivery service.

References are sections of [suf2-specification.md](../spec/suf2-specification.md), [design-guidelines.md](../implementers/design-guidelines.md) (DG) or the generic [trust-chain requirements](../reference/trust-chain.md#2-requirements-for-a-verified-boot-claim) (TC), and cases in [conformance.md](../spec/conformance.md).

### Update input

| ID | Treatment | How | Reference | Test |
| --- | --- | --- | --- | --- |
| U1 | Mitigated | Mandatory signature check against a provisioned key; no unsigned mode | spec 7, 9 | S1–S6, S18a–S18c |
| U2 | Mitigated | Manifest signature, image digests and, when encrypted, the GCM tag. UF2 headers and package identifiers are checked against the fixed layout and signed manifest; they have no separate signature | spec 3–7, 9 | S1, I1, I4, E1–E4 |
| U3 | Mitigated | Package identifier recomputed from the manifest, canonical block numbering and a final digest over the assembled image. Blocks of other UF2 families never enter a session | spec 4, 5, 8 | T2, T3, T13a–T13b, T15, T17 |
| U4 | Mitigated | Assembly is order-independent: valid reordering and identical duplicates are **accepted**, because the assembled package must still verify. Truncation, conflicting duplicates and inconsistent block counts are rejected. A failed session stays failed until a local reset, and blocks arriving after completion cannot change the candidate | spec 8 | V5–V7, T1, T8–T10, T14, T16–T21, T22, I2, I3 |
| U5 | Partial | Fixed layout, checked arithmetic, bounded caller-owned state. Evidence depends on fuzzing, which is not yet planned in detail | DG 1 | T4, T5a–T5c, T6a–T6d, T11a–T11b; fuzzing plan (gap) |
| U6 | Mitigated | The signed load address identifies the intended execution base. Local policy checks that base and image length and selects the physical installation destination; UF2 `targetAddr` must be zero | spec 3, 6; TC 2 | T5a–T5c, T11b, S11a, B17 |
| U7 | Partial | Fixed limits and session reset on link disconnect bound resource use. Someone with access to the update interface can still keep the device in update mode | spec 8; DG 5 | T9, T10, T12 |
| U8 | Partial | Staged data is re-verified immediately before destructive writes, then installed bytes are checked before being marked bootable. On each boot, the image must stay unchanged from verification to handoff | spec 9; DG 2; TC 2 (item 5) | B9 |
| U9 | Partial | The loader does not export keys or protected plaintext; protection against extraction through physical access or side channels is not guaranteed by the package profile | DG 8; TC 2 | B10; platform review |
| U10 | Platform | Security configuration must be stored and protected as part of the platform policy; package metadata cannot change it | spec 6, 9; TC 2 | B12; platform review |
| U11 | Partial | Generic error handling must not disclose secrets; timing and side-channel behavior require cryptographic-library and platform review | DG 7, 8 | side-channel review (gap) |
| U12 | Platform | Normal update-mode requests and update-interface commands must not bypass package authorization or grant privileges | DG 5; TC 2 | B14; platform review |
| U13 | Mitigated | Parsing is bounded, but package metadata remains untrusted. Local policy controls destinations; signature and image checks precede installation, and encrypted plaintext is authenticated before install | spec 6, 8, 9 | S1, T5a–T5c, E1–E4 |

### Distribution and host

| ID | Treatment | How | Reference | Test |
| --- | --- | --- | --- | --- |
| H1 | Mitigated | Signature verification on the device | spec 9 | S1, I1 |
| H2 | Accepted | No anti-rollback in v0.1 | spec 12 | B8 (documents acceptance) |
| H3 | Mitigated | Signed product ID checked against local policy | spec 6, 9 | S10 |
| H4 | External | Delivery-service availability | none | none |
| H5 | External | Host tools must treat packages as untrusted input; host-tool hardening is outside the device profile | DG 1 | host-tool tests (separate) |
| H6 | Accepted | Encryption covers image contents only; sizes, digests and key IDs stay visible, and a plaintext digest allows guess confirmation | spec 6 | none |

### Signing and keys

| ID | Treatment | How | Reference | Test |
| --- | --- | --- | --- | --- |
| K1 | Accepted | Guidance on offline keys; several keys may be provisioned, but there is no revocation or rotation | DG 6; TC 6 | none |
| K2 | External | Build and signing pipeline security | none | none |
| K3 | Partial | Test keys are labelled, and production configurations must reject them | DG 6; TC 6 | B20 (planned; not yet run) |
| K4 | Accepted | A key per product or fleet segment limits the blast radius compared with one fleet-wide key, but extraction exposes packages protected by that key across its devices. Hardware resistance to key extraction is a platform concern | DG 6 | platform key-extraction review |
| K5 | Partial | Producers must use a fresh CSPRNG nonce per AES-GCM invocation with no user override. The provisioning and signing pipeline must enforce at most 2^32 invocations per key over its lifetime, across all producers combined, including discarded or retried encryptions. A device cannot detect reuse or enforce that shared budget | spec 7; DG 6 | PR1–PR3 (planned; not yet run) |
| K6 | Mitigated | Only provisioned keys are trusted; key IDs only select among them | spec 6.1, 9 | S4–S6, S19 |
| K7 | Mitigated | Algorithms are fixed by the format version; unsupported versions are rejected; no fallback from encrypted to signed-only | spec 7, 9, 11 | S7a–S7d, S8, E7, E8 |
| K8 | Accepted | v0.1 has no key-revocation or retirement mechanism; a provisioned key remains trusted | spec 12 | none |
| K9 | Out of scope | v0.1 does not define authorized key transitions or recovery from a transition that removes all usable keys | spec 1, 12 | none |

### Application and bootloader

| ID | Treatment | How | Reference | Test |
| --- | --- | --- | --- | --- |
| X1 | Accepted | The update-mode request is one-shot and grants nothing | DG 5 | B6 |
| X2 | Platform | Per-boot verification detects image changes. Without hardware boot protection the application can replace the loader, key or policy. A verified-boot design that restricts application flash access is not yet defined | spec 9; TC 2 | B3, B4; platform review |
| X3 | Partial | Application-facing subcase of U12: the request only selects update mode; the platform must ensure the application-to-loader interface cannot modify protected state or grant privileges | DG 5; TC 2 | B6, B7; platform review |

### Storage and physical access

| ID | Treatment | How | Reference | Test |
| --- | --- | --- | --- | --- |
| P1 | Partial | The installed image and retained manifest are verified at every boot. Modification is detected, not prevented | spec 9; DG 4 | B3, B4, B21 |
| P2 | Platform | Requires a root of trust established by trusted provisioning, plus a trust anchor and boot policy protected against the stated attacker; the verifier may be immutable or authenticated by an earlier trusted stage | TC 2 (items 1, 2) | platform walkthrough |
| P3 | Platform | Checked code must stay unchanged until handoff (TC 2 item 5). Keeping it unchanged while it runs is a separate runtime protection the platform must provide (TC 4) | TC 2 (item 5); TC 4 | platform walkthrough |
| P4 | Accepted | Encryption is transit-only | spec 12 | none |
| P5 | Partial | Staging, journal and recovery mode. The old image may be lost; recovery mode is the guarantee | DG 3 | B1, B2, B5 |
| P6 | Accepted | Verification failure enters recovery; watchdogs and health checks are the integrator's job | DG 3, 5 | none |
| P7 | Accepted | Hardware attacks are outside the v0.1 profile | none | none |
| P8 | Mitigated | The journal is treated as untrusted recovery input: its fields are validated, staging is re-authenticated before resuming, and journal state never marks an image bootable or skips boot verification | DG 3 | B2, B11 |
| P9 | Platform | Debug and alternate boot paths must provide equivalent authorization and integrity guarantees under protected local policy, or be disabled against the stated attacker; a reachable bypass prevents a verified-boot claim | TC 2 (item 3); TC 3 | platform walkthrough |
| P10 | External | Correct trust-anchor and policy provisioning is outside SUF2 and must be established by the manufacturing process | TC 2; TC 6 | manufacturing/provisioning review |
| P11 | Out of scope | v0.1 defines application packages, not bootloader-update authorization or rollback policy | spec 1, 12 | none |
| P12 | Platform | Recovery interfaces must preserve equivalent authorization and protected-state guarantees under protected policy; they may use a distinct verifier or key if explicitly authorized | DG 3, 5; TC 2 (items 3, 7) | B13; platform walkthrough |
| P13 | Partial | Transfer limits bound individual sessions but do not prevent flash wear from repeated writes. v0.1 defines no anti-rollback counter; if a platform uses irreversible counters or fuses, its update policy must avoid exhausting them | spec 8, 12; DG 9 | endurance analysis (gap) |
| P14 | Accepted | v0.1 has no anti-rollback mechanism for firmware or key-policy state; integrity checks do not establish freshness | spec 12 | B8 covers old signed firmware; authentic policy-state rollback has no test |

## Outcomes by platform class

SUF2 does not provide a hardware root of trust, so the outcome depends on the platform. The classes are defined in [trust-chain.md](../reference/trust-chain.md#3-assurance-labels):

- **Authenticated update:** the update path accepts packages authorized by a trusted key and local policy. This does not promise per-boot image checks or equivalent guarantees across other boot paths.
- **Verified boot:** every boot path reachable to the stated attacker ends at a trusted verifier, which checks the installed image against protected local policy on each boot and ensures the checked bytes are the bytes executed.

"Holds" means the objective is intended to hold if the platform requirements are met. Nothing has been tested yet.

| Attacker | Authenticated update | Verified boot |
| --- | --- | --- |
| A. Update interface | Holds for authenticity and integrity; availability not protected | Same |
| B. File tampering | Holds | Holds |
| B. Replay of an old signed file | **Does not hold** (H2) | **Does not hold** (H2) |
| C. Compromised host | Holds | Holds |
| D. Compromised application | **Does not hold:** it can overwrite the loader, key or policy (X2); runtime containment is not provided | Loader, trust anchor and policy are protected against this attacker; persistent firmware and update-state changes are checked at boot; runtime behavior is not contained |
| PH1. Physical interruption/access | Holds for authenticity and integrity; availability may be affected | Same |
| PH2. Flash or debug access | **Does not hold** while the attacker can effectively write the loader/key or use an alternate boot path (P2, P9) | No claim against effective flash/debug access unless hardware prevents it and all alternate paths preserve verification; in that configuration, assess the attacker against the capabilities that remain. Firmware may still be readable (P4) |
| PH3. Laboratory attacker | No resistance to probing or fault injection is claimed (P7) | Same; requires separate hardware evidence |
| F. Signing compromise | **Does not hold** (K1, K2) | **Does not hold** |

## Assumptions

- The trust anchor was provisioned correctly and cannot be changed by the attacker in question.
- The signing key stays secret.
- The cryptographic library is correct.
- The bootloader is installed by a means outside SUF2.
- The package producer generates nonces as required (K5).
- Device security configuration and provisioning are protected by the platform and manufacturing process (U10, P10).

## Claims that need evidence

| Claim | How it will be checked |
| --- | --- |
| A rejected or interrupted update never leaves a bootable partial image | Fault-injection tests (B2) |
| Re-verifying staging reduces the check/use window to what the platform guarantees | Test B9 and a hardware review of TC 2 item 5 |
| A forged or corrupted journal cannot cause an unverified boot | Test B11 |
| Device security policy cannot be changed through package or recovery input | Test B12 and platform review |
| Recovery paths do not bypass authorization or protected-state checks | Test B13 and platform walkthrough |
| Normal update-mode requests do not grant privileges beyond selecting the update path | Test B14 |
| The parser handles hostile input safely | A fuzzing plan (to be written) and tests T1–T20 |
| Hardware boot protection gives the integrity described above | Each platform's documentation and a hardware walkthrough |
| Per-boot verification of large images is fast enough | Measurement on each target |
| Device secrets resist physical extraction and secret-dependent timing | Platform and cryptographic-library review |
| Repeated updates do not exceed the target's flash endurance assumptions | Target-specific endurance analysis |

Known gaps: executable fuzzing and producer-test harnesses (PR1–PR3, K5), platform assurance tests (including B20–B21), a side-channel review (U11), flash-endurance analysis (P13), provisioning evidence (P10), recovery-path tests (P12), and platform walkthroughs. B14 covers the normal update-mode request in U12.

## Decisions for v0.1 and future work

Fixed for v0.1:

- No anti-rollback (H2, P14), no key revocation or rotation (K1, K8, K9), no encryption at rest (P4).
- Encryption is optional and protects image contents in transit only (H6).
- The root of trust, security-policy protection, runtime containment, recovery-path protection and bootloader-update policy are the platform's or integrator's job (P2, P3, P9–P12, U10, X2, X3).

Candidates for a later revision:

- A monotonic version counter against rollback (H2).
- A key revocation, retirement or rotation mechanism (K1, K8), with a recoverable key-transition protocol (K9).
- A verified-boot profile in which the loader restricts application flash access on hardware that supports it (X2).

Encryption is required only when protected device policy says so (spec 9.3). A package cannot weaken that policy.

## Keeping this current

Update this page whenever the specification, a platform integration, or the threat model changes. External review is required before v0.1 is frozen.
