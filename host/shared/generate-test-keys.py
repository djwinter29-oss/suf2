"""Create local test material; never publish or provision the private key."""

import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate TEST-ONLY P-256 key material locally.")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "local")
    args = parser.parse_args()
    paths = [args.output / name for name in
             ("signing-private.pem", "signing-public.pem", "signing-test-cert.pem")]
    if any(path.exists() for path in paths):
        parser.error("Output files already exist; refusing to overwrite keys")
    args.output.mkdir(parents=True, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "SUF2 TEST ONLY - NOT FOR PRODUCTION")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    values = (
        key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                          serialization.NoEncryption()),
        key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo),
        cert.public_bytes(serialization.Encoding.PEM),
    )
    for path, value in zip(paths, values):
        with path.open("xb") as file:
            path.chmod(0o600)
            file.write(value)
    print("Created local TEST-ONLY keys and certificate. Never use them in production.")


if __name__ == "__main__":
    main()
