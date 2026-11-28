# SUF2 conformance test catalogue

This is a draft checklist for comparing SUF2 producers and receivers against the [specification](suf2-specification.md). Nothing here has been run yet, and there are no published vectors or test harness.

The cases fall into three groups: portable package and receiver checks (sections 1–5), platform assurance tests (section 6) and producer-pipeline checks (section 7).

## Result terms

- **Accept:** a complete candidate passes every check and may be installed. It says nothing about whether a platform finished installing or booting it.
- **Reject:** the candidate or block fails a required check. The session fails, the cause is reported, nothing is installed and the installed application is unchanged.
- **Ignore:** no effect on the session and no error. This covers non-SUF2 sectors and identical repeated blocks after completion.
- **Drop/report:** a recognized SUF2 block is discarded and reported without touching the current candidate.
- **Incomplete:** blocks are still missing. The receiver keeps collecting and installs nothing; a local reset discards the session.
- **No boot:** the loader does not start the application and enters the platform's recovery path.

## Fixture conventions

**Test device.** Accepts product IDs P1 and P2 (but not P3), execution bases `0x08000000` and `0x08010000`, images up to 4,096 bytes, and at most 33 blocks. Encryption is optional. The block limit is deliberately above the 17 blocks a 4,096-byte image needs, so image-length policy can be tested separately from the block limit. Record the staging and decryption capacity for resource cases.

**Keys.** Test-only and clearly labelled: signing keys A and B (provisioned) and C (not provisioned), and AES keys K1 and K2 (provisioned). Never use production keys.

**Building mutated packages.**
- Whenever block 0 changes, recompute the package identifier and put it in every block. The only exception is T15, which tests the identifier itself.
- Unless a case is marked *(attacker)*, re-sign the mutated manifest with key A. That keeps the signature valid, so the case reaches the check it names instead of failing at the signature. These cases show that the receiver enforces the profile even for a validly signed package.
- A case marked *(attacker)* keeps the original signature. These cases show that the signature protects the changed field.

**Checking results.** Every Reject also checks that the cause is reported and nothing is installed. The **Stage** column names the check that should fire (Block, Struct, Policy, Key, Sig, Digest, GCM). Assert the stage only if the receiver documents a status code or test hook for it; otherwise a pass means just "rejected, session failed, no install".

## 1. Valid packages

These show that correct packages are accepted in every layout the receiver must handle.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| V1 | Signed-only package, one-chunk image | Accept | §5, §9.1 |
| V2 | Signed-only package, several chunks and a short last chunk | Accept | §5, §7, §9.1 |
| V3 | Signed-only package whose image length is a multiple of 256 | Accept | §5, §9.1 |
| V4 | Encrypted package with valid signature, provisioned keys and tag | Accept | §6, §7, §9.1 |
| V5 | Blocks arrive in reverse order | Accept | §5, §8.2 |
| V6 | Blocks arrive shuffled, with image blocks before block 0 | Accept | §8.2 |
| V7 | Every block is sent twice, identically | Accept; duplicates do not change completeness | §8.2 |
| V8 | Unrelated sectors are interleaved with a valid package | Ignore the sectors; Accept the package | §8.1 |
| V9 | Signed-only image of exactly 4,096 bytes | Accept | §9.1 |
| V10 | Encrypted image with a short last chunk (nonzero image bytes, then zero padding) | Accept; padding is excluded from the digests and the GCM input | §5, §7 |

## 2. Signature and manifest

These show that the signature covers the manifest, that only provisioned keys are trusted, and that a validly signed package is still rejected if it breaks the profile.

| ID | Case | Expected | Stage | Spec |
| --- | --- | --- | --- | --- |
| S1 | *(attacker)* Change one signed field of an encrypted package, once per field listed below | Reject | Sig | §7, §9.1 |
| S2 | Set `r` or `s` to zero or above its allowed range | Reject | Sig | §7 |
| S3 | Replace a valid `s` with `n − s` | Reject | Sig | §7 |
| S4 | Sign with unprovisioned key C and its correctly derived key ID | Reject | Key | §6.1, §9.1 |
| S5 | Sign with key A but carry a key ID that matches no provisioned key | Reject | Key | §6.1, §9.1 |
| S6 | Carry key A's ID but sign with provisioned key B | Reject | Sig | §6.1, §9.1 |
| S7a | Sign with a secp256k1 key but carry key A's ID | Reject | Sig | §7 |
| S7b | Sign the manifest with ECDSA over SHA-384 | Reject | Sig | §7 |
| S7c | Signed-only package whose two digests are SHA3-256, validly signed | Reject | Digest | §7, §9.1 |
| S7d | Encrypted package using AES-128-GCM, validly signed | Reject | GCM | §7, §9.1 |
| S8 | Unsupported format version | Reject | Struct | §6, §11 |
| S9 | Nonzero byte in reserved range 84–95 or 176–191 | Reject | Struct | §6 |
| S10 | Product ID P3 | Reject | Policy | §6, §9.1 |
| S11a | Execution base outside local policy | Reject | Policy | §6, §9.1 |
| S11b | Image length 4,097 (18 blocks, so within the block limit) | Reject | Policy | §9.1 |
| S12 | Image length zero | Reject | Struct | §6 |
| S13 | `numBlocks` in every block differs from `ceil(L / 256) + 1` | Reject | Struct | §5, §6 |
| S14 | A manifest flag bit other than bit 0 set | Reject | Struct | §6 |
| S15 | Signed-only package with unequal digests, or a nonzero encryption-key ID, nonce or tag | Reject | Struct | §6 |
| S16 | Encrypted package with an all-zero encryption-key ID | Reject | Struct | §6 |
| S17 | Wrong manifest magic | Reject | Struct | §6 |
| S18a | Sign a message without the `SUF2-MANIFEST-V1` prefix | Reject | Sig | §7 |
| S18b | Hash the signed message twice before signing | Reject | Sig | §7 |
| S18c | Encode `r` and `s` little-endian | Reject | Sig | §7 |

S1 field variants: product ID (P2), execution base (`0x08010000`), a length that stays within the same chunk count and padding, encryption-key ID (K2), nonce, plaintext digest, stored digest and tag. Magic, version, flags and reserved bytes are not S1 variants because they fail structure checks before the signature; S8, S9, S14 and S17 cover them.

## 3. Image integrity

These show that the image bytes are tied to the signed digests and that a partial image is never installed.

| ID | Case | Expected | Stage | Spec |
| --- | --- | --- | --- | --- |
| I1 | Flip one bit of image data in a signed-only package | Reject | Digest | §9.1 step 4 |
| I2 | Omit the last image chunk; confirm Incomplete; then send it | Incomplete, then Accept | | §8.2 |
| I3 | While collecting, send a block whose `blockNo` equals `numBlocks` and whose `numBlocks` matches the session | Reject before staging | Block | §5, §8.2 |
| I4 | Encrypted package with a stored digest that does not match the received ciphertext; no matching AES key provisioned | Reject at the digest check, before key selection | Digest | §9.1 steps 4–5 |
| I5 | Encrypted package with a valid tag but a plaintext digest that does not match the decrypted image | Reject | Digest | §9.1 step 5 |

## 4. Transport and receiver

These exercise the receiver's session rules: ordering, conflicts, bounds, reset and what happens after completion or failure.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| T1 | A different payload for a block number already received | Reject the block | §8.2 |
| T2 | While collecting package A, a block from package B | Reject; do not switch candidates | §4, §8.2 |
| T3 | Before block 0 arrives, a block with the same package ID but a different `numBlocks` | Reject | §8.2 |
| T4 | Manifest bytes at a nonzero block number and image bytes at block 0 | Reject | §5, §9.1 |
| T5a | Clear `not main flash`, keeping the SUF2 family ID | Reject | §3 |
| T5b | Set an extra UF2 flag, keeping the SUF2 family ID | Reject | §3 |
| T5c | Nonzero `targetAddr` | Reject | §3 |
| T6a | Payload size other than 272 | Reject | §3 |
| T6b | Nonzero byte in `data[272..475]` | Reject | §3 |
| T6c | Nonzero padding after the last useful image byte | Reject | §5 |
| T7 | A sector with incorrect UF2 magic | Ignore | §8.1 |
| T8 | Send the highest-numbered block first, stop early, then send the rest | Incomplete, then Accept | §8.2 |
| T9 | Fail a session, reset locally, then send a valid package | Reject, then Accept in the new session | §8.2 |
| T10 | Stage part of package A, reset, then send only A's missing blocks | Incomplete; staged blocks from before the reset do not count | §8.2 |
| T11 | White-box arithmetic test: derive the chunk count for `L = 0xFFFFFFFF`, and check `base = 0xFFFFFFF0` with `L = 0x20` | No wraparound; the wrapping range is rejected before any storage access | §6, §9.1 |
| T12 | Send a block above the local block limit, then run 100 invalid-session and reset cycles | Reject before staging; peak usage stays within the staging capacity and does not grow across cycles | §8.2 |
| T13 | Blocks of another UF2 family, or with `familyID present` clear, alone and during collection | Ignore; no plain-UF2 fallback install | §8.1 |
| T14 | After a failure, send more SUF2 blocks, including a whole valid package, before reset | Drop/report; the session stays failed | §8.2 |
| T15 | Give every block a package ID that differs from the one recomputed from block 0, for example one computed over manifest bytes 0–191 only, or left unchanged after block 0 was altered | Reject | §4, §8.2 |
| T16 | After candidate A completes, an identical repeated block, then a different SUF2 block | Ignore the duplicate; drop/report the other; A is unchanged | §8.2 |
| T17 | After block 0 arrives, a block with the same package ID and a conflicting `numBlocks` | Reject | §8.2 |
| T18a | No session; `numBlocks = 1` | Reject before staging | §8.2 |
| T18b | No session; `numBlocks = 34` (above the block limit) | Reject before staging | §8.2 |
| T18c | No session; `blockNo >= numBlocks` | Reject before staging | §8.2 |
| T19 | While A is being verified or installed, a malformed block or a block for package B | Drop/report; A is unaffected | §8.2 |
| T20 | As the first SUF2 block, a malformed block that passes the T18 bounds (for example, wrong payload size) | Reject without staging | §8.2 |
| T21 | Reset after complete candidate A while verification is running, then send B; separately, reset and send A again | The old session authorizes nothing; the new candidate is checked from scratch | §8.2 |
| T22 | Send an incomplete package, then close the file or let a timeout expire; follow with a fresh complete copy | Nothing is installed after the close or timeout; the fresh copy is Accepted | §8.2 |

T12's limits and its 100 cycles are test parameters, not wire-format limits.

## 5. Encryption

These show that encrypted packages are authenticated before any plaintext is trusted, and that nothing falls back to signed-only handling. E1–E4 and E9 are re-signed so they reach the GCM check.

| ID | Case | Expected | Stage | Spec |
| --- | --- | --- | --- | --- |
| E1 | Change the tag; keep the ciphertext and all other fields | Reject | GCM | §7, §9.1 |
| E2 | Change the nonce; keep the ciphertext and tag made with the original | Reject | GCM | §7, §9.1 |
| E3 | Flip a ciphertext bit and update the stored digest; keep the original tag | Reject | GCM | §7, §9.1 |
| E4 | Change the plaintext digest; keep the original ciphertext and tag | Reject | GCM | §7, §9.1 |
| E5 | Encryption-key ID with no provisioned key | Reject | Key | §6.1, §9.1 |
| E6 | Validly signed signed-only package, sent to a device that requires encryption | Reject | Policy | §9.3 |
| E7 | *(attacker)* Clear the encrypted flag of an encrypted package | Reject; never handled as signed-only | Sig | §6, §9.1 |
| E8 | Encrypted candidate that fails GCM, with verification-path selection instrumented | Reject; the signed-only path is never attempted | GCM | §9.1 |
| E9 | Compute the AAD without the `SUF2-AEAD-V1` prefix, or over manifest bytes 0–191 | Reject | GCM | §7 |

## 6. Platform integration and assurance tests

Installation, recovery and the update interface differ by platform, so these are not portable package requirements. For each platform, record its recovery behavior and policy configuration. Passing them does not by itself establish a secure-boot claim.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| B1 | A candidate fails authentication while a valid application is installed | The installed application is unchanged | §8.2 |
| B2 | Cut power at each documented journal transition, then reboot | No partial or unverified image starts; resume safely or enter recovery | §10 |
| B3 | Modify one byte of the installed application | No boot | §9.2 |
| B4 | Modify the retained manifest | No boot | §9.2 |
| B5 | Boot with no valid installed image | No boot | §9.2 |
| B6 | Request update mode but provide no valid package | Stay in the update or recovery path; start nothing unauthorized | §10 |
| B7 | Provide a valid package after requesting update mode | Install only after verification; boot only after boot checks pass | §9, §10 |
| B8 | An older, correctly signed image that otherwise meets policy | Accept unless the platform documents its own rollback policy; record which applies | §1, §12 |
| B9 | Modify staged bytes after verification but before installation | Re-verify or reject; the modified bytes are not installed | §10 |
| B10 | Inspect update-interface files and outputs before package authentication | No keys and no unauthenticated plaintext are exposed | §10 |
| B11 | Forge or corrupt journal fields, then reboot | The journal cannot skip verification; resume safely or enter recovery | §10 |
| B12 | Try to change protected policy through package metadata or persistent update state | Rejected; policy is unchanged | §6, §9 |
| B13 | Try to use a recovery interface to install unauthorized code or alter protected state | Blocked; unauthorized code does not run | §10 |
| B14 | Use an update-mode request or interface command to bypass package authorization | Rejected, or limited to selecting the update path | §10 |
| B15 | Store the manifest apart from the image, with flash padding after the L-byte image | Boot succeeds for a valid image; the hash covers exactly L bytes | §9.2 |
| B16 | Boot an image installed from an encrypted package; keep the signed manifest and only the plaintext image | Boot succeeds; the loader needs no ciphertext and does not recheck the stored digest or tag | §9.2 |
| B17 | Submit a valid package with an out-of-policy execution base while tracing flash writes | Reject before installation; nothing is written to a package-selected destination | §6, §9.1 |

B2 and B11 apply only to platforms with a journal. B6, B10 and B12–B14 need platform-specific interface and policy fixtures.

## 7. Producer and signing-pipeline tests (planned)

These check host tools and the signing pipeline, not receivers. A deterministic CSPRNG fixture and a small test budget let them run without proving randomness quality or spending the real 2^32 budget.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| N1 | Build encrypted packages with a fixture CSPRNG that returns a different 12-byte value per call; try to supply a nonce through user-facing inputs | One nonce is requested per encryption; a user-supplied nonce is refused | §7 |
| N2 | Use a shared ledger with a reduced budget across two producer instances, including a discarded encryption and a retry | Every encryption counts against the shared budget and the next one after the limit is refused; copying an existing package costs nothing | §7 |

## 8. Robustness activities

These supplement the cases above. They have no portable numeric thresholds unless a platform profile supplies them.

- Fuzz the UF2 block parser, the manifest parser and the receiver state machine. No input may cause a crash, out-of-bounds access, unauthorized install or state growth beyond the configured limits.
- Exercise FAT-style sector writes: partial writes, rewrites and large unrelated writes. Non-SUF2 data must never look like an installable package, and ignored sectors must not change session state.
- Record the corpus, resource limits and test budget so runs can be repeated and compared.

## 9. Vector plan

Before calling this catalogue a conformance standard, publish signed-only and encrypted package vectors with test-only keys. Include the intermediate manifest fields, hashes, signatures, tags, package IDs and expected verification stage, and check them with independent producer and receiver implementations.
