"""Verify all host producers independently against the signed-only wire profile."""

import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = Path(__file__).resolve().parents[2]
ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
BASE = 0x10010000
PRODUCT = bytes.fromhex("00112233445566778899aabbccddeeff")


def hex_record(address, kind, payload=b""):
    record = bytes([len(payload)]) + address.to_bytes(2, "big") + bytes([kind]) + payload
    return ":" + (record + bytes([-sum(record) % 256])).hex()


def executable(payload, wide):
    size, phsize = (64, 56) if wide else (52, 32)
    header = bytearray(size)
    header[:7] = b"\x7fELF" + bytes([2 if wide else 1, 1, 1])
    struct.pack_into("<HHI", header, 16, 2, 40, 1)
    if wide:
        struct.pack_into("<Q", header, 32, size)
        struct.pack_into("<HHH", header, 52, size, phsize, 1)
        ph = struct.pack("<II6Q", 1, 5, size + phsize, BASE, BASE, len(payload), len(payload) + 64, 4)
    else:
        struct.pack_into("<I", header, 28, size)
        struct.pack_into("<HHH", header, 40, size, phsize, 1)
        ph = struct.pack("<8I", 1, size + phsize, BASE, BASE, len(payload), len(payload) + 64, 5, 4)
    return bytes(header) + ph + payload


class InteroperabilityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.key_path = self.folder / "test-private.pem"
        self.key_path.write_bytes(self.key.private_bytes(serialization.Encoding.PEM,
                                 serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.key_path.chmod(0o600)
        self.env = dict(os.environ, PYTHONPATH=str(ROOT / "python/src"))

    def run_packer(self, producer, source, target, extra=()):
        args = ["pack", "--input", str(source), "--output", str(target),
                "--key", str(self.key_path), "--product-id", PRODUCT.hex(), "--base", hex(BASE),
                *extra]
        if producer == "python":
            command = [sys.executable, "-m", "suf2", *args]
        elif producer == "dotnet":
            command = ["dotnet", "run", "--project", str(ROOT / "dotnet"), "-c", "Release",
                       "--no-build", "--", *args]
        else:
            command = ["node", str(ROOT / "shared/tests/dashboard-pack.mjs"), *args]
        return subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=60)

    def verify(self, package, image):
        count = (len(image) + 255) // 256 + 1
        self.assertEqual(len(package), count * 512)
        manifest = package[48:304]
        self.assertEqual(manifest[:8], b"SUF2MFST")
        self.assertEqual(struct.unpack_from("<II", manifest, 8), (1, 0))
        self.assertEqual(manifest[16:32], PRODUCT)
        self.assertEqual(struct.unpack_from("<II", manifest, 32), (BASE, len(image)))
        self.assertEqual(manifest[56:96], bytes(40))
        self.assertEqual(manifest[160:192], bytes(32))
        digest = hashlib.sha256(image).digest()
        self.assertEqual(manifest[96:128], digest)
        self.assertEqual(manifest[128:160], digest)
        point = self.key.public_key().public_bytes(serialization.Encoding.X962,
                                                  serialization.PublicFormat.UncompressedPoint)
        self.assertEqual(manifest[40:56], hashlib.sha256(point).digest()[:16])
        r, s = int.from_bytes(manifest[192:224], "big"), int.from_bytes(manifest[224:], "big")
        self.assertTrue(1 <= r < ORDER and 1 <= s <= (ORDER - 1) // 2)
        self.key.public_key().verify(utils.encode_dss_signature(r, s),
                                    b"SUF2-MANIFEST-V1\0" + manifest[:192],
                                    ec.ECDSA(hashes.SHA256()))
        assembled = bytearray()
        for index in range(count):
            block = package[index * 512:(index + 1) * 512]
            self.assertEqual(struct.unpack_from("<8I", block),
                             (0x0A324655, 0x9E5D5157, 0x2001, 0, 272, index, count, 0x1E0424ED))
            self.assertEqual(block[32:48], hashlib.sha256(manifest).digest()[:16])
            self.assertEqual(block[304:508], bytes(204))
            self.assertEqual(struct.unpack_from("<I", block, 508)[0], 0x0AB16F30)
            if index:
                assembled.extend(block[48:304])
        self.assertEqual(assembled[:len(image)], image)
        self.assertEqual(assembled[len(image):], bytes(len(assembled) - len(image)))

    def test_bin_hex_elf32_elf64_verify_across_all_producers(self):
        image = bytes(range(256)) + b"short-final-chunk"
        lines = [hex_record(0, 4, b"\x10\x01")]
        lines += [hex_record(offset, 0, image[offset:offset + 16])
                  for offset in range(0, len(image), 16)]
        lines.append(hex_record(0, 1))
        fixtures = {"image.bin": image, "image.hex": "\n".join(lines).encode(),
                    "elf32.elf": executable(image, False), "elf64.elf": executable(image, True)}
        for name, data in fixtures.items():
            source = self.folder / name
            source.write_bytes(data)
            for producer in ("python", "dotnet", "dashboard"):
                with self.subTest(input=name, producer=producer):
                    output = self.folder / (name + "." + producer + ".suf2")
                    result = self.run_packer(producer, source, output)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.verify(output.read_bytes(), image)

    def test_gaps_rejected_then_explicit_fill_verified(self):
        source = self.folder / "sparse.hex"
        source.write_text("\n".join([hex_record(0, 4, b"\x10\x01"),
                                     hex_record(0, 0, b"ab"), hex_record(4, 0, b"c"),
                                     hex_record(0, 1)]))
        for producer in ("python", "dotnet", "dashboard"):
            with self.subTest(producer=producer):
                output = self.folder / (producer + ".suf2")
                rejected = self.run_packer(producer, source, output)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertFalse(output.exists())
                accepted = self.run_packer(producer, source, output,
                                           ["--end", hex(BASE + 6), "--fill", "0xff"])
                self.assertEqual(accepted.returncode, 0, accepted.stderr)
                self.verify(output.read_bytes(), b"ab\xff\xffc\xff")

    def test_wrong_curve_rejected_by_all_producers(self):
        source = self.folder / "image.bin"
        source.write_bytes(b"firmware")
        self.key_path.write_bytes(ec.generate_private_key(ec.SECP384R1()).private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        for producer in ("python", "dotnet", "dashboard"):
            with self.subTest(producer=producer):
                output = self.folder / (producer + ".suf2")
                result = self.run_packer(producer, source, output)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
