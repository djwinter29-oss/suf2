#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Check a firmware/build.sh output folder against the board's flash layout.

Checks that:
- the loader binary fits the loader region;
- every loader UF2 block uses the board's family ID and lands inside the
  loader region (RP2350 builds may carry picotool's single erratum-E10
  absolute block);
- blink.suf2, if present and a native harness is given, passes the same
  receive, verify and boot checks the loader runs, using the board's product
  ID, application address and slot size.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

XIP_BASE = 0x10000000
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
FLAG_FAMILY = 0x00002000
FAMILY = {"rp2040": 0xE48BFF56, "rp2350": 0xE48BFF59}
ABSOLUTE_FAMILY = 0xE48BFF57


def config(build: Path) -> dict[str, object]:
    text = (build / "generated/suf2_board_config.h").read_text()
    values: dict[str, object] = {}
    for name in ("SUF2_LOADER_SIZE", "SUF2_APP_OFFSET", "SUF2_SLOT_SIZE"):
        match = re.search(rf"#define {name} (0x[0-9a-fA-F]+)u", text)
        if not match:
            raise SystemExit(f"{name} missing from suf2_board_config.h")
        values[name] = int(match.group(1), 16)
    match = re.search(r"#define SUF2_PRODUCT_ID_BYTES \{([^}]*)\}", text)
    if not match:
        raise SystemExit("SUF2_PRODUCT_ID_BYTES missing from suf2_board_config.h")
    values["product"] = bytes(int(v, 16) for v in match.group(1).replace(" ", "").split(",") if v)
    return values


def check_loader(build: Path, board: str, loader_size: int) -> None:
    binary = build / "suf2_loader.bin"
    if binary.stat().st_size > loader_size:
        raise SystemExit(f"{binary}: {binary.stat().st_size} bytes exceeds the {loader_size:#x} loader region")

    data = (build / "suf2_loader.uf2").read_bytes()
    if not data or len(data) % 512:
        raise SystemExit("suf2_loader.uf2: not a whole number of 512-byte blocks")
    absolute = 0
    for offset in range(0, len(data), 512):
        m0, m1, flags, address, size, _, _, family = struct.unpack_from("<8I", data, offset)
        if (m0, m1, struct.unpack_from("<I", data, offset + 508)[0]) != UF2_MAGIC or not flags & FLAG_FAMILY:
            raise SystemExit(f"suf2_loader.uf2: bad block at offset {offset}")
        if family == ABSOLUTE_FAMILY and board == "rp2350":
            absolute += 1
            continue
        if family != FAMILY[board]:
            raise SystemExit(f"suf2_loader.uf2: family {family:#x}, expected {FAMILY[board]:#x}")
        if not (XIP_BASE <= address and address + size <= XIP_BASE + loader_size):
            raise SystemExit(f"suf2_loader.uf2: block at {address:#x} is outside the loader region")
    if absolute > 1:
        raise SystemExit("suf2_loader.uf2: more than one absolute-family block")
    print(f"loader: {binary.stat().st_size} of {loader_size} bytes, {len(data) // 512} UF2 blocks")


def check_app(build: Path, values: dict[str, object], harness: Path, public_key: Path) -> None:
    from cryptography.hazmat.primitives import serialization

    package = build / "blink/blink.suf2"
    key = serialization.load_pem_public_key(public_key.read_bytes())
    point = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    slot = values["SUF2_SLOT_SIZE"]
    result = subprocess.run(
        [str(harness), "--stream", str(package), "--product", values["product"].hex(),
         "--base", hex(XIP_BASE + values["SUF2_APP_OFFSET"]), "--key", point.hex(),
         "--max-size", str(slot), "--capacity", str(slot)],
        capture_output=True, text=True, check=False,
    )
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise SystemExit(f"harness failed: {result.stderr or result.stdout}") from None
    if (report.get("state"), report.get("verify"), report.get("boot")) != ("COMPLETE", "OK", "OK"):
        raise SystemExit(f"blink.suf2 rejected: {report}")
    print(f"blink.suf2: accepted at {XIP_BASE + values['SUF2_APP_OFFSET']:#x}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--board", choices=sorted(FAMILY), required=True)
    parser.add_argument("--harness", type=Path, help="suf2_harness from the native library build")
    parser.add_argument("--public-key", type=Path, help="PEM public key blink.suf2 was signed with")
    arguments = parser.parse_args()

    values = config(arguments.build_dir)
    check_loader(arguments.build_dir, arguments.board, values["SUF2_LOADER_SIZE"])
    if (arguments.build_dir / "blink/blink.suf2").is_file():
        if not (arguments.harness and arguments.public_key):
            print("blink.suf2: not checked (needs --harness and --public-key)", file=sys.stderr)
        else:
            check_app(arguments.build_dir, values, arguments.harness, arguments.public_key)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
