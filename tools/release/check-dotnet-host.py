#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Smoke-test a self-contained .NET SUF2 CLI folder before it is released.

Runs the native executable without a .NET install on PATH, checks its
version, and packs a small image with a fresh key to make sure the
signature path works on this platform.

  check-dotnet-host.py FOLDER --version VERSION
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

UF2_MAGIC = (0x0A324655, 0x9E5D5157)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--version", required=True)
    arguments = parser.parse_args()
    folder = arguments.folder.resolve()

    libraries = json.loads((folder / "suf2.deps.json").read_text())["libraries"]
    if f"suf2/{arguments.version}" not in libraries:
        raise SystemExit(f"suf2.deps.json does not record version {arguments.version}")

    executable = folder / ("suf2.exe" if os.name == "nt" else "suf2")
    environment = {key: value for key, value in os.environ.items() if not key.startswith("DOTNET_")}
    environment["DOTNET_ROOT"] = str(folder / "no-shared-runtime")

    def run(*args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([str(executable), *args], env=environment, capture_output=True,
                              text=True, timeout=60, check=False)

    help_text = run("--help")
    if help_text.returncode != 0 or "pack" not in help_text.stdout:
        raise SystemExit(f"--help failed: {help_text.stderr or help_text.stdout}")

    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        key = ec.generate_private_key(ec.SECP256R1())
        (work / "key.pem").write_bytes(key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        (work / "app.bin").write_bytes(bytes(range(256)) * 4)
        packed = run("pack", "--input", str(work / "app.bin"), "--output", str(work / "app.suf2"),
                     "--key", str(work / "key.pem"), "--product-id", "00112233445566778899aabbccddeeff",
                     "--base", "0x10011000")
        if packed.returncode != 0:
            raise SystemExit(f"pack failed: {packed.stderr or packed.stdout}")
        data = (work / "app.suf2").read_bytes()
        if not data or len(data) % 512 or struct.unpack_from("<2I", data) != UF2_MAGIC:
            raise SystemExit("pack produced a malformed package")

    print(f"{executable.name} {arguments.version}: help and pack OK ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
