#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Check a tools/firmware/build.sh output folder against the board's flash layout.

Checks that:
- the loader binary fits the loader region;
- every loader UF2 block uses the board's family ID and lands inside the
  loader region (RP2350 builds may carry picotool's single erratum-E10
  absolute block);
- blink.suf2, if present and a native harness is given, passes the same
  receive, verify and boot checks the loader runs, using the board's product
  ID, application address and slot size.
- ESP32-S3 checks native image sizes, provisioning files, fixed partitions,
  shared key tables and preservation of the complete signed application BIN.
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


def config(build: Path, board: str) -> dict[str, object]:
    header = build / ("suf2-boot/generated/suf2_config.h" if board == "esp32s3" else "generated/suf2_board_config.h")
    text = header.read_text()
    values: dict[str, object] = {}
    names = ("SUF2_APP_OFFSET", "SUF2_SLOT_SIZE")
    names += (("SUF2_RECOVERY_OFFSET", "SUF2_RECOVERY_SIZE", "SUF2_META_OFFSET",
               "SUF2_STAGING_OFFSET", "SUF2_EXECUTION_BASE") if board == "esp32s3" else ("SUF2_LOADER_SIZE",))
    for name in names:
        match = re.search(rf"#define {name} (0x[0-9a-fA-F]+)u", text)
        if not match:
            raise SystemExit(f"{name} missing from {header}")
        values[name] = int(match.group(1), 16)
    match = re.search(r"#define SUF2_PRODUCT_ID_BYTES \{([^}]*)\}", text)
    if not match:
        raise SystemExit(f"SUF2_PRODUCT_ID_BYTES missing from {header}")
    values["product"] = bytes(int(v, 16) for v in match.group(1).replace(" ", "").split(",") if v)
    values["base"] = values["SUF2_EXECUTION_BASE"] if board == "esp32s3" else XIP_BASE + values["SUF2_APP_OFFSET"]
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


def check_esp32(build: Path, values: dict[str, object]) -> None:
    loader = build / "suf2-boot"
    flash = json.loads((loader / "flasher_args.json").read_text())
    expected = {"0x0": "bootloader/bootloader.bin", "0x20000": "partition_table/partition-table.bin",
                "0x30000": "suf2_boot.bin"}
    if flash["flash_files"] != expected or flash["extra_esptool_args"]["chip"] != "esp32s3":
        raise SystemExit("ESP32-S3 flashing must include only the bootloader, partition table and recovery app")
    for file, limit in (("bootloader/bootloader.bin", 0x20000),
                        ("suf2_boot.bin", values["SUF2_RECOVERY_SIZE"])):
        data = (loader / file).read_bytes()
        if len(data) < 24 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 9 or len(data) > limit:
            raise SystemExit(f"{file}: invalid ESP32-S3 image or partition overflow")
        print(f"{file}: {len(data)} of {limit} bytes")
    entries = {}
    table = (loader / "partition_table/partition-table.bin").read_bytes()
    for offset in range(0, len(table), 32):
        magic, kind, subtype, address, size, label, _ = struct.unpack_from("<HBBII16sI", table, offset)
        if magic != 0x50AA:
            break
        entries[label.rstrip(b"\0").decode()] = (kind, subtype, address, size)
    expected_parts = {
        "recovery": (0, 0, values["SUF2_RECOVERY_OFFSET"], values["SUF2_RECOVERY_SIZE"]),
        "suf2_meta": (1, 0x40, values["SUF2_META_OFFSET"], 4096),
        "candidate": (0, 0x20, values["SUF2_APP_OFFSET"], values["SUF2_SLOT_SIZE"]),
        "staging": (1, 0x41, values["SUF2_STAGING_OFFSET"], values["SUF2_SLOT_SIZE"]),
    }
    if any(entries.get(name) != entry for name, entry in expected_parts.items()):
        raise SystemExit("ESP32-S3 partition table does not match SUF2 configuration")
    if (loader / "generated/suf2_keys.h").read_bytes() != (build / "blink/generated/suf2_keys.h").read_bytes():
        raise SystemExit("ESP32-S3 projects have different trusted-key tables")
    print("ESP32-S3 flash layout and generated key tables: OK")


def check_app(build: Path, values: dict[str, object], harness: Path, public_key: Path, board: str) -> None:
    from cryptography.hazmat.primitives import serialization

    package = build / "blink/blink.suf2"
    key = serialization.load_pem_public_key(public_key.read_bytes())
    point = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    slot = values["SUF2_SLOT_SIZE"]
    result = subprocess.run(
        [str(harness), "--stream", str(package), "--product", values["product"].hex(),
         "--base", hex(values["base"]), "--key", point.hex(),
         "--max-size", str(slot), "--capacity", str(slot), "--bitmap-bytes", str((slot // 256 + 8) // 8)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise SystemExit(f"harness exited {result.returncode}: {result.stderr or result.stdout}")
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise SystemExit(f"harness failed: {result.stderr or result.stdout}") from None
    if (report.get("state"), report.get("verify"), report.get("boot")) != ("COMPLETE", "OK", "OK"):
        raise SystemExit(f"blink.suf2 rejected: "
                         f"{ {name: report.get(name) for name in ('state', 'error', 'verify', 'boot')} }")
    if board == "esp32s3":
        raw = package.read_bytes()
        image = (build / "blink/blink.bin").read_bytes()
        chunks = {struct.unpack_from("<I", raw, off + 20)[0]: raw[off + 48:off + 304]
                  for off in range(0, len(raw), 512)}
        length = struct.unpack_from("<I", chunks[0], 36)[0]
        stored = b"".join(chunks[i] for i in range(1, len(chunks)))[:length]
        if stored != image or length != len(image):
            raise SystemExit("blink.suf2 does not contain the complete native ESP-IDF blink.bin")
    print(f"blink.suf2: accepted at {values['base']:#x}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--board", choices=sorted([*FAMILY, "esp32s3"]), required=True)
    parser.add_argument("--harness", type=Path, help="suf2_harness from the native library build")
    parser.add_argument("--public-key", type=Path, help="PEM public key blink.suf2 was signed with")
    arguments = parser.parse_args()

    values = config(arguments.build_dir, arguments.board)
    if arguments.board == "esp32s3":
        check_esp32(arguments.build_dir, values)
    else:
        check_loader(arguments.build_dir, arguments.board, values["SUF2_LOADER_SIZE"])
    if (arguments.build_dir / "blink/blink.suf2").is_file():
        if not (arguments.harness and arguments.public_key):
            print("blink.suf2: not checked (needs --harness and --public-key)", file=sys.stderr)
        else:
            check_app(arguments.build_dir, values, arguments.harness, arguments.public_key, arguments.board)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
