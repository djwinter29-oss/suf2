#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Stage and verify the public dashboard site.

Only the listed static files are published. Tests, package metadata, host
CLIs and keys never leave the repository.

  dashboard_site.py build  --output DIR --version VERSION
  dashboard_site.py verify --output DIR --version VERSION
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "host/dashboard"
PUBLIC = ("index.html", "styles.css", "app.js", "input.js", "packer.js", ".nojekyll")
GENERATED = ("version.txt",)
# A real PEM key has base64 body lines; the dashboard only mentions the header.
PEM_KEY = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----\s*\n[A-Za-z0-9+/]{40,}")
REFERENCE = re.compile(r'(?:src|href)="\./([^"]+)"|from "\./([^"]+)"')


def build(output: Path, version: str) -> None:
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    for name in PUBLIC:
        shutil.copyfile(SOURCE / name, output / name)
    (output / "version.txt").write_text(f"{version}\n", encoding="utf-8")
    print(f"staged {len(PUBLIC) + len(GENERATED)} files in {output}")


def verify(output: Path, version: str) -> None:
    files = sorted(str(path.relative_to(output)) for path in output.rglob("*") if path.is_file())
    expected = sorted(PUBLIC + GENERATED)
    if files != expected:
        raise SystemExit(f"unexpected site contents: {files}, expected {expected}")
    for name in PUBLIC:
        if (output / name).read_bytes() != (SOURCE / name).read_bytes():
            raise SystemExit(f"{name} differs from host/dashboard/{name}")
    for name in files:
        text = (output / name).read_text(encoding="utf-8")
        if PEM_KEY.search(text):
            raise SystemExit(f"{name} contains a PEM private key")
        for match in REFERENCE.finditer(text):
            target = match.group(1) or match.group(2)
            if not (output / target).is_file():
                raise SystemExit(f"{name} refers to missing ./{target}")
    if (output / "version.txt").read_text(encoding="utf-8").strip() != version:
        raise SystemExit("version.txt does not match the release version")
    print(f"verified {len(files)} files, version {version}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("build", "verify"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    arguments = parser.parse_args()
    (build if arguments.command == "build" else verify)(arguments.output, arguments.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
