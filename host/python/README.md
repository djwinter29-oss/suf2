# Python CLI

Requires Python 3.10+ and the `cryptography` library. From this folder:

```sh
python -m pip install .
suf2 pack --input firmware.bin --output firmware.suf2 \
  --key ../shared/local/signing-private.pem \
  --product-id 00112233445566778899aabbccddeeff --base 0x10010000
```

There is no UI. `python -m suf2` is an equivalent entry point.
The output must not already exist; errors go to stderr and return a nonzero exit code.

Inputs are BIN, Intel HEX or little-endian executable ELF32/ELF64.
`--base` is required and must match the lowest loaded address in HEX/ELF.
ELF uses `PT_LOAD` physical addresses and file-backed bytes, not BSS or the entry point.
Overlaps, malformed files and inputs/images above 16 MiB are rejected.
All loaded bytes must be included; a file with several unrelated load regions needs
to be exported as a single firmware image first.

Gaps are rejected unless both `--end` (exclusive) and `--fill` (0..255) are explicit:

```sh
suf2 pack --input firmware.hex --output firmware.suf2 \
  --key ../shared/local/signing-private.pem \
  --product-id 00112233445566778899aabbccddeeff \
  --base 0x10010000 --end 0x10011000 --fill 0xff
```

Fill bytes become part of the signed image and are installed. Check the range
against your loader's flash policy before packing.
Signing accepts unencrypted PKCS#8 PEM P-256 private keys only.
This prototype is **signed-only**, not a complete producer for the draft's optional encryption mode.
See the [shared host contract](../README.md) and [test-key instructions](../shared/README.md).

Tests without installing the package:

```sh
PYTHONPATH=src python -m unittest discover -s tests
```
