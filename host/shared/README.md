# Shared host contract tests

The [tests/](tests/) check compatibility between Python, .NET and dashboard
producers. Key-format examples, the generator and ignored local test keys now
live in root-level [test-keys/](../../test-keys/README.md).

The packers accept P-256 PKCS#8 PEM private keys, not certificates as signing
inputs. Firmware trusts provisioned public keys, not certificate issuers. Its
signing-key ID is derived from the uncompressed 65-byte EC point, not a container
fingerprint. AES encryption is not yet implemented in the host packers.
