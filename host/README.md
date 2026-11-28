# Host firmware tools

SUF2 has three host applications with the same signed-only package contract:

| Application | Use |
| --- | --- |
| [python/](python/) | Python CLI for build scripts and automation |
| [dotnet/](dotnet/) | .NET CLI; no UI |
| [dashboard/](dashboard/) | Static browser dashboard, deployable to GitHub Pages |
| [shared/](shared/) | Key-format documentation, public examples and local test-key generation |

All three pack BIN, Intel HEX and little-endian ELF32/ELF64 into the current
[draft SUF2 format](../docs/spec/suf2-specification.md). Authentication is mandatory.
No tool offers an unsigned option.

Open the [public dashboard](https://djwinter29-oss.github.io/suf2/) to try
browser packing with a locally generated test key.

## One conversion contract

- Supply a 16-byte product ID as 32 hexadecimal digits, the intended execution
  base, and a P-256 PKCS#8 PEM private signing key.
- BIN has no address metadata; the supplied base identifies its execution base.
- HEX records and ELF file-backed `PT_LOAD` segments retain their physical
  addresses. The base must match the lowest loaded address.
- ELF BSS (`memsz - filesz`) is not firmware data and is not copied into the image.
  Entry-point records do not override the execution base.
- Every loaded byte is included. Overlaps are rejected, even identical ones.
- Gaps require an explicit exclusive end and fill byte. No automatic gap filling,
  address rebasing or dropping RAM/secondary regions.
- Inputs and the resulting image are limited to 16 MiB; the device can enforce a
  smaller local limit. The selected range must fit 32-bit addresses.
- The output is contiguous firmware with an authenticated manifest, SHA-256
  digests, a low-S P-256 signature and standard SUF2 block framing.

Signatures are randomized, so independently packed files need not be byte-identical.
Their image, signed fields and verification results must agree.
An authentic package is not proof that its address or image suits a device:
the loader still enforces protected local policy.

## Browser boundary

"Upload" means selecting a local file. The dashboard reads firmware and keys
in the browser and downloads the result; no server receives them. It has no
analytics, remote scripts or storage for private keys. However, the page's code
can read the selected key: trust the exact deployed version, and prefer an
offline CLI or protected signer for production. GitHub Pages supplies hosting,
not a trusted signing service.

## Current limits

These are runnable prototypes, not security-reviewed production tools.
Optional AES-GCM encryption is deliberately not implemented yet. The draft
requires a lifetime invocation budget shared across every producer using an
AES key; independent CLI processes and a static website cannot enforce that
without a defined shared signing/provisioning service. Do not claim full
encrypted-producer conformance or add a local counter as a substitute.

Key rotation, certificate trust-chain validation, remote signing, deployment
to devices and GitHub release automation are not part of these packers.

## Checks

```sh
PYTHONPATH=host/python/src python -m unittest discover -s host/python/tests -v
dotnet build host/dotnet -c Release
python host/dotnet/selfcheck.py
npm --prefix host/dashboard test
PYTHONPATH=host/python/src python -m unittest discover -s host/shared/tests -v
```

Run these from the repository root. The interoperability tests build BIN, HEX,
ELF32 and ELF64 packages with each producer, then independently verify the
signature, manifest, image and block framing with Python's crypto library.
They also exercise sparse conversion and wrong-curve rejection.
The [host CI workflow](../.github/workflows/host-tests.yml) runs the same checks.
The [Pages workflow](../.github/workflows/pages.yml) publishes only the explicit
static asset list, never keys, fixtures or test-runner caches.
