# SUF2 secure design guidelines

Guidance for implementers of SUF2 receivers, loaders and host tools. It complements [suf2-specification.md](../suf2-specification.md) (what must hold) with how to build it safely. Terms follow the generic threat model in [threat-model.md](../reference/threat-model.md); what SUF2 does about each threat is in [security-design.md](security-design.md).

## 1. Parsing untrusted input

- Treat every UF2 sector as hostile until fully validated.
- Use the fixed block and manifest layouts; reject unsupported format versions, unknown flags and nonzero reserved fields. Algorithms are fixed by the version.
- Use checked arithmetic for every offset, length and count. Reject overflow and overlap.
- Never allocate based on an untrusted count. Use caller-owned, bounded memory and a block bitmap.
- Never write to flash using the UF2 `targetAddr` or the manifest load address directly. The manifest address is the intended execution base; local policy checks that base and image length and selects the physical installation destination.
- Derive chunk offsets and final padding from `blockNo` and the signed image length. The 16-byte package ID associates blocks; it does not authenticate them.
- Fuzz the receiver and verifier before any release claim.

## 2. Stage, verify, then install

1. Collect the whole candidate in staging storage.
2. Verify signature, digests and, if encrypted, the GCM tag. A rejected package changes no installed image or boot-eligibility state.
3. Re-verify the staged data immediately before any destructive step, because staging is mutable storage.
4. Before modifying a destination that is currently bootable, durably invalidate its boot eligibility or ensure trusted boot-time verification rejects changed bytes. Keep an inactive candidate destination ineligible without invalidating a preserved active image. If no eligible image remains, trusted recovery must not depend on the image being overwritten.
5. Install in bounded steps under a journal.
6. Verify the installed bytes, then mark the image bootable as the final commit.
7. Reboot and verify again on boot.

Never feed GCM streaming output to the active slot before the final tag and plaintext digest have been authenticated. Keep verified staging unchanged through installation, and verify the installed bytes before making them bootable. Choose the decryption storage strategy per platform; the file format does not prescribe one.

## 3. Interruption safety

- Persist enough state in a journal to resume: which phase, which package, and the authenticated information needed to restart.
- Treat the journal as untrusted input: validate every field, and never let journal state mark an image bootable or skip verification.
- On restart, re-authenticate staging, then resume or enter recovery. Do not execute a partially written application.
- Mark an image valid only after complete verification, as the last write.
- Preserving the old image through every power loss is not promised. Recovery mode is the guarantee.
- Test fault injection before and after every journal and commit transition.

## 4. Boot verification

- Verify the retained signed manifest and the installed bytes on every boot.
- On failure, enter update mode; do not run the application.
- Document the boot-time cost, which grows with image size, and measure it on the target.
- Per-boot verification detects modification. It does not prevent it; that needs the platform guarantees in [trust-chain.md](../reference/trust-chain.md).

## 5. Update entry

- No timed windows or double-reset gestures; they are unreliable and widen the attack surface.
- Without a valid image, the loader exposes update mode.
- A valid application MAY request a one-shot reboot into update mode. The request grants nothing: a valid signed package is still required.
- After a failed transfer, remain in update mode. Reset the session on link disconnect, not on filesystem flush.

## 6. Keys

- Keep signing keys on the host, offline where possible. Never put private keys in firmware, source, fixtures or command-line literals.
- Provision public keys (or hashes) in a protected location. Key IDs only select among provisioned keys.
- Label test keys and make production builds reject them.
- Several provisioned verification keys are allowed. Rotation and revocation are deferred.
- For encryption: a fresh CSPRNG nonce per AES-GCM invocation, no user nonce override, at most 2^32 invocations per key across all producers combined, one symmetric key per product or fleet segment, and awareness that one extracted key exposes every package encrypted with it. Count discarded and retried encryptions; copying an existing package does not add an invocation. Enforce the lifetime budget in the provisioning and signing pipeline, not separately per workstation or build job.

## 7. Cryptography

- Use maintained libraries. Do not write primitives.
- Validate public keys and reject malformed or non-canonical signatures.
- State prehash versus message semantics for each call to avoid double hashing.
- Constant-time comparison for secrets and tags.
- Never retry a failed encrypted verification as signed-only.

## 8. Error handling

- Give distinguishable statuses for malformed input, policy rejection, cryptographic failure and storage failure.
- A callback that does not perform its operation must never report success.
- Do not expose private keys, decryption keys or protected plaintext through any virtual file.

## 9. Resource planning

- Compute the flash map per board from the real loader footprint and erase alignment, not an assumed universal layout.
- Staging roughly doubles the storage needed for the application. Report the maximum accepted image size per board.
- On RP2040/RP2350, flash erase and program run from RAM with XIP unavailable; keep interrupts and the second core in mind during writes.

## 10. Reviewer checklist

- Can any package field choose a trust root, address or policy?
- Can a failed package alter installed state?
- Can a partial install boot?
- Is every boot verified, and what happens on failure?
- Are secrets absent from the repository and the firmware?
- Does the platform document which threats remain open?
