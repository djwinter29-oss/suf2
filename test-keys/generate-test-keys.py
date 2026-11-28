"""Create local test material; never publish or provision the private key."""

import argparse
import base64
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import secrets

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def signing_material():
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "SUF2 TEST ONLY - NOT FOR PRODUCTION")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    return (
        key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                          serialization.NoEncryption()),
        key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo),
        cert.public_bytes(serialization.Encoding.PEM),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate TEST-ONLY P-256 and AES-256 keys locally.")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "local")
    parser.add_argument("--aes-only", action="store_true", help="Add an AES key without changing signing keys")
    args = parser.parse_args()
    names = [] if args.aes_only else ["signing-private.pem", "signing-public.pem", "signing-test-cert.pem"]
    names.append("encryption-key.jwk")
    paths = [args.output / name for name in names]
    if any(path.exists() for path in paths):
        parser.error("Output files already exist; refusing to overwrite keys")
    args.output.mkdir(parents=True, exist_ok=True)
    args.output.chmod(0o700)
    label = secrets.token_bytes(16)
    while not any(label):
        label = secrets.token_bytes(16)
    jwk = {
        "kty": "oct", "alg": "A256GCM", "kid": label.hex(),
        "k": base64.urlsafe_b64encode(secrets.token_bytes(32)).rstrip(b"=").decode("ascii"),
        "key_ops": ["encrypt", "decrypt"], "test_only": True,
    }
    values = list(signing_material()) if not args.aes_only else []
    values.append((json.dumps(jwk, indent=2) + "\n").encode("ascii"))
    for path, value in zip(paths, values):
        with path.open("xb") as file:
            path.chmod(0o600)
            file.write(value)
    print("Created local TEST-ONLY key material. Never use it in production.")


if __name__ == "__main__":
    main()
