# SUF2 specification

Requirement words MUST, MUST NOT, SHOULD and MAY are used in the RFC 2119 sense.

## 1. Scope

SUF2 defines a single-image, authenticated firmware package carried in UF2 blocks, with optional AES-256-GCM encryption in transit. It defines the package, the receiver behavior, and the installed-image verification rule. It does not define a bootloader, a transport other than UF2 blocks, key distribution, or anti-rollback.

The specification answers the threat model in [threat-model.md](reference/threat-model.md); the mapping from threats to specification sections is in [security-design.md](security/security-design.md). It relies on the platform guarantees in [trust-chain.md](reference/trust-chain.md).

## 2. Conventions

- Multi-byte integers in the UF2 block header, SUF2 record header and manifest are unsigned little-endian unless stated otherwise. This includes the UF2 numeric fields and every multi-byte field in the record and manifest tables below.
- Byte strings such as identifiers, nonces, digests and tags are copied in the listed order; integer endianness does not apply to them. The ECDSA signature is the exception explicitly encoded as two 32-byte big-endian integers, `r` followed by `s`.
- "Reserved, zero" fields MUST be written as zero and MUST be rejected if nonzero.
- A receiver MUST reject anything this document does not define. There is no extension mechanism in v0.1.

## 3. UF2 transport profile

SUF2 keeps the 512-byte UF2 block structure and its magic numbers, as described in the upstream UF2 specification.

| Outer UF2 field | SUF2 rule |
| --- | --- |
| flags | `not main flash` (0x00000001) and `familyID present` (0x00002000) MUST both be set; all other flags MUST be zero |
| familyID | A SUF2 transport family ID. **Open item:** value to be chosen randomly and registered upstream. It is not a stock MCU family ID. |
| targetAddr | MUST be zero. It is never a flash destination. |
| payloadSize | MUST be 256 |
| blockNo | Canonical index starting at 0 |
| numBlocks | MUST equal 2 + N, with N the number of image chunks |
| data[0..255] | A 32-byte SUF2 record header, then up to 224 bytes of content; unused bytes zero |
| data[256..475] | Zero |

Rationale: setting `not main flash` and using a distinct family ID means a legacy UF2 bootloader that ignores SUF2 should not write the plaintext to flash at an address. This is a safety measure, not a guarantee, and the specification does not claim every historical bootloader honors it.

## 4. Record header (32 bytes)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `SUF2` |
| 4 | 1 | Record format version (1) |
| 5 | 1 | Record kind: 1 = manifest, 2 = image data |
| 6 | 2 | Reserved, zero |
| 8 | 16 | Package identifier |
| 24 | 4 | Logical byte offset within the manifest or stored image |
| 28 | 2 | Content length, at most 224 |
| 30 | 2 | Reserved, zero |

The **package identifier** is the first 16 bytes of SHA-256 over the complete 256-byte manifest, including its signature. It routes records to a candidate. It confers no trust and is not an authentication primitive.

## 5. Package layout

A package consists of two manifest records and N image records, N = ceil(stored_length / 224).

- Manifest records: offsets 0 and 224, content lengths 224 and 32.
- Image records: offset = 224 × chunk index. Only the last chunk may be short.
- `blockNo` 0 and 1 are the manifest; 2 … N+1 are image chunks.

Producers SHOULD emit blocks in ascending order. Receivers MUST accept any arrival order.

## 6. Manifest (256 bytes)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | Manifest magic (constant to be frozen with vectors) |
| 8 | 2 | Manifest version (1) |
| 10 | 2 | Manifest length (256) |
| 12 | 2 | Signature suite: 1 = ECDSA P-256 with SHA-256 |
| 14 | 2 | Flags: bit 0 = encrypted; all others zero |
| 16 | 16 | Product/board identifier |
| 32 | 4 | Logical application load address |
| 36 | 4 | Plaintext image length |
| 40 | 4 | Stored image length |
| 44 | 4 | Number of image chunks |
| 48 | 16 | Signing-key identifier |
| 64 | 16 | Encryption-key identifier (zero when signed-only) |
| 80 | 12 | AES-GCM nonce (zero when signed-only) |
| 92 | 2 | Encryption suite: 0 = none, 1 = AES-256-GCM |
| 94 | 2 | Digest suite: 1 = SHA-256 |
| 96 | 32 | SHA-256 of plaintext image |
| 128 | 32 | SHA-256 of stored image |
| 160 | 16 | Detached GCM tag (zero when signed-only) |
| 176 | 16 | Reserved, zero |
| 192 | 64 | Signature, r ‖ s, 32 bytes each, big-endian |

Rules:

- Plaintext and stored lengths MUST be equal and nonzero.
- Signed-only: stored bytes equal plaintext; both digests MUST match.
- Encrypted: stored bytes are ciphertext without the tag.
- The flag and the encryption suite MUST agree; a mismatch is an error.
- The load address is a logical value. The device maps it through local policy and MUST NOT write to flash based on it alone.

## 7. Cryptographic construction

Producer order:

1. Fill bytes 0..127 (including the plaintext digest).
2. If encrypting: generate a fresh 96-bit nonce from a CSPRNG, then AES-256-GCM encrypt with AAD = `"SUF2-AEAD-V1"` ‖ `0x00` ‖ manifest[0..127].
3. Hash the stored bytes into 128..159. Put the GCM tag in 160..175.
4. Compute SHA-256 over `"SUF2-MANIFEST-V1"` ‖ `0x00` ‖ manifest[0..191], and sign that digest with ECDSA P-256. Implementations MUST state whether their library call takes the message or a prehash, to avoid double hashing.
5. Encode r ‖ s. Producers emit low-S. Verifiers MUST reject out-of-range or zero r or s and MUST reject non-low-S.
6. Compute the package identifier and emit the records.

This order avoids a circular dependency between tag, ciphertext digest and signature.

Verifier order (see section 9): signature first, then stored digest, then GCM tag and decryption, then plaintext digest. The signing key ID selects only among keys provisioned locally; keys carried in a package are never trusted.

## 8. Receiver behavior

- A session starts with fixed local limits for image size, block count and staging capacity.
- The first structurally valid SUF2 record selects the package identifier. Image records may arrive before the manifest and stay untrusted and bounded until verification.
- A different package identifier, or a duplicate with different content, fails the session. The receiver MUST NOT switch candidates silently.
- Identical duplicates are accepted and do not change completeness.
- Sectors that are not SUF2 records are ignored. Recognized but malformed SUF2 records produce an explicit error.
- Completeness is tracked per block. It MUST NOT be inferred from a timeout, file close, or arrival of the highest block.
- A failed or incomplete session can be reset. Staging may be discarded. The installed application is untouched.
- A timeout MAY discard an incomplete session and MUST NOT authenticate or commit one.

## 9. Verification

Candidate verification (before install) MUST check, in this order:

1. Structure, lengths, counts, reserved bits, supported suites.
2. Local policy: product identifier, load address and size limits, and whether encryption is required.
3. Signature with the provisioned key selected by key ID.
4. Stored-image digest over the staged bytes.
5. For encrypted packages: GCM authentication over the ciphertext and AAD before any plaintext is installed, then the plaintext digest after decryption.

Any failure rejects the package. A failure MUST NOT fall back to a weaker mode.

**Installed-image verification (every boot).** The loader retains the exact signed manifest alongside the installed plaintext. Before executing, it MUST verify the manifest signature against the provisioned key, check local policy, hash the installed bytes and compare against the plaintext digest. If any step fails it MUST NOT execute the application and MUST enter recovery.

### 9.1 Encryption policy

A receiver MAY be configured to require encryption. A signed-only package is then rejected. An encrypted flag is never evidence of authenticity: the signature check still applies.

## 10. Installation (informative)

Installation is platform-specific and outside the portable core. The recommended pattern is stage, verify, journal, copy in bounded steps, verify the installed bytes, then reboot. See [design-guidelines.md](security/design-guidelines.md).

## 11. Versioning

The record version, manifest version and suite fields identify the format. A receiver rejects versions and suites it does not implement. Published constants MUST NOT be changed silently; a change requires a new version.

## 12. Explicit non-features in v0.1

Anti-rollback or security counters, timestamps as authorization, extensible tags, compression, sparse or multi-component images, bootloader self-update, public-key recipient wrapping, key distribution, and encryption at rest.

## 13. Open questions

| Item | Notes |
| --- | --- |
| Family ID | Choose and register a SUF2 transport family ID upstream |
| Manifest magic and key-ID derivation | Freeze with vectors |
| Cost of per-boot SHA-256 | Measure on Cortex-M0+ for large images |
| P-256 verify cost and footprint on RP2040 | Measure with a pinned crypto library |
| Reuse of an existing image format | Decide using [related-work.md](related-work.md) before freezing |
| Two-pass decrypt-then-install | Requires staging; decide whether v0.1 includes it |
| Whether non-low-S rejection is needed | Compare with library behavior |
