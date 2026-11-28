# SUF2 dashboard

A dependency-free, static, signed-only firmware packer. HTML, CSS and ES modules
run entirely in the browser using WebCrypto. There are no remote scripts, fonts,
uploads, analytics, cookies, service workers, or local/session storage.

**Encryption is not supported.** Firmware remains readable. AES-GCM support is
deferred until a global, shared per-key invocation budget can be enforced across
all producers, including discarded outputs and retries.

## Run locally

From the repository root:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory host/dashboard
```

Open `http://localhost:8000`. Use a current browser with WebCrypto and ES modules.
HTTPS or a localhost secure context is required; opening `index.html` directly as
a `file:` URL is not supported. No build or dependency installation is needed.

1. Select BIN, Intel HEX, or little-endian ELF32/64 ET_EXEC firmware. Check the
   detected format. Both the input and the normalized image are limited to 16 MiB.
2. Enter the vendor's 32-hex-digit product ID and required execution base. BIN is
   mapped at this base. For HEX/ELF it must be the lowest included byte address.
3. If the image has gaps, provide **both** an exclusive end address and a fill byte.
   The same pair can explicitly add trailing fill. All file-backed bytes must fit;
   no out-of-range segments are discarded. Overlaps, including identical overlaps,
   are rejected. ELF uses PT_LOAD `p_filesz` at `p_paddr`, requires
   `p_filesz <= p_memsz`, and ignores BSS. HEX start-address records and ELF entry
   points do not override the base.
4. Supply one unencrypted ECDSA P-256 PKCS#8 PEM (`BEGIN PRIVATE KEY`) file or paste
   its contents. SEC1 (`BEGIN EC PRIVATE KEY`), encrypted PEM, other curves, and
   certificates are not accepted.
5. Sign and download `.suf2`. Key fields and application references are cleared
   after each packing attempt. Clear all also revokes the current download.

Output follows [`suf2-specification.md`](../../docs/spec/suf2-specification.md):
256-byte manifest, domain-separated SHA-256/ECDSA signature in P1363 low-S form,
point-derived signing-key ID, manifest-derived package ID, and ascending 512-byte
UF2 blocks with 256-byte image chunks. Signed-only encryption fields are zero;
plaintext and stored digests match. The dashboard supplies no signing trust:
devices must already trust a provisioned signing key and enforce product/range
policy. It does not add anti-rollback protection.

## Security boundary

**Hosted scripts can read any private key you provide.** HTTPS protects transport,
not against compromised hosting, altered scripts, browser extensions, or a
compromised browser/OS. The restrictive page CSP is defense in depth, not a trust
anchor. Review the source and serve an offline copy in a trusted environment.
For production signing, prefer a reviewed offline CLI or an HSM-backed signing
pipeline. Never use production keys on an untrusted hosted dashboard.

Private keys are used in memory only. A temporary extractable WebCrypto import
derives public coordinates, then signing uses a non-extractable import. DER bytes
are overwritten and key references released. JavaScript strings, browser-managed
file data, and cryptographic objects cannot be reliably zeroized; clearing inputs
does not guarantee secure memory erasure. A self-signed certificate is not a trust
anchor and does not make a signing key trusted by a device.

## Tests

Node 20 or newer; no npm install required:

```sh
npm --prefix host/dashboard run checks
npm --prefix host/dashboard test
```

Tests use Node's built-in runner and `crypto.webcrypto`, independently verify a
real signed package, inspect the wire layout, and reject malformed/sparse inputs.

### Module API

`input.js` exports `parseFirmware(input, { format, base, end, fill })`, where
`input` is an `ArrayBuffer` or `Uint8Array`, `format` is `"bin"`, `"hex"`, or
`"elf"`, and addresses/fill are numbers. Omit both `end` and `fill` for contiguous
input. It returns a normalized `Uint8Array`; malformed input throws.
`parseFirmware` is also re-exported by `packer.js`.

`packer.js` exports
`await packSigned({ image, base, productId, privateKeyPem }, crypto)`, with a
normalized `Uint8Array`, numeric base, 32-hex-character product ID, PEM string,
and optional WebCrypto provider (defaults to browser `globalThis.crypto`).
It returns `{ bytes, manifest, packageId, signingKeyId, imageDigest }`, all
`Uint8Array` values. Node callers can pass `webcrypto` from `node:crypto` and
write `bytes` with `node:fs`; no private key is returned.

## GitHub Pages

The public dashboard is at <https://djwinter29-oss.github.io/suf2/>.
Pages serves the `gh-pages` branch, which holds only the six public static
assets. The host CLIs, local keys and test fixtures are never published.

The [Pages workflow](../../.github/workflows/pages.yml) runs the dashboard
checks and tests, then force-pushes a fresh one-commit `gh-pages` branch with
that explicit asset list. It runs on dashboard changes pushed to `main` or
`master`, or by manual dispatch. Its only extra permission is
`contents: write`, which it needs to push the branch.

The workflow does not enable Pages itself. No repository settings need to be
changed to run locally. When hosted, the dashboard is served at the repository's
Pages root, not a `/host/dashboard` subpath.
