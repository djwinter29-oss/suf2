# .NET signed-only SUF2 packer

Independent implementation of the draft SUF2 format version 1 in
[`docs/spec/suf2-specification.md`](../../docs/spec/suf2-specification.md).
Requires .NET SDK 10; uses only the .NET standard libraries, with no NuGet packages.

```sh
dotnet build host/dotnet/Suf2.csproj -c Release
dotnet run --project host/dotnet/Suf2.csproj -c Release --no-build -- --help
dotnet run --project host/dotnet/Suf2.csproj -c Release --no-build -- pack \
  --input firmware.bin --output firmware.suf2 --key signing-private.pem \
  --product-id 00112233445566778899aabbccddeeff --base 0x08004000
```

Focused checks (Python 3 and OpenSSL required only for these checks):

```sh
python3 host/dotnet/selfcheck.py
```

Checks exercise BIN/HEX/ELF, sparse and malformed inputs, exact package bytes,
low-S ranges, independent OpenSSL signature verification and overwrite refusal.
Ephemeral private keys live in an ignored, private `host/dotnet/.checks/`
directory and are removed after the run.

Generate an **unencrypted PKCS#8 P-256 private PEM** in a private signing
environment, for example with `openssl genpkey -algorithm EC -pkeyopt
ec_paramgen_curve:P-256 -out signing-private.pem`. Do not commit private keys.
SEC1 `EC PRIVATE KEY`, encrypted PEM, other curves and public-only PEM are rejected.

## Options and input rules

`pack` requires `--input`, `--output`, `--key`, `--product-id` (exactly 32 hex
characters), and `--base`. Addresses accept decimal or `0x` hexadecimal. Base
must fit a 32-bit unsigned address. `--end` is exclusive and may equal
`0x100000000`; `--fill` is a decimal or `0x` byte from 0 to 255. Options may
appear in any order; duplicate and unknown options are errors.

* **BIN:** mapped at the explicitly supplied base. Empty images are rejected.
* **Intel HEX:** checksums, lengths, EOF and record types 0–5 are checked.
  Extended segment/linear addresses are applied; type 3/5 entry points are
  validated but do not change the load base. Overlaps, trailing records and
  32-bit address overflow are rejected. Data records may not cross a 64 KiB
  address window.
* **ELF:** only little-endian ELF32/ELF64 `ET_EXEC`. `PT_LOAD` file bytes are
  mapped at `p_paddr`, not `p_vaddr` or the entry point. Bounds and
  `filesz <= memsz` are checked. BSS (`memsz - filesz`) is **not** synthesized.
  Extended program-header counts are unsupported.

For HEX/ELF, base must equal the lowest address containing loaded file bytes.
All loaded bytes must fit the selected range: no section or segment is silently
discarded. Loaded segments must never overlap, even if their bytes agree.
Input files are limited to 16 MiB, including HEX text and ELF metadata. The
resulting contiguous image must contain 1 byte to 16 MiB and stay inside the
32-bit address space.

Gaps, including an explicitly extended tail, are rejected unless **both**
`--end` and `--fill` are supplied. For example, add `--end 0x08008000 --fill
0xff` to deliberately materialize holes and a tail up to that address. A fill
without an end is always rejected; an end alone is allowed only if it exactly
matches an already contiguous image's natural end.

The output uses exclusive creation and refuses overwrite. Failures go to stderr
with a nonzero exit code, without stack dumps or key contents.

## Draft signed-only limits

Output uses 512-byte UF2 blocks, a 256-byte signed manifest and 256-byte image
chunks. ECDSA P-256/SHA-256 signatures are fixed-width P1363 with low-S
normalization. Key and package IDs follow the specification's SHA-256 rules.
All encryption and reserved fields are zero. There are no encryption options,
receiver, device-policy checks, anti-rollback guarantees, or production signing
key management. This packer does not make legacy UF2 loaders safe for SUF2.
