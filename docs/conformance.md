# SUF2 conformance test catalogue

This catalogue lists the cases an implementation of [suf2-specification.md](suf2-specification.md) should pass. It contains no code or vectors yet; vectors will be produced and frozen with the specification, so that independent implementations (host tool and C library) agree.

Result key: **A** accept, **R** reject, **I** ignore.

## 1. Valid packages

| ID | Case | Expected |
| --- | --- | --- |
| V1 | Signed-only, one-chunk image | A |
| V2 | Signed-only, multi-chunk image with short last chunk | A |
| V3 | Signed-only, image length exactly a multiple of 256 | A |
| V4 | Encrypted, valid key and tag | A |
| V5 | Blocks in reverse order | A |
| V6 | Blocks in random order, image blocks before manifest | A |
| V7 | Every block duplicated identically | A, completeness unchanged |
| V8 | Unrelated sectors interleaved | I for unrelated, A overall |
| V9 | Maximum allowed image size | A |

## 2. Signature and manifest

| ID | Case | Expected |
| --- | --- | --- |
| S1 | One bit flipped in each signed manifest field | R |
| S2 | Signature r or s zero or out of range | R |
| S3 | High-S signature | R |
| S4 | Valid signature by an unprovisioned key | R |
| S5 | Unknown signing-key ID | R |
| S6 | Signing-key ID derived from an attacker key, with that key's valid signature | R; no package-supplied trust root |
| S7 | Package using a different signature, digest or encryption algorithm under format version 1 | R; algorithms are fixed, not selected by fields |
| S8 | Unsupported format version | R |
| S9 | Nonzero byte in either manifest reserved range (84–95 or 176–191) | R |
| S10 | Wrong product identifier | R |
| S11 | Intended execution base or image length outside local policy, including a staging address that is not a permitted execution base | R; no package-directed physical write |
| S12 | Zero image length | R |
| S13 | Signed image length inconsistent with received block count or image bytes | R |
| S14 | Manifest flag bits other than bit 0 set | R |
| S15 | Signed-only package with different digests, or a nonzero encryption-key ID, nonce or tag | R |
| S16 | Encrypted package with an all-zero encryption-key ID | R |
| S17 | Wrong manifest magic, or `numBlocks` not ceil(L / 256) + 1 | R |
| S18 | Signature over the wrong message: hashed twice, or without the `"SUF2-MANIFEST-V1"` prefix | R |

## 3. Image integrity

| ID | Case | Expected |
| --- | --- | --- |
| I1 | One bit flipped in image data | R |
| I2 | Truncated image (missing last chunk) | R, incomplete |
| I3 | Extra image chunk beyond N | R |
| I4 | Stored digest wrong | R |
| I5 | Plaintext digest wrong (encrypted) | R |

## 4. Transport and receiver

| ID | Case | Expected |
| --- | --- | --- |
| T1 | Conflicting duplicate (same block number, different bytes) | R, session fails |
| T2 | Blocks from two packages mixed | R |
| T3 | Wrong `numBlocks` or `blockNo` | R |
| T4 | Manifest or image content placed at the wrong `blockNo` | R during structure or image verification |
| T5 | SUF2 family ID with other flags wrong (`not main flash` clear or an extra flag set), or nonzero `targetAddr` | R |
| T6 | Payload size not 272, nonzero data[272..475], or nonzero last-chunk padding | R |
| T7 | Bad UF2 magic | I |
| T8 | Highest-numbered block arrives first; transfer incomplete | not complete |
| T9 | Session reset after failure, then a valid package | A |
| T10 | Repeated failed candidates, then valid | A |
| T11 | Integer overflow in derived chunk count, block offset or storage range | R |
| T12 | Huge `numBlocks`, endless stream, or repeated failing sessions | R or bounded; no unbounded memory or state growth |
| T13 | UF2 blocks of another family, or with the `familyID present` flag clear, during a session or alone | I; session unaffected; never installed through plain-UF2 fallback |
| T14 | SUF2 blocks after a session has failed and before a reset, including a complete valid package | R; session stays failed |
| T15 | All blocks carry the same package identifier, but it differs from the one computed from the manifest | R |
| T16 | After completion, an identical duplicate and then a different block from another package | I without error, then R for the second block; candidate and installation unaffected |
| T17 | Same package identifier but conflicting `numBlocks` values while collecting | R; session fails |
| T18 | With no session or while collecting, `numBlocks` below 2, above the local limit, or `blockNo >= numBlocks` | R before staging; session enters failed |
| T19 | Malformed or different-package block arrives after candidate A is complete | Report/drop the incoming block; candidate A's verification and installation are unaffected |
| T20 | First recognized SUF2 block is malformed or out of bounds | R without staging; session enters failed until reset |

## 5. Encryption

| ID | Case | Expected |
| --- | --- | --- |
| E1 | Tag modified | R before any install |
| E2 | Nonce modified | R |
| E3 | Ciphertext modified | R |
| E4 | AAD-covered manifest field modified | R |
| E5 | Wrong or unprovisioned encryption key ID | R |
| E6 | Signed-only package on a device requiring encryption | R |
| E7 | Encrypted flag cleared by an attacker | R; fails signed-only structure or signature verification |
| E8 | Failed encrypted verification retried as signed-only | R |

## 6. Installation and boot (platform tests)

| ID | Case | Expected |
| --- | --- | --- |
| B1 | Authentication failure leaves the installed application | unchanged |
| B2 | Power loss at each journal transition, then reboot | resume or recovery; no partial boot |
| B3 | Installed byte modified | no boot, update mode |
| B4 | Installed manifest modified | no boot, update mode |
| B5 | No image present | update mode |
| B6 | Application requests update mode, then no valid package | stays in update mode |
| B7 | Valid package after request | installs, reboots, boots |
| B8 | Older correctly signed image | A (no anti-rollback in v0.1) |
| B9 | Staged bytes modified after verification, before install | R; installed state unchanged |
| B10 | Inspect all virtual files the loader exposes | No keys or protected plaintext |
| B11 | Journal forged or corrupted (bad phase, wrong package, invalid fields), then reboot | verified image boots or update mode; journal never skips verification |
| B12 | Attempt to alter security policy through package metadata or persistent update state | rejected; protected policy and permitted destinations remain unchanged |
| B13 | Attempt to use a recovery interface to install unauthorized code or alter protected state | rejected or blocked; no unauthorized code runs |
| B14 | Attempt to use a normal update-mode request or update-interface command to bypass authorization or gain privileges | rejected or limited to selecting the update path |
| B15 | Manifest is stored separately from the image and flash padding follows the L-byte image | A; boot hash covers exactly L image bytes |

## 7. Robustness

- Fuzz the UF2 block parser, manifest parser and receiver state machine.
- Random FAT-style writes: partial sector writes, rewrites, large unrelated writes.
- Measure and enforce maximum image size at its boundary (one byte over must reject).

## 8. Vector plan

Vectors will be generated by the host tool with test-only, clearly labelled keys, stored as `.suf2` files with a manifest listing the expected result, and checked by the Python and C implementations with identical results.
