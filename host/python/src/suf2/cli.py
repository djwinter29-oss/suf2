import argparse
import sys
from pathlib import Path

from .image import load
from .packer import pack


def integer(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("Use a decimal integer or a 0x-prefixed hex integer") from error


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack signed-only firmware using the draft SUF2 format.")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("pack")
    command.add_argument("--input", type=Path, required=True)
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--key", type=Path, required=True, help="Unencrypted PKCS#8 PEM P-256 private key")
    command.add_argument("--product-id", required=True, help="Exactly 32 hexadecimal digits")
    command.add_argument("--base", type=integer, required=True)
    command.add_argument("--end", type=integer, help="Exclusive image end, required with --fill")
    command.add_argument("--fill", type=integer, help="Explicit byte for gaps and trailing padding")
    args = parser.parse_args()
    try:
        if len(args.product_id) != 32 or any(c not in "0123456789abcdefABCDEF" for c in args.product_id):
            raise ValueError("Product ID must be exactly 32 hexadecimal digits")
        with args.key.open("rb") as source:
            pem = source.read(65537)
        if len(pem) > 65536:
            raise ValueError("Signing-key file is too large")
        result = pack(load(args.input, args.base, args.end, args.fill),
                      args.base, bytes.fromhex(args.product_id), pem)
        with args.output.open("xb") as output:
            output.write(result)
    except (OSError, ValueError, TypeError) as error:
        if isinstance(error, OSError):
            message = f"File operation failed: {error.strerror}"
        else:
            message = str(error)
        print(f"suf2: {message}", file=sys.stderr)
        return 1
    print(f"Created {args.output} ({len(result)} bytes, signed-only)")
    return 0
