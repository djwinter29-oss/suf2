# Test keys and certificates

Development material only. Generate private keys locally; never commit them or
use these keys in production.

| Material | Format | Used by |
| --- | --- | --- |
| Signing private key | P-256 PKCS#8 PEM | Host signing only; never embedded in firmware |
| Signing public key | SubjectPublicKeyInfo PEM or X.509 certificate | Firmware signature verification |
| Encryption key | AES-256 octet JWK, with a 32-hex-digit `kid` | Firmware AES-GCM decryption |

```sh
python3 test-keys/generate-test-keys.py
```

Files are created in ignored `local/` with owner-only permissions. Existing
keys are never overwritten. To add an AES key to existing signing material:

```sh
python3 test-keys/generate-test-keys.py --aes-only
```

The JWK contains a secret 32-byte key in unpadded base64url `k`; `kid` is a
nonzero opaque 16-byte label, not a hash of the secret. Both producer and device
must use the same label. Public-only samples in [examples/](examples/) illustrate
signing containers; their matching private key is not shipped.

AES provisioning does not enable encryption in the three host packers: those
remain signing-only. An external encryption/signing pipeline must implement the
specification's nonce generation and shared per-key invocation budget. Test
fixtures use fresh disposable AES keys; they are not a production pipeline.

Firmware contains public signing keys and, when configured, secret AES keys.
Without hardware readout/debug protection, embedded AES keys can be extracted.
Generated headers and binaries belong in ignored `build/`; production keys must
come from a separately protected provisioning system, never this folder.
Do not publish headers or firmware binaries containing AES keys. CI artifact
builds disable AES provisioning explicitly. Select JWKs with the board CMake
option `SUF2_DECRYPTION_KEYS`; an explicit empty value provisions none.
The ESP32-S3 recovery application holds the AES keys; its second-stage bootloader
only needs public signing keys to verify installed plaintext.

See [host/shared/](../host/shared/) for cross-producer tests.
