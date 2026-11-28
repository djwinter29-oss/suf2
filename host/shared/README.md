# Shared keys, certificates and host contract

The three applications use the same formats:

| Material | Practical format | Purpose |
| --- | --- | --- |
| Signing private key | PKCS#8 PEM (`BEGIN PRIVATE KEY`), P-256 | Local signing; never published or embedded in the dashboard |
| Signing public key | SubjectPublicKeyInfo PEM (`BEGIN PUBLIC KEY`) | Provisioning/export container |
| Example certificate | Self-signed X.509 PEM (`BEGIN CERTIFICATE`) | Shows a common public-key container; not a CA or SUF2 authorization |

The packers currently accept private keys, **not certificates** as signing inputs.
The loader trusts provisioned P-256 public keys, not certificate issuers.
Its key ID is derived from the uncompressed 65-byte EC point, not from PEM text,
certificate DER, SPKI DER or a certificate fingerprint.

Generate fresh **test-only** material with Python and `cryptography` installed:

```sh
python host/shared/generate-test-keys.py
```

The files go into ignored `local/`, including a matching private key, public key
and self-signed certificate. Existing files are never overwritten. Do not
commit these private keys or provision test keys on production devices.
The generator is a development convenience, not a production provisioning system.

Public-only samples in [examples/](examples/) illustrate the PEM formats; their
matching private key is deliberately not shipped. Generate local material to
try signing.

For production, use a separate protected signing pipeline or HSM. The current
prototype accepts unencrypted PEM for portability across Python, .NET and WebCrypto;
it does not yet support encrypted PEM, PKCS#12/PFX or hardware signers.
Never place a private key in this repository, GitHub Pages, a command argument,
browser storage or a public test vector.
