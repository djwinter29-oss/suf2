# SUF2 conformance test catalogue

This is a draft checklist for comparing SUF2 producers and receivers against the [specification](suf2-specification.md). Nothing here has been run yet, and there are no published vectors or test harness.

The cases fall into three groups: portable package and receiver checks (sections 1–5), platform assurance tests (section 6) and producer-pipeline checks (section 7).

New to the catalogue? Run V1–V5, then S1 and S4, I1–I2, T1–T2, T18a–T18c and E1. That covers a valid update, a few common rejections and recovery from an incomplete transfer.

## Reporting results

Record every case as **Pass**, **Fail**, **N/A** (with the reason) or **Not run** (for example, a needed hook is missing). A receiver may claim to pass sections 1–5 only if every case that applies to it passes and each N/A is explained. Sections 6 and 7 are reported separately, because they depend on the platform and the signing pipeline.

Section 6 cites the [design guidelines](security/design-guidelines.md) as *DG n* (section n) and the [trust chain](reference/trust-chain.md) as *TC n* (section n, with requirement numbers in brackets).

## Result terms

- **Accept:** a complete candidate passes every check and may be installed. It says nothing about whether a platform finished installing or booting it.
- **Reject:** the candidate or block fails a required check. The session fails, the cause is reported, nothing is installed and the installed application is unchanged.
- **Ignore:** no effect on the session and no error. This covers non-SUF2 sectors and identical repeated blocks after completion.
- **Drop and report:** a recognized SUF2 block is discarded and reported without touching the current candidate.
- **Incomplete:** blocks are still missing. The receiver keeps collecting and installs nothing; a local reset discards the session.
- **No boot:** the loader does not start the application and enters the platform's recovery path.

## Fixture conventions

| Setting | Test-device configuration |
| --- | --- |
| Product IDs | P1 and P2 are accepted; P3 is not |
| Execution bases | `0x08000000` and `0x08010000` |
| Maximum image | 4,096 bytes |
| Block limit and staging | At most 33 blocks; staging holds 33 256-byte content areas (manifest plus 32 image chunks) and completeness metadata. The 4,096-byte image limit needs only 17 blocks, so image policy can be tested separately from the block limit. |
| Decryption storage | At least 4,096 bytes |
| Encryption policy | Optional by default. E6 uses an encryption-required device; B19 changes the policy after installing a signed-only image. |
| Block-limit boundary | S11c uses a 33-block package, which passes the block limit but exceeds the image limit. |

**Keys.** Use clearly labelled test-only signing keys A and B (provisioned) and C (not provisioned), plus AES keys K1 and K2 (provisioned). Never use production keys.

**Building mutated packages.**
- Whenever block 0 changes, recompute the package identifier and put it in every block. The only exception is T15, which tests the identifier itself.
- Unless noted otherwise, re-sign a changed manifest with key A. This keeps the signature valid so the case reaches the check it is meant to exercise.
- In section 2, the **Signed by** column uses *A* for a normal key-A signature, *original* when the existing signature is retained, and *other* when the case itself specifies the signer or signature construction.

Test IDs are stable because other documents refer to them. Lettered IDs are variations of the same case; run each one, because a receiver can pass one and fail another.

The harness must observe session state (none, collecting, complete or failed) and whether installation was authorized or completed. A receiver may expose that through its documented status interface or a test hook. Marked *(hook)* cases need a hook to control timing or observe an internal decision.

## Stage labels

A stage names the check that should reject a candidate, not when it happens to run. Where timing matters, the Expected column says so. Stages are descriptive, not required status-code names; assert one only when the receiver exposes suitable diagnostics. Otherwise the expected result is rejection with no installation.

| Stage | Check |
| --- | --- |
| Block | Pre-staging bounds in §8.2 |
| Format | UF2 block and content layout, including last-chunk padding, §§3–5 and §8.1 step 3 |
| Session | Conflicts between blocks of one session, §8.2 |
| Struct | Manifest, package-identifier and block-count checks, §9.1 step 1. A receiver may run these when block 0 arrives. |
| Policy | Local policy, §9.1 step 2 |
| SigKey | Signing-key selection, §9.1 step 3 |
| Sig | Signature validation, §9.1 step 3 |
| Stored | Stored-image digest, §9.1 step 4 |
| AESKey | Encryption-key selection, §9.1 step 5 |
| GCM | GCM tag verification, §9.1 step 5 |
| Plain | Plaintext digest, §9.1 step 5 |

## 1. Valid packages

Ordinary signed-only and encrypted updates, before any rejection path.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| V1 | Signed-only package, one-chunk image | Accept | §5, §9.1 |
| V2 | Signed-only package, several chunks and a short last chunk | Accept | §5, §7, §9.1 |
| V3 | Signed-only package whose image length is a multiple of 256 | Accept | §5, §9.1 |
| V4 | Encrypted package with valid signature, provisioned keys and tag | Accept | §6, §7, §9.1 |
| V5 | Blocks arrive in reverse order | Accept | §5, §8.2 |
| V6 | Blocks arrive shuffled, with image blocks before block 0 | Accept | §8.2 |
| V7a | Send one block twice and withhold another; observe Incomplete, then send the withheld block | Incomplete, then Accept; the duplicate did not count toward completeness | §8.2 |
| V7b | Every block is sent twice, identically | Accept | §8.2 |
| V8 | Unrelated sectors are interleaved with a valid package | Ignore the sectors; Accept the package | §8.1 |
| V9 | Signed-only image of exactly 4,096 bytes | Accept | §9.1 |
| V10 | Encrypted image with a short last chunk (nonzero image bytes, then zero padding) | Accept; padding is excluded from the digests and the GCM input | §5, §7 |
| V11 | Signed-only package signed with provisioned key B | Accept | §6.1, §7, §9.1 |
| V12 | Signed-only package with product ID P2 and execution base `0x08010000` | Accept | §6, §9.1 |
| V13 | Encrypted package using provisioned AES key K2 | Accept | §6.1, §7, §9.1 |

## 2. Signature and manifest

Malformed signatures, unknown keys and manifests outside the profile. The **Signed by** column shows how each package's signature is produced.

| ID | Case | Signed by | Stage | Spec |
| --- | --- | --- | --- | --- |
| S1 | Change one signed field of an encrypted package, once per field listed below | original | Sig | §7, §9.1 |
| S2a | `r = 0` | other | Sig | §7 |
| S2b | `s = 0` | other | Sig | §7 |
| S2c | `r = n` | other | Sig | §7 |
| S2d | `s = n` | other | Sig | §7 |
| S3a | Replace a valid `s` with `n − s` | other | Sig | §7 |
| S3b | `s = (n − 1) / 2 + 1`, the smallest high-S value | other | Sig | §7 |
| S4 | Sign with unprovisioned key C and its correctly derived key ID | other | SigKey | §6.1, §9.1 |
| S5 | Sign with key A but carry a key ID that matches no provisioned key | A | SigKey | §6.1, §9.1 |
| S6 | Carry key A's ID but sign with provisioned key B | other | Sig | §6.1, §9.1 |
| S7a | Sign with a secp256k1 key but carry key A's ID | other | Sig | §7 |
| S7b | Sign the manifest with ECDSA over SHA-384 | A | Sig | §7 |
| S7c | Signed-only package whose two digests are SHA3-256 | A | Stored | §7, §9.1 |
| S7d | Encrypt with AES-128-GCM using the first 16 bytes of K1; carry K1's ID | A | GCM | §7, §9.1 |
| S8 | Unsupported format version | A | Struct | §6, §11 |
| S9a | Nonzero byte in reserved range 84–95 | A | Struct | §6 |
| S9b | Nonzero byte in reserved range 176–191 | A | Struct | §6 |
| S10 | Product ID P3 | A | Policy | §6, §9.1 |
| S11a | Execution base outside local policy | A | Policy | §6, §9.1 |
| S11b | Image length 4,097 (18 blocks, within the 33-block limit) | A | Policy | §9.1 |
| S11c | Image length 8,192 (33 blocks, exactly at the block limit) | A | Policy | §8.2, §9.1 |
| S12 | Image length zero, with `numBlocks` and the block list left unchanged | A | Struct | §5, §6 |
| S13a | `numBlocks` in every block is one more than `ceil(L / 256) + 1` | A | Struct | §5, §6 |
| S13b | `numBlocks` in every block is one less than `ceil(L / 256) + 1` | A | Struct | §5, §6 |
| S14 | A manifest flag bit other than bit 0 set | A | Struct | §6 |
| S15a | Signed-only package with unequal digests | A | Struct | §6 |
| S15b | Signed-only package with a nonzero encryption-key ID | A | Struct | §6 |
| S15c | Signed-only package with a nonzero nonce | A | Struct | §6 |
| S15d | Signed-only package with a nonzero tag | A | Struct | §6 |
| S16 | Encrypted package with an all-zero encryption-key ID | A | Struct | §6 |
| S17 | Wrong manifest magic | A | Struct | §6 |
| S18a | Sign a message without the `SUF2-MANIFEST-V1` prefix | A | Sig | §7 |
| S18b | Hash the signed message twice before signing | A | Sig | §7 |
| S18c | Encode `r` and `s` little-endian | A | Sig | §7 |
| S19 | Correctly sign with key A but derive its manifest key ID from the compressed public key | A | SigKey | §6.1 |

For S1, use an encrypted base package with a short last chunk. Variants change product ID to P2, execution base to `0x08010000`, signing-key ID to key B's ID while retaining key A's signature, image length by a few bytes into the existing final chunk's zero padding, encryption-key ID to K2, nonce, either digest, or tag. Use S8, S9a, S9b, S14 and S17 for fields that fail structure before signature verification.

Notes on the less obvious cases:

- **S6, S5, S19:** S6 checks that the key ID selects the provisioned key A rather than B. S5 rejects an arbitrary unknown ID. S19 checks the compressed-versus-uncompressed derivation rule; a receiver with the wrong derivation may accept S19 and reject an ordinary package such as V1.
- **S7a–S7d:** these show there is no algorithm agility. A receiver that verifies only with provisioned key A over P-256 rejects S7a just as it rejects S6, so S7a mainly catches a receiver that tries other curves. S7d encrypts with AES-128 using the first 16 bytes of K1 and K1's ID; the receiver selects the provisioned 256-bit K1 and rejects the tag.
- **S12:** a zero length also makes `numBlocks` disagree with the derived count, so S13-style checking rejects it first. The rule that L must be nonzero is isolated at boot by B22.
- **S11b, S11c:** S11c passes the block-limit check, so rejection must come from the image-size policy.

## 3. Image integrity

Data that no longer matches the signed manifest, including after decryption. I3 is listed with the bounds cases in section 4.

| ID | Case | Expected | Stage | Spec |
| --- | --- | --- | --- | --- |
| I1 | Flip one bit of image data in a signed-only package | Reject | Stored | §9.1 step 4 |
| I2 | Omit the last image chunk; observe Incomplete, then send it | Incomplete, then Accept | — | §8.2 |
| I4 | *(hook)* Encrypted package with a stored digest that does not match the ciphertext and an encryption-key ID that matches no provisioned key | Reject at the stored-digest check, before AES-key selection | Stored | §9.1 steps 4–5 |
| I5 | Encrypted package whose plaintext digest is wrong before encryption, with a valid tag and signature | Reject | Plain | §7, §9.1 step 5 |
| I6 | Flip one ciphertext bit of an encrypted package; leave the stored digest and tag unchanged | Reject | Stored | §9.1 step 4 |

For I5, put the incorrect plaintext digest in the manifest before computing the AAD and encrypting. The resulting tag is valid for that digest, but hashing the decrypted plaintext will not match it.

## 4. Transport and receiver

The receiver collects one candidate at a time, and neither a failed session nor a reset can make partial data installable.

| ID | Case | Expected | Stage | Spec |
| --- | --- | --- | --- | --- |
| T1 | A different payload for a block number already received | Reject the session; do not accept the conflicting block | Session | §8.2 |
| T2 | While collecting candidate Alpha, a block from candidate Beta | Reject; do not switch candidates | Session | §4, §8.2 |
| T3 | Before block 0 arrives, a block with the same package ID but a different `numBlocks` | Reject | Session | §8.2 |
| T4 | Swap the manifest and image content between blocks 0 and 1 | Reject | Struct | §5, §6 |
| T5a | Clear `not main flash`, keeping the SUF2 family ID | Reject | Format | §3 |
| T5b | Set an extra UF2 flag, keeping the SUF2 family ID | Reject | Format | §3 |
| T5c | Nonzero `targetAddr` | Reject | Format | §3 |
| T6a | Payload size other than 272 | Reject | Format | §3 |
| T6b | Nonzero byte in `data[272..475]` | Reject | Format | §3 |
| T6c | Nonzero padding in the last image block, sent before block 0 | Reject when block 0 arrives | Format | §5, §8.2 |
| T6d | Nonzero padding in the last image block, sent after block 0 | Reject when the block arrives, and before installation | Format | §5, §8.1, §8.2 |
| T7a | While collecting a valid package, a sector with a wrong first magic number | Ignore; the session is unchanged and the package then Accepts | — | §8.1 |
| T7b | As T7a, with a wrong second magic number | Ignore; as T7a | — | §8.1 |
| T7c | As T7a, with a wrong end magic number | Ignore; as T7a | — | §8.1 |
| T8 | Send the highest-numbered block first, stop early, then send the rest | Incomplete, then Accept | — | §8.2 |
| T9 | Fail a session with a conflicting duplicate, reset locally, then send a valid package | Session fails, then the new session Accepts | Session | §8.2 |
| T10 | Stage part of candidate Alpha, reset, then send only Alpha's missing blocks | Incomplete; staged blocks from before the reset do not count | — | §8.2 |
| T11a | *(white-box)* Derive the chunk count at the largest `u32` image length | No arithmetic wraparound | Struct | §5, §6 |
| T11b | *(white-box)* Call the execution-range check directly with `base = 0xFFFFFFF0` and `L = 0x20`, using a policy that accepts address ranges | Reject without wraparound, before storage access | Policy | §6, §9.1 |
| T12 | *(hook)* Run 100 cycles: stage several valid blocks of a candidate, fail the session with a conflicting duplicate, then reset | Peak usage stays within staging capacity and does not grow across cycles | Session | §8.2 |
| T13a | A complete, valid plain-UF2 image for the device's own MCU family, alone and during collection | Ignore; nothing is installed and the session is unchanged | — | §8.1 |
| T13b | SUF2 blocks with `familyID present` clear, alone and during collection | Ignore; nothing is installed and the session is unchanged | — | §8.1 |
| T14 | After a conflicting duplicate fails the session, send more SUF2 blocks, including a whole valid package, before reset | Drop and report each block; the session stays failed | Session | §8.2 |
| T15 | Package ID mismatch | Reject | Struct | §4, §8.2 |
| T16 | After candidate Alpha completes, an identical repeated block, then a different SUF2 block | Ignore the duplicate; drop and report the other; Alpha is unchanged | — | §8.2 |
| T17 | After block 0 arrives, a block with the same package ID and a conflicting `numBlocks` | Reject | Session | §8.2 |
| I3 | While collecting, send a block whose `blockNo` equals `numBlocks` and whose `numBlocks` matches the session | Reject before staging | Block | §5, §8.2 |
| T18a | No session; `numBlocks = 1` | Reject before staging | Block | §8.2 |
| T18b | No session; `numBlocks = 34` (above the block limit) | Reject before staging | Block | §8.2 |
| T18c | No session; `blockNo >= numBlocks` | Reject before staging | Block | §8.2 |
| T18d | No session; `numBlocks = 0` | Reject before staging | Block | §8.2 |
| T18e | No session; `numBlocks = 0xFFFFFFFF` | Reject before staging | Block | §8.2 |
| T19 | *(hook)* While candidate Alpha is being verified or installed, send a malformed block or one for candidate Beta | Drop and report the block; Alpha is unaffected | — | §8.2 |
| T20 | As the first SUF2 block, a malformed block that passes the T18 bounds (for example, wrong payload size) | Reject without staging | Format | §8.2 |
| T21 | *(hook)* Reset during verification, then test a new candidate | The old session authorizes nothing; the new candidate is checked from scratch | — | §8.2 |
| T22 | Send an incomplete package, then close the file or let a timeout expire; follow with a fresh complete copy | Nothing is installed after the close or timeout; the fresh copy is Accepted | — | §8.2 |

Further setup for the less usual cases:

- **T4:** use a two-block package with valid transport fields and block numbering, but swap the manifest and image content. Derive the package ID from the content in block 0, whose first bytes are not the manifest magic.
- **T11a:** use `L = 0xFFFFFFFF`. **T11b:** call the range-check function directly; the fixture's fixed base list would reject this base before any range arithmetic runs, which makes the packaged form the same as S11a.
- **T15:** try a package ID computed over manifest bytes 0–191, and one left unchanged after block 0 was altered.
- **T12:** the 100 cycles are a test parameter, not a wire-format limit. The hook measures peak staging use, and each cycle must stage real blocks so that leaks in staging or completeness metadata show up.
- **T19 and T21:** use a hook to pause verification or installation. For T21, run once with candidate Beta after reset and once with Alpha resent.

## 5. Encryption

An encrypted package must pass key selection and authentication before any decrypted bytes are trusted, so all of these reject.

| ID | Case | Stage | Spec |
| --- | --- | --- | --- |
| E1 | Change the tag; keep the ciphertext and all other fields | GCM | §7, §9.1 |
| E2 | Change the nonce; keep the ciphertext and tag made with the original | GCM | §7, §9.1 |
| E3 | Flip a ciphertext bit and update the stored digest; keep the original tag | GCM | §7, §9.1 |
| E4 | Change the plaintext digest; keep the original ciphertext and tag | GCM | §7, §9.1 |
| E5 | Encryption-key ID with no provisioned key | AESKey | §6.1, §9.1 |
| E6 | Validly signed signed-only package sent to a device configured to require encryption (V4 must still Accept on that device) | Policy | §9.3 |
| E7 | Clear the encrypted flag, zero the encryption-key ID, nonce and tag, and set the stored digest equal to the plaintext digest; keep the original signature | Sig | §6, §9.1 |
| E8 | *(hook)* Encrypted candidate that fails GCM, with verification-path selection observed | GCM | §9.1 |
| E9a | Compute the AAD without the `SUF2-AEAD-V1` prefix | GCM | §7 |
| E9b | Compute the AAD over manifest bytes 0–191 | GCM | §7 |
| E10 | Encrypt with provisioned key K2 but carry K1's ID | GCM | §6.1, §9.1 |

E1–E4, E9a–E9b and E10 are re-signed so they reach GCM. E10 is the encryption-side counterpart of S6: the key ID selects K1, so a receiver that tries every provisioned key would wrongly accept it. E7 changes the encrypted flag and related fields, so the existing signature no longer authenticates the manifest. E8 needs a hook to confirm that failed encrypted verification never falls back to signed-only handling.

## 6. Platform integration and assurance tests

Installation, recovery and update interfaces vary by platform, so these checks need a platform-specific fixture. Passing them does not by itself establish a secure-boot claim.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| B1 | A candidate fails authentication while a valid application is installed | The installed application is unchanged | §8.2 |
| B2 | Cut power at each documented journal transition, then reboot | No partial or unverified image starts; resume safely or enter recovery | DG 3; TC 2 (item 7) |
| B3 | Modify one byte of the installed application | No boot | §9.2 |
| B4 | Modify the retained manifest | No boot | §9.2 |
| B5 | Boot with no valid installed image | No boot | §9.2 |
| B6 | Request update mode but provide no valid package | Stay in the update or recovery path; start nothing unauthorized | DG 5; TC 2 (item 7) |
| B7 | Provide a valid package after requesting update mode | Install only after verification; boot only after boot checks pass | §9; DG 2 |
| B8 | An older, correctly signed image that otherwise meets policy | Accept unless the platform documents its own rollback policy | §1, §12 |
| B9 | Modify staged bytes after verification but before installation | Re-verify or reject; the modified bytes are not installed | DG 2 |
| B10 | Inspect update-interface files and outputs before package authentication | No keys and no unauthenticated plaintext are exposed | DG 8 |
| B11 | Forge or corrupt journal fields, then reboot | The journal cannot skip verification; resume safely or enter recovery | DG 3; TC 2 (items 3, 7) |
| B12 | Try to change protected policy through package metadata or persistent update state | Rejected; policy is unchanged | §6, §9 |
| B13 | Try to use a recovery interface to install unauthorized code or alter protected state | Blocked; unauthorized code does not run | DG 3, 5; TC 2 (item 3) |
| B14 | Use an update-mode request or interface command to bypass package authorization | Rejected, or limited to selecting the update path | DG 5; TC 2 (item 3) |
| B15 | Store the manifest apart from the image, with flash padding after the L-byte image | Boot succeeds for a valid image; the hash covers exactly L bytes | §9.2 |
| B16 | Boot an image installed from an encrypted package; keep the signed manifest and only the plaintext image | Boot succeeds; the loader needs no ciphertext and does not recheck the stored digest or tag | §9.2 |
| B17 | Submit a valid package with an allowed execution base while tracing flash writes | Installation writes only to the destination selected by local policy | §6, §9.1 |
| B18 | Install an image, then withdraw its product ID or execution base from local boot policy | No boot | §9.2 |
| B19 | Install a signed-only image while encryption is optional, then enable the encryption-required update policy | Boot succeeds; encryption policy applies at package acceptance, not boot | §9.2, §9.3 |
| B20 | Build the firmware with the normal production build path, then submit a package signed by test key A | Reject; the production build does not provision any test signing key | DG 6; TC 6 |
| B21 | Replace the retained signature with its mathematically equivalent high-S form, then reboot | No boot | §7, §9.2 |
| B22 | Replace the retained manifest with one that has image length 0 and the SHA-256 of the empty string as plaintext digest, re-signed with key A | No boot | §6, §9.2 |
| B23 | Replace the retained manifest with one that has an unsupported format version, re-signed with key A | No boot | §6, §9.2, §11 |
| B24 | Replace the retained manifest with one that has a nonzero reserved byte, re-signed with key A | No boot | §6, §9.2 |

B2 and B11 apply only to platforms with a journal. B6, B10 and B12–B14 need platform-specific interface fixtures. B8 should record which rollback policy applies. B18 needs a protected-policy fixture that can withdraw an allowed product ID or execution base. B20 checks the production build output; do not use production key material. For B21, change `s` to `n − s` in a valid retained manifest signature. B22–B24 are signed correctly, so the loader must reject them on structure alone.

## 7. Producer and signing-pipeline tests (planned)

These planned checks cover producer behavior. The fixtures make the rules testable; they do not measure randomness or consume the real 2^32 nonce budget.

| ID | Case | Expected | Spec |
| --- | --- | --- | --- |
| PR1 | Build encrypted packages with a fixture CSPRNG that returns a different 12-byte value per call; try to supply a nonce through user-facing inputs | One nonce is requested per encryption; a user-supplied nonce is refused | §7 |
| PR2 | Use a shared ledger with a reduced budget across two producer instances, including a discarded encryption and a retry | Every encryption counts against the shared budget and the next one after the limit is refused; copying an existing package costs nothing | §7 |
| PR3 | Configure a test signer to return a valid high-S signature | Producer replaces `s` with `n − s` and emits a valid low-S signature | §7 |

## 8. Robustness activities

Use these alongside the fixed cases; record the corpus, resource limits and test budget so another run can reproduce the result.

- Fuzz the UF2 block parser, the manifest parser and the receiver state machine. No input may cause a crash, out-of-bounds access, unauthorized install or state growth beyond the configured limits.
- Exercise FAT-style sector writes: partial writes, rewrites and large unrelated writes. Non-SUF2 data must never look like an installable package, and ignored sectors must not change session state.

## 9. Vector plan

Before calling this catalogue a conformance standard, publish signed-only and encrypted package vectors with test-only keys. Include the intermediate manifest fields, hashes, signatures, tags, package IDs and expected verification stage, and check them with independent producer and receiver implementations.
