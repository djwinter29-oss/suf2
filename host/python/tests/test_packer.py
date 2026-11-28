import hashlib
import struct
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from suf2.image import assemble, elf, intel_hex
from suf2.packer import ORDER, pack


def record(address, kind, payload=b""):
    value = bytes([len(payload)]) + address.to_bytes(2, "big") + bytes([kind]) + payload
    return ":" + (value + bytes([-sum(value) % 256])).hex()


def elf_fixture(payload=b"firmware", address=0x10010000, wide=False):
    size, phsize = (64, 56) if wide else (52, 32)
    header = bytearray(size)
    header[:7] = b"\x7fELF" + bytes([2 if wide else 1, 1, 1])
    struct.pack_into("<HHI", header, 16, 2, 40, 1)
    if wide:
        struct.pack_into("<Q", header, 32, size)
        struct.pack_into("<HHH", header, 52, size, phsize, 1)
        ph = struct.pack("<II6Q", 1, 5, size + phsize, address, address, len(payload), len(payload) + 8, 4)
    else:
        struct.pack_into("<I", header, 28, size)
        struct.pack_into("<HHH", header, 40, size, phsize, 1)
        ph = struct.pack("<8I", 1, size + phsize, address, address, len(payload), len(payload) + 8, 5, 4)
    return bytes(header) + ph + payload


class ImageTests(unittest.TestCase):
    def test_hex_extended_address_and_checksum(self):
        text = "\n".join([record(0, 4, b"\x10\x01"), record(0, 0, b"abc"), record(0, 1)])
        self.assertEqual(assemble(intel_hex(text.encode()), 0x10010000), b"abc")
        with self.assertRaisesRegex(ValueError, "checksum"):
            intel_hex((text[:-2] + "01").encode())

    def test_hex_requires_eof_and_rejects_trailing_records(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            intel_hex(record(0, 0, b"x").encode())
        with self.assertRaisesRegex(ValueError, "follows"):
            intel_hex((record(0, 1) + "\n" + record(0, 0, b"x")).encode())

    def test_hex_rejects_cross_window_and_unknown_record(self):
        for text in (record(0xFFFF, 0, b"ab"), record(0, 6)):
            with self.assertRaises(ValueError):
                intel_hex((text + "\n" + record(0, 1)).encode())

    def test_elf_32_and_64_excludes_bss(self):
        for wide in (False, True):
            self.assertEqual(assemble(elf(elf_fixture(wide=wide)), 0x10010000), b"firmware")

    def test_elf_rejects_truncation_and_nonexecutable(self):
        with self.assertRaisesRegex(ValueError, "segment"):
            elf(elf_fixture()[:-1])
        value = bytearray(elf_fixture())
        struct.pack_into("<H", value, 16, 3)
        with self.assertRaisesRegex(ValueError, "executable"):
            elf(value)

    def test_sparse_requires_explicit_range_and_fill(self):
        segments = [(0x1000, b"ab"), (0x1004, b"c")]
        with self.assertRaisesRegex(ValueError, "gaps"):
            assemble(segments, 0x1000)
        self.assertEqual(assemble(segments, 0x1000, 0x1006, 255), b"ab\xff\xffc\xff")
        with self.assertRaisesRegex(ValueError, "both"):
            assemble(segments, 0x1000, 0x1006)

    def test_overlap_out_of_range_and_wrong_base(self):
        for segments, base, end in (
            ([(5, b"ab"), (6, b"c")], 5, None),
            ([(0xFFFFFFFF, b"ab")], 0xFFFFFFFF, None),
            ([(5, b"a")], 4, None),
            ([(5, b"abc")], 5, 7),
        ):
            with self.assertRaises(ValueError):
                assemble(segments, base, end, 0 if end else None)


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = ec.generate_private_key(ec.SECP256R1())
        cls.pem = cls.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                       serialization.NoEncryption())

    def test_manifest_block_layout_and_signature(self):
        image = bytes(range(256)) + b"short"
        package = pack(image, 0x10010000, bytes(range(16)), self.pem)
        self.assertEqual(len(package), 1536)
        manifest = package[48:304]
        self.assertEqual(manifest[:8], b"SUF2MFST")
        self.assertEqual(struct.unpack_from("<II", manifest, 8), (1, 0))
        self.assertEqual(struct.unpack_from("<II", manifest, 32), (0x10010000, 261))
        self.assertEqual(manifest[96:128], hashlib.sha256(image).digest())
        self.assertEqual(manifest[128:160], manifest[96:128])
        point = self.key.public_key().public_bytes(serialization.Encoding.X962,
                                                   serialization.PublicFormat.UncompressedPoint)
        self.assertEqual(manifest[40:56], hashlib.sha256(point).digest()[:16])
        self.assertEqual(manifest[56:96], bytes(40))
        self.assertEqual(manifest[160:192], bytes(32))
        r = int.from_bytes(manifest[192:224], "big")
        s = int.from_bytes(manifest[224:256], "big")
        self.assertTrue(1 <= r < ORDER and 1 <= s <= (ORDER - 1) // 2)
        self.key.public_key().verify(utils.encode_dss_signature(r, s),
                                     b"SUF2-MANIFEST-V1\0" + manifest[:192],
                                     ec.ECDSA(hashes.SHA256()))
        for number in range(3):
            block = package[number * 512:(number + 1) * 512]
            self.assertEqual(struct.unpack_from("<8I", block),
                             (0x0A324655, 0x9E5D5157, 0x2001, 0, 272, number, 3, 0x1E0424ED))
            self.assertEqual(block[32:48], hashlib.sha256(manifest).digest()[:16])
            self.assertEqual(block[304:508], bytes(204))
            self.assertEqual(struct.unpack_from("<I", block, 508)[0], 0x0AB16F30)
        self.assertEqual(package[1072:1077], b"short")
        self.assertEqual(package[1077:1328], bytes(251))
        mutated = bytearray(manifest[:192])
        mutated[16] ^= 1
        with self.assertRaises(InvalidSignature):
            self.key.public_key().verify(utils.encode_dss_signature(r, s),
                                         b"SUF2-MANIFEST-V1\0" + mutated,
                                         ec.ECDSA(hashes.SHA256()))

    def test_rejects_wrong_curve_and_invalid_parameters(self):
        other = ec.generate_private_key(ec.SECP384R1()).private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
        for image, base, product, pem in (
            (b"x", 0, bytes(16), other),
            (b"", 0, bytes(16), self.pem),
            (b"xx", 0xFFFFFFFF, bytes(16), self.pem),
            (b"x", 0, bytes(15), self.pem),
            (b"x", 0, bytes(16), b"bad"),
        ):
            with self.assertRaises(ValueError):
                pack(image, base, product, pem)


if __name__ == "__main__":
    unittest.main()
