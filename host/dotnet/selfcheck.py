#!/usr/bin/env python3
"""Focused CLI/wire checks; requires a Release build, Python 3 and OpenSSL."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parent
DLL = ROOT / "bin/Release/net10.0/suf2.dll"
ORDER = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)


def command(*args):
    return subprocess.run(args, capture_output=True, check=True)


def hex_record(address, kind, data=b""):
    record = bytes([len(data)]) + address.to_bytes(2, "big") + bytes([kind]) + data
    return ":" + (record + bytes([-sum(record) & 255])).hex().upper() + "\n"


def elf(bits, segments):
    header_size, ph_size = (52, 32) if bits == 32 else (64, 56)
    header = bytearray(header_size)
    header[:7] = b"\x7fELF" + bytes([1 if bits == 32 else 2, 1, 1])
    struct.pack_into("<HHI", header, 16, 2, 3 if bits == 32 else 62, 1)
    struct.pack_into("<I" if bits == 32 else "<Q", header, 28 if bits == 32 else 32, header_size)
    struct.pack_into("<HHH", header, 40 if bits == 32 else 52, header_size, ph_size, len(segments))
    offset = header_size + ph_size * len(segments)
    program_headers, payload = bytearray(), bytearray()
    for address, data, bss in segments:
        if bits == 32:
            ph = struct.pack("<IIIIIIII", 1, offset, address + 4096, address, len(data),
                             len(data) + bss, 5, 1)
        else:
            ph = struct.pack("<IIQQQQQQ", 1, 5, offset, address + 4096, address, len(data),
                             len(data) + bss, 1)
        program_headers.extend(ph)
        payload.extend(data)
        offset += len(data)
    return header + program_headers + payload


def main():
    if not DLL.is_file():
        raise SystemExit("Build first: dotnet build host/dotnet/Suf2.csproj -c Release")
    work = ROOT / ".checks" / uuid.uuid4().hex
    work.mkdir(parents=True, mode=0o700)
    checks = 0
    try:
        key = work / "private.pem"
        command("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                "ec_paramgen_curve:P-256", "-out", str(key))
        key.chmod(0o600)
        public = work / "public.pem"
        command("openssl", "pkey", "-in", str(key), "-pubout", "-out", str(public))
        point = command("openssl", "pkey", "-in", str(key), "-pubout", "-outform", "DER").stdout[-65:]
        assert len(point) == 65 and point[0] == 4
        product = bytes.fromhex("00112233445566778899aabbccddeeff")

        def pack(name, data, expected=None, base="0x1000", extra=(), signing_key=key):
            nonlocal checks
            checks += 1
            source, output = work / name, work / f"{checks}.suf2"
            source.write_bytes(data)
            args = ["dotnet", str(DLL), "pack", "--input", str(source), "--output", str(output),
                    "--key", str(signing_key), "--product-id", product.hex(), "--base", base, *extra]
            result = subprocess.run(args, capture_output=True)
            if expected is None:
                assert result.returncode != 0 and result.stderr.startswith(b"Error:"), result
                assert b"BEGIN PRIVATE KEY" not in result.stderr and b" at " not in result.stderr
                assert not output.exists()
                return
            assert result.returncode == 0, result.stderr.decode()
            package = output.read_bytes()
            count = (len(expected) + 255) // 256 + 1
            assert len(package) == count * 512
            manifest = package[48:304]
            package_id = hashlib.sha256(manifest).digest()[:16]
            stored = bytearray()
            for index in range(count):
                block = package[index * 512:(index + 1) * 512]
                assert struct.unpack_from("<8I", block) == (
                    0x0A324655, 0x9E5D5157, 0x2001, 0, 272, index, count, 0x1E0424ED)
                assert block[32:48] == package_id and block[304:508] == bytes(204)
                assert struct.unpack_from("<I", block, 508)[0] == 0x0AB16F30
                if index:
                    stored.extend(block[48:304])
            assert stored[:len(expected)] == expected and not any(stored[len(expected):])
            assert manifest[:8] == b"SUF2MFST"
            assert struct.unpack_from("<II", manifest, 8) == (1, 0)
            assert manifest[16:32] == product
            assert struct.unpack_from("<II", manifest, 32) == (int(base, 0), len(expected))
            assert manifest[40:56] == hashlib.sha256(point).digest()[:16]
            assert manifest[56:96] == bytes(40) and manifest[160:192] == bytes(32)
            assert manifest[96:128] == manifest[128:160] == hashlib.sha256(expected).digest()
            r, s = (int.from_bytes(manifest[start:start + 32], "big") for start in (192, 224))
            assert 1 <= r < ORDER and 1 <= s <= ORDER // 2

            def der_integer(value):
                encoded = value.to_bytes((value.bit_length() + 7) // 8, "big")
                if encoded[0] & 128:
                    encoded = b"\0" + encoded
                return b"\x02" + bytes([len(encoded)]) + encoded

            integers = der_integer(r) + der_integer(s)
            signature, signed = work / "signature.der", work / "signed.bin"
            signature.write_bytes(b"\x30" + bytes([len(integers)]) + integers)
            signed.write_bytes(b"SUF2-MANIFEST-V1\0" + manifest[:192])
            command("openssl", "dgst", "-sha256", "-verify", str(public),
                    "-signature", str(signature), str(signed))
            # Existing outputs must remain byte-for-byte unchanged.
            again = subprocess.run(args, capture_output=True)
            assert again.returncode != 0 and output.read_bytes() == package

        image = bytes(range(256)) + b"\x42"
        pack("image.bin", image, image)
        pack("empty.bin", b"")
        pack("overflow.bin", b"ab", base="0xffffffff")
        pack("last.bin", b"x", b"x", base="0xffffffff", extra=("--end", "0x100000000"))
        pack("discard.bin", image, extra=("--end", "0x1100", "--fill", "0xff"))
        pack("tail.bin", b"ab", b"ab\xff\xff", extra=("--end", "0x1004", "--fill", "0xff"))
        pack("gap.bin", b"ab", extra=("--end", "0x1004"))
        pack("fill-only.bin", b"ab", extra=("--fill", "0xff"))
        pack("wrong-base.hex", (hex_record(0x1001, 0, b"ab") + hex_record(0, 1)).encode())
        pack("large.bin", bytes(16 * 1024 * 1024 + 1))
        eof = hex_record(0, 1)
        contiguous = (hex_record(0, 2, b"\x01\x00") + hex_record(0, 0, b"ab") +
                      hex_record(0, 3, bytes(4)) + hex_record(0, 5, bytes(4)) + eof).encode()
        pack("contiguous.hex", contiguous, b"ab")
        sparse = (hex_record(0, 4, b"\0\0") + hex_record(0x1000, 0, b"ab") +
                  hex_record(0x1004, 0, b"cd") + eof).encode()
        pack("sparse.hex", sparse)
        pack("filled.hex", sparse, b"ab\xff\xffcd\xff\xff",
             extra=("--end", "0x1008", "--fill", "255"))
        pack("missing-eof.hex", hex_record(0x1000, 0, b"ab").encode())
        pack("after-eof.hex", contiguous + hex_record(0x1002, 0, b"c").encode())
        pack("checksum.hex", contiguous.replace(b"6162", b"6163"))
        pack("overlap.hex", (hex_record(0x1000, 0, b"ab") +
                            hex_record(0x1001, 0, b"b") + eof).encode())
        pack("control-length.hex", (hex_record(0, 4, b"\0") + eof).encode())
        pack("bad-eof.hex", (hex_record(0x1000, 0, b"ab") + hex_record(0, 1, b"\0")).encode())
        pack("bad-start-segment.hex", (hex_record(0, 3, bytes(3)) + eof).encode())
        pack("bad-start-linear.hex", (hex_record(0, 5, bytes(5)) + eof).encode())
        pack("control-address.hex", (hex_record(1, 2, b"\0\0") + eof).encode())
        pack("unsupported.hex", (hex_record(0, 6) + eof).encode())
        pack("overflow.hex", (hex_record(0, 4, b"\xff\xff") +
                             hex_record(0xffff, 0, b"ab") + eof).encode(), base="0xffffffff")
        pack("window.hex", (hex_record(0, 4, b"\0\0") +
                           hex_record(0xffff, 0, b"ab") + eof).encode(), base="0xffff")
        for bits in (32, 64):
            pack(f"image{bits}.elf", elf(bits, [(0x1000, b"abcd", 128)]), b"abcd")
            pack(f"sparse{bits}.elf", elf(bits, [(0x1000, b"ab", 2), (0x1004, b"cd", 0)]))
            pack(f"filled{bits}.elf", elf(bits, [(0x1000, b"ab", 2), (0x1004, b"cd", 0)]),
                 b"ab\xff\xffcd", extra=("--end", "0x1006", "--fill", "0xff"))
            pack(f"overlap{bits}.elf", elf(bits, [(0x1000, b"ab", 0), (0x1001, b"b", 0)]))
            pack(f"truncated{bits}.elf", elf(bits, [(0x1000, b"abcd", 0)])[:-1])
            invalid = elf(bits, [(0x1000, b"abcd", 0)])
            struct.pack_into("<I" if bits == 32 else "<Q", invalid, (52 + 20) if bits == 32 else (64 + 40), 3)
            pack(f"memsz{bits}.elf", invalid)
            invalid = elf(bits, [(0x1000, b"abcd", 0)])
            invalid[5] = 2
            pack(f"big-endian{bits}.elf", invalid)
            invalid = elf(bits, [(0x1000, b"abcd", 0)])
            struct.pack_into("<H", invalid, 16, 3)
            pack(f"shared{bits}.elf", invalid)
        other = work / "other.pem"
        command("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                "ec_paramgen_curve:P-384", "-out", str(other))
        pack("curve.bin", b"ab", signing_key=other)
        pack("public.bin", b"ab", signing_key=public)
        print(f"PASS: {checks} CLI cases, exact wire checks, OpenSSL signature verification, overwrite checks")
    finally:
        shutil.rmtree(work)
        try:
            work.parent.rmdir()
        except OSError:
            pass


if __name__ == "__main__":
    main()
