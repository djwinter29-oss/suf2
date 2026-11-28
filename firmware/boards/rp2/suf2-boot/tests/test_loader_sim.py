"""Loader storage tests: install, tamper and power loss on simulated flash.

Runs sim_loader, which links the real RP2 flash_store.c and msc_disk.c.
Packages come from the host Python packer, the same tool users run.
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from suf2.packer import pack

SIM = os.environ.get("SUF2_SIM_LOADER", "")
PRODUCT = b"SUF2SIM".ljust(16, b"\0")
APP_BASE = 0x10011000
APP_OFFSET, META_OFFSET, SLOT = 0x11000, 0x10000, 0xF7000


def pem(key):
    return key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


def point(key):
    return key.public_key().public_bytes(serialization.Encoding.X962,
                                         serialization.PublicFormat.UncompressedPoint).hex()


@unittest.skipUnless(SIM, "set SUF2_SIM_LOADER to the sim_loader binary")
class LoaderSim(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = ec.generate_private_key(ec.SECP256R1())
        cls.tmp = tempfile.mkdtemp()
        cls.old_image = bytes((i * 7) & 0xFF for i in range(20_000))
        cls.new_image = os.urandom(70_001)
        cls.old = cls.write("old.suf2", pack(cls.old_image, APP_BASE, PRODUCT, pem(cls.key)))
        cls.new = cls.write("new.suf2", pack(cls.new_image, APP_BASE, PRODUCT, pem(cls.key)))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    @classmethod
    def write(cls, name, data):
        path = os.path.join(cls.tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def setUp(self):
        self.flash = os.path.join(self.tmp, f"{self.id()}.flash")
        if os.path.exists(self.flash):
            os.remove(self.flash)

    def sim(self, *args, power_cut=None):
        cmd = [SIM, "--flash", self.flash, "--product", PRODUCT.hex(), "--key", point(self.key)]
        if power_cut is not None:
            cmd += ["--power-cut", str(power_cut)]
        out = subprocess.run(cmd + list(args), check=True, capture_output=True, text=True).stdout
        return json.loads(out)

    def flash_bytes(self, offset, length):
        with open(self.flash, "rb") as f:
            f.seek(offset)
            return f.read(length)

    def install(self, path):
        result = self.sim("copy", path)
        self.assertEqual(result.get("install"), "OK", result)

    def test_install_then_boot(self):
        self.install(self.new)
        self.assertEqual(self.sim("boot"), {"meta": "COMMITTED", "boot": "OK"})
        self.assertEqual(self.flash_bytes(APP_OFFSET, len(self.new_image)), self.new_image)

    def test_install_preserves_journal_and_commit_layout(self):
        self.install(self.new)
        with open(self.new, "rb") as f:
            manifest = f.read(512)[48:304]
        journal = self.flash_bytes(META_OFFSET, 268)
        magic, version, retained, check = struct.unpack("<II256sI", journal)
        self.assertEqual((magic, version, check), (0x4C4A3253, 1, 0xB3B5CDAC))
        self.assertEqual(retained, manifest)
        commit = self.flash_bytes(META_OFFSET + 4096 - 256, 24)
        magic, package_id, check = struct.unpack("<I16sI", commit)
        self.assertEqual((magic, check), (0x4B4F3253, 0xB4B0CDAC))
        self.assertEqual(package_id, hashlib.sha256(manifest).digest()[:16])

    def test_tampered_app_does_not_boot(self):
        self.install(self.new)
        with open(self.flash, "r+b") as f:
            f.seek(APP_OFFSET + 1234)
            byte = f.read(1)
            f.seek(APP_OFFSET + 1234)
            f.write(bytes([byte[0] ^ 1]))
        self.assertEqual(self.sim("boot")["boot"], "PLAINTEXT_DIGEST")

    def test_rejected_package_leaves_installed_app_alone(self):
        self.install(self.old)
        other = ec.generate_private_key(ec.SECP256R1())
        wrong_key = self.write("wrong-key.suf2", pack(self.new_image, APP_BASE, PRODUCT, pem(other)))
        wrong_base = self.write("wrong-base.suf2", pack(self.new_image, APP_BASE + 0x1000, PRODUCT, pem(self.key)))
        # Bigger than staging: refused while receiving, before policy is checked.
        too_big = self.write("too-big.suf2", pack(bytes(SLOT + 256), APP_BASE, PRODUCT, pem(self.key)))
        for path, cause in ((wrong_key, "UNKNOWN_KEY"), (wrong_base, "POLICY_ADDRESS"), (too_big, "BLOCK_BOUNDS")):
            with self.subTest(cause):
                result = self.sim("copy", path)
                self.assertIn(cause, (result.get("verify"), result.get("error")), result)
                self.assertNotIn("install", result)
                self.assertEqual(self.sim("boot"), {"meta": "COMMITTED", "boot": "OK"})
        self.assertEqual(self.flash_bytes(APP_OFFSET, len(self.old_image)), self.old_image)

    def test_power_cut_at_every_install_step(self):
        sectors = -(-len(self.new_image) // 4096)
        total_ops = 2 + 2 * sectors + 1  # META erase, journal, erase+program per sector, commit
        for cut in range(1, total_ops + 1):
            with self.subTest(cut=cut):
                if os.path.exists(self.flash):
                    os.remove(self.flash)
                self.install(self.old)
                self.assertEqual(self.sim("copy", self.new, power_cut=cut)["install"], "POWER_CUT")
                boot = self.sim("boot")
                if cut <= 2:
                    # Lost while META was being rewritten: no journal, so the
                    # device stays in update mode until the file is copied again.
                    self.assertEqual(boot, {"meta": "EMPTY"})
                    self.install(self.new)
                    boot = self.sim("boot")
                    self.assertEqual(boot, {"meta": "COMMITTED", "boot": "OK"})
                elif cut == total_ops and boot["meta"] == "COMMITTED":
                    # The half-written commit page already held the whole record.
                    self.assertEqual(boot, {"meta": "COMMITTED", "boot": "OK"})
                else:
                    self.assertEqual(boot, {"meta": "PENDING", "resume": "OK", "meta_after": "COMMITTED",
                                            "boot": "OK"})
                self.assertEqual(self.flash_bytes(APP_OFFSET, len(self.new_image)), self.new_image)

    def test_power_cut_during_resume(self):
        self.install(self.old)
        self.sim("copy", self.new, power_cut=10)
        self.assertEqual(self.sim("boot", power_cut=5)["resume"], "POWER_CUT")
        self.assertEqual(self.sim("boot")["boot"], "OK")

    def test_drive_is_a_valid_fat16_volume(self):
        disk = os.path.join(self.tmp, "disk.img")
        self.sim("disk", disk)
        if shutil.which("fsck.fat"):
            subprocess.run(["fsck.fat", "-n", disk], check=True, capture_output=True)
        with open(disk, "rb") as f:
            boot = f.read(512)
            self.assertEqual(boot[54:62], b"FAT16   ")
            bps, spc, reserved, fats, root_entries = struct.unpack_from("<HBHBH", boot, 11)
            fat_sectors = struct.unpack_from("<H", boot, 22)[0]
            root = (reserved + fats * fat_sectors) * bps
            f.seek(root)
            entries = [f.read(32) for _ in range(3)]
            names = [e[:11] for e in entries]
            self.assertEqual(names, [b"SUF2BOOT   ", b"SUF2INFOTXT", b"STATUS  TXT"])
            data = root + root_entries * 32
            size = struct.unpack_from("<I", entries[1], 28)[0]
            f.seek(data)
            self.assertEqual(f.read(size), b"SUF2 loader simulation\r\n")


if __name__ == "__main__":
    unittest.main()
