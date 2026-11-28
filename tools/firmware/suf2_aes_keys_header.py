#!/usr/bin/env python3
"""Generate a private firmware AES-key table from AES-256 octet JWK files."""

import argparse
import base64
import json
import os
from pathlib import Path
import re

from suf2_keys_header import is_under


def unique_fields(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError("duplicate JWK field")
        result[name] = value
    return result


def load_key(path):
    with path.open("rb") as file:
        data = file.read(16385)
    if len(data) > 16384:
        raise ValueError("JWK file is too large")
    jwk = json.loads(data, object_pairs_hook=unique_fields)
    if not isinstance(jwk, dict) or jwk.get("kty") != "oct" or jwk.get("alg", "A256GCM") != "A256GCM":
        raise ValueError("expected an AES-256 octet JWK")
    label, encoded = jwk.get("kid"), jwk.get("k")
    if not isinstance(label, str) or not re.fullmatch(r"[0-9a-fA-F]{32}", label) or int(label, 16) == 0:
        raise ValueError("kid must be a nonzero 16-byte label encoded as 32 hex digits")
    if not isinstance(encoded, str) or not re.fullmatch(r"[A-Za-z0-9_-]{43}", encoded):
        raise ValueError("k must be an unpadded base64url-encoded 32-byte AES key")
    key = base64.b64decode(encoded + "=", altchars=b"-_", validate=True)
    if len(key) != 32 or base64.urlsafe_b64encode(key).rstrip(b"=").decode("ascii") != encoded:
        raise ValueError("k must be a canonical base64url-encoded 32-byte AES key")
    operations = jwk.get("key_ops", ["decrypt"])
    if not isinstance(operations, list) or "decrypt" not in operations:
        raise ValueError("JWK must permit decryption")
    return bytes.fromhex(label), key, jwk.get("test_only", False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("keys", nargs="*", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--forbid-dir", type=Path, action="append", default=[])
    args = parser.parse_args()
    entries, labels, secrets = [], set(), set()
    for path in args.keys:
        try:
            label, key, test_only = load_key(path)
        except (OSError, ValueError) as error:
            parser.error(f"{path}: {error}")
        if args.production and (test_only or any(is_under(path, folder) for folder in args.forbid_dir)):
            parser.error(f"{path}: test AES key is not allowed in a production build")
        if label in labels or key in secrets:
            parser.error(f"{path}: duplicate AES key or label")
        labels.add(label)
        secrets.add(key)
        entries.append((label, key))
    lines = ['/* Generated secret key material. Never commit or publish this header. */',
             '#pragma once', '#include "suf2/suf2.h"',
             'static const suf2_decryption_key_t suf2_decryption_keys[] = {']
    for label, key in entries:
        lines.append("    {{" + ",".join(f"0x{v:02x}" for v in label) + "},")
        lines.append("     {" + ",".join(f"0x{v:02x}" for v in key) + "}},")
    if not entries:
        lines.append("    {{0}, {0}},")
    lines += ["};", f"#define SUF2_DECRYPTION_KEY_COUNT {len(entries)}u", ""]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    if not args.output.exists() or args.output.read_text() != text:
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as file:
            os.fchmod(file.fileno(), 0o600)
            file.write(text)
    else:
        args.output.chmod(0o600)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
