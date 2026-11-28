"""Behaviour tests for the SUF2 firmware core, driven through suf2_harness.

Packages are built here with full control over every field so that both
valid and deliberately broken packages can be produced. One test also uses the
host Python packer to make sure the firmware accepts its output.
"""

import hashlib
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

HARNESS = sys.argv.pop(1) if len(sys.argv) > 1 else os.environ.get("SUF2_HARNESS", "")
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
PRODUCT = bytes(range(16))
BASE = 0x10020000
FAMILY = 0x1E0424ED


def public_point(key):
    return key.public_key().public_bytes(serialization.Encoding.X962,
                                         serialization.PublicFormat.UncompressedPoint)


def key_id(key):
    return hashlib.sha256(public_point(key)).digest()[:16]


def sign(key, manifest, high_s=False):
    der = key.sign(b"SUF2-MANIFEST-V1\0" + bytes(manifest[:192]), ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    s = min(s, N - s)
    if high_s:
        s = N - s
    manifest[192:256] = r.to_bytes(32, "big") + s.to_bytes(32, "big")


def make_manifest(key, image, **kw):
    m = bytearray(256)
    m[0:8] = b"SUF2MFST"
    struct.pack_into("<III", m, 8, kw.get("version", 1), kw.get("flags", 0), 0)
    m[16:32] = kw.get("product", PRODUCT)
    struct.pack_into("<II", m, 32, kw.get("base", BASE), kw.get("length", len(image)))
    m[40:56] = kw.get("key_id", key_id(key))
    digest = hashlib.sha256(image).digest()
    m[96:128] = kw.get("plaintext_digest", digest)
    m[128:160] = kw.get("stored_digest", digest)
    for offset, value in kw.get("patch", {}).items():
        m[offset:offset + len(value)] = value
    if kw.get("sign", True):
        sign(key, m, kw.get("high_s", False))
    return m


def blocks(manifest, image, package_id=None, num_blocks=None):
    pid = package_id or hashlib.sha256(bytes(manifest)).digest()[:16]
    chunks = [image[i:i + 256].ljust(256, b"\0") for i in range(0, len(image), 256)]
    total = num_blocks or len(chunks) + 1
    out = []
    for no, content in enumerate([bytes(manifest)] + chunks):
        out.append(block(no, total, pid, content))
    return out


def block(no, total, pid, content, flags=0x2001, target=0, payload=272, family=FAMILY, tail=b""):
    header = struct.pack("<8I", 0x0A324655, 0x9E5D5157, flags, target, payload, no, total, family)
    data = (pid + content + tail).ljust(476, b"\0")
    return header + data + struct.pack("<I", 0x0AB16F30)


class Harness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = ec.generate_private_key(ec.SECP256R1())
        cls.other = ec.generate_private_key(ec.SECP256R1())
        cls.image = bytes((i * 31 + 7) & 0xFF for i in range(1000))
        cls.tmp = tempfile.TemporaryDirectory()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_stream(self, sectors, keys=None, extra=(), aes_keys=()):
        path = os.path.join(self.tmp.name, "stream.bin")
        with open(path, "wb") as f:
            f.write(b"".join(sectors))
        keys = keys if keys is not None else [self.key]
        args = [HARNESS, "--stream", path, "--product", PRODUCT.hex(), "--base", hex(BASE)]
        for k in keys:
            args += ["--key", public_point(k).hex()]
        for i, (label, key) in enumerate(aes_keys):
            key_path = os.path.join(self.tmp.name, f"aes-{i}.bin")
            fd = os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "wb") as f:
                f.write(label + key)
            args += ["--aes-key-file", key_path]
        out = subprocess.run(args + list(extra), check=True, capture_output=True, text=True).stdout
        return json.loads(out)

    def package(self, image=None, **kw):
        image = self.image if image is None else image
        return blocks(make_manifest(self.key, image, **kw), image)

    def assert_ok(self, result):
        self.assertEqual((result["state"], result["verify"], result["boot"]), ("COMPLETE", "OK", "OK"), result)

    def assert_failed(self, result, cause):
        self.assertEqual((result["state"], result["error"]), ("FAILED", cause), result)

    def assert_rejected(self, result, cause):
        self.assertEqual((result["state"], result["error"], result["verify"]), ("FAILED", cause, cause), result)

    # ---- Acceptance ---------------------------------------------------

    def test_in_order(self):
        r = self.run_stream(self.package())
        self.assert_ok(r)
        self.assertEqual(r["results"], "SSSSS")

    def test_any_order_and_image_before_manifest(self):
        p = self.package()
        self.assert_ok(self.run_stream([p[3], p[1], p[4], p[2], p[0]]))

    def test_sizes_at_chunk_boundaries(self):
        for size in (1, 255, 256, 257, 512):
            with self.subTest(size=size):
                self.assert_ok(self.run_stream(self.package(self.image[:size])))

    def test_identical_duplicates_and_foreign_sectors(self):
        p = self.package()
        other_family = block(0, 2, bytes(16), bytes(256), family=0xE48BFF56)
        no_family_flag = block(0, 2, bytes(16), bytes(256), flags=0x1)
        stream = [b"\0" * 512, p[0], other_family, p[1], p[1], p[0], no_family_flag, p[2], p[3], p[4]]
        r = self.run_stream(stream)
        self.assert_ok(r)
        self.assertEqual(r["results"], "ISISDDISSS")

    def test_ignored_sectors_never_start_a_session(self):
        r = self.run_stream([b"\xff" * 512, block(0, 2, bytes(16), bytes(256), family=0x12345678)])
        self.assertEqual((r["state"], r["results"]), ("IDLE", "II"))

    def test_python_host_packer_output(self):
        from suf2.packer import pack
        pem = self.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.NoEncryption())
        data = pack(self.image, BASE, PRODUCT, pem)
        self.assert_ok(self.run_stream([data[i:i + 512] for i in range(0, len(data), 512)]))

    def test_second_provisioned_key(self):
        self.assert_ok(self.run_stream(self.package(), keys=[self.other, self.key]))

    # ---- Session rules (section 8.2) ---------------------------------

    def test_complete_session_ignores_repeat_and_drops_others(self):
        p = self.package()
        foreign = self.package(self.image[:300])[0]
        r = self.run_stream(p + [p[2], foreign])
        self.assert_ok(r)
        self.assertEqual(r["results"], "SSSSSDR")
        self.assertEqual(r["reports"], ["SESSION_COMPLETE"])

    def test_conflicting_duplicate_fails_then_drops(self):
        p = self.package()
        changed = bytearray(p[1]); changed[100] ^= 1
        r = self.run_stream([p[0], p[1], bytes(changed), p[2]])
        self.assert_failed(r, "CONFLICTING_DUPLICATE")
        self.assertEqual(r["reports"], ["CONFLICTING_DUPLICATE", "SESSION_FAILED"])

    def test_conflicting_manifest_duplicate(self):
        p = self.package()
        other = self.package(patch={16: b"\x55"})
        changed = bytearray(other[0]); changed[32:48] = p[0][32:48]
        self.assert_failed(self.run_stream([p[0], bytes(changed)]), "CONFLICTING_DUPLICATE")

    def test_mixed_packages(self):
        p = self.package()
        q = self.package(self.image[::-1])
        self.assert_failed(self.run_stream([p[0], p[1], q[2]]), "PACKAGE_MISMATCH")

    def test_different_num_blocks(self):
        p = self.package()
        m = bytearray(p[2]); struct.pack_into("<I", m, 24, 6)
        self.assert_failed(self.run_stream([p[1], bytes(m)]), "PACKAGE_MISMATCH")

    def test_malformed_first_block_fails_without_staging(self):
        p = self.package()
        bad = bytearray(p[1]); struct.pack_into("<I", bad, 12, 0x10000000)
        r = self.run_stream([bytes(bad)] + p)
        self.assert_failed(r, "BLOCK_FORMAT")
        self.assertEqual(r["results"], "R" * 6)

    def test_block_header_rules(self):
        pid = bytes(16)
        cases = {
            "flags": block(1, 3, pid, bytes(256), flags=0x2000),
            "extra flag": block(1, 3, pid, bytes(256), flags=0x2001 | 0x4000),
            "targetAddr": block(1, 3, pid, bytes(256), target=0x10000000),
            "payloadSize": block(1, 3, pid, bytes(256), payload=476),
            "tail": block(1, 3, pid, bytes(256), tail=b"\x01"),
        }
        for name, sector in cases.items():
            with self.subTest(name):
                self.assert_failed(self.run_stream([sector]), "BLOCK_FORMAT")

    def test_block_bounds(self):
        pid = bytes(16)
        for name, sector in {"one block": block(0, 1, pid, bytes(256)),
                             "blockNo too large": block(3, 3, pid, bytes(256)),
                             "beyond capacity": block(1, 300, pid, bytes(256))}.items():
            with self.subTest(name):
                self.assert_failed(self.run_stream([sector]), "BLOCK_BOUNDS")

    def test_bitmap_limit(self):
        r = self.run_stream(self.package(bytes(2048)), extra=["--bitmap-bytes", "1"])
        self.assert_failed(r, "BLOCK_BOUNDS")

    # ---- Block 0 checks ----------------------------------------------

    def test_wrong_package_identifier(self):
        m = make_manifest(self.key, self.image)
        self.assert_failed(self.run_stream(blocks(m, self.image, package_id=b"\x11" * 16)), "PACKAGE_ID")

    def test_block_count_disagrees_with_length(self):
        m = make_manifest(self.key, self.image)
        p = blocks(m, self.image, num_blocks=6)
        pid = p[0][32:48]
        self.assert_failed(self.run_stream(p + [block(5, 6, pid, bytes(256))]), "BLOCK_COUNT")

    def test_nonzero_padding(self):
        m = make_manifest(self.key, self.image)
        p = blocks(m, self.image)
        last = bytearray(p[4]); last[48 + 255] = 1
        self.assert_failed(self.run_stream([p[0], bytes(last)]), "PADDING")
        self.assert_failed(self.run_stream([bytes(last), p[0]]), "PADDING")

    def test_manifest_structure(self):
        cases = {
            "magic": ({"patch": {0: b"SUF2MFSX"}}, "MANIFEST_FORMAT"),
            "version": ({"version": 2}, "VERSION"),
            "flags": ({"flags": 2}, "MANIFEST_FORMAT"),
            "reserved 84": ({"patch": {90: b"\x01"}}, "MANIFEST_FORMAT"),
            "reserved 176": ({"patch": {191: b"\x01"}}, "MANIFEST_FORMAT"),
            "digests differ": ({"stored_digest": bytes(32)}, "MANIFEST_FORMAT"),
            "signed-only nonce": ({"patch": {72: b"\x01"}}, "MANIFEST_FORMAT"),
            "signed-only tag": ({"patch": {160: b"\x01"}}, "MANIFEST_FORMAT"),
            "signed-only enc key": ({"patch": {56: b"\x01"}}, "MANIFEST_FORMAT"),
            "encrypted without key id": ({"flags": 1}, "MANIFEST_FORMAT"),
        }
        for name, (kw, cause) in cases.items():
            with self.subTest(name):
                self.assert_failed(self.run_stream(self.package(**kw)), cause)

    # ---- Candidate verification (section 9.1) ------------------------

    def test_policy(self):
        self.assert_rejected(self.run_stream(self.package(product=b"\xaa" * 16)), "POLICY_PRODUCT")
        self.assert_rejected(self.run_stream(self.package(base=BASE + 0x100)), "POLICY_ADDRESS")
        self.assert_rejected(self.run_stream(self.package(), extra=["--max-size", "999"]), "POLICY_SIZE")
        self.assert_rejected(self.run_stream(self.package(), extra=["--require-encryption"]),
                             "POLICY_ENCRYPTION_REQUIRED")

    def test_policy_is_checked_before_signature(self):
        m = make_manifest(self.key, self.image, product=b"\xaa" * 16)
        m[200] ^= 1
        self.assert_rejected(self.run_stream(blocks(m, self.image)), "POLICY_PRODUCT")

    def test_unknown_key(self):
        self.assert_rejected(self.run_stream(self.package(), keys=[self.other]), "UNKNOWN_KEY")

    def test_signature_failures(self):
        m = make_manifest(self.key, self.image)
        m[96] ^= 1  # both digests changed after signing, so they still match
        m[128] ^= 1
        cases = {"altered": m, "high S": make_manifest(self.key, self.image, high_s=True)}
        zero_r = make_manifest(self.key, self.image); zero_r[192:224] = bytes(32)
        big_r = make_manifest(self.key, self.image); big_r[192:224] = N.to_bytes(32, "big")
        other_signer = make_manifest(self.other, self.image, key_id=key_id(self.key))
        cases.update({"r = 0": zero_r, "r = n": big_r, "wrong signer": other_signer})
        for name, manifest in cases.items():
            with self.subTest(name):
                self.assert_rejected(self.run_stream(blocks(manifest, self.image)), "SIGNATURE")

    def test_stored_digest(self):
        m = make_manifest(self.key, self.image)
        tampered = bytearray(self.image); tampered[700] ^= 1
        self.assert_rejected(self.run_stream(blocks(m, bytes(tampered))), "STORED_DIGEST")

    def test_encrypted_package_without_decryption_key(self):
        ciphertext = os.urandom(len(self.image))
        p = self.package(ciphertext, flags=1, plaintext_digest=hashlib.sha256(self.image).digest(),
                         patch={56: b"\x01" * 16, 72: b"\x02" * 12, 160: b"\x03" * 16})
        self.assert_rejected(self.run_stream(p), "NO_DECRYPTION_KEY")
        bad = make_manifest(self.key, ciphertext, flags=1, patch={56: b"\x01" * 16},
                            stored_digest=bytes(32))
        self.assert_rejected(self.run_stream(blocks(bad, ciphertext)), "STORED_DIGEST")

    def encrypted_manifest(self, image, key, label, plaintext_digest=None):
        m = make_manifest(self.key, image, flags=1, sign=False,
                          plaintext_digest=plaintext_digest or hashlib.sha256(image).digest(),
                          patch={56: label, 72: os.urandom(12)})
        encrypted = AESGCM(key).encrypt(bytes(m[72:84]), image, b"SUF2-AEAD-V1\0" + bytes(m[:128]))
        stored = encrypted[:-16]
        m[128:160] = hashlib.sha256(stored).digest()
        m[160:176] = encrypted[-16:]
        sign(self.key, m)
        return m, stored

    def test_encrypted_install_at_block_and_sector_boundaries(self):
        key, label = AESGCM.generate_key(bit_length=256), os.urandom(16)
        output = os.path.join(self.tmp.name, "installed.bin")
        for size in (1, 15, 16, 17, 255, 256, 257, 4095, 4096, 4097, 8193):
            with self.subTest(size=size):
                image = os.urandom(size)
                m, stored = self.encrypted_manifest(image, key, label)
                r = self.run_stream(blocks(m, stored), aes_keys=[(label, key)],
                                    extra=["--require-encryption", "--installed-out", output])
                self.assert_ok(r)
                self.assertEqual((r["install"], r["meta"]), ("OK", "COMMITTED"))
                self.assertTrue(r["staging_unchanged"])
                self.assertEqual(Path(output).read_bytes(), image)

    def test_encrypted_failures_never_modify_metadata_or_application(self):
        key, label = AESGCM.generate_key(bit_length=256), os.urandom(16)
        m, stored = self.encrypted_manifest(self.image, key, label)
        cases = [
            (m, stored, [], "NO_DECRYPTION_KEY"),
            (m, stored, [(os.urandom(16), key)], "NO_DECRYPTION_KEY"),
            (m, stored, [(label, AESGCM.generate_key(bit_length=256))], "GCM_TAG"),
        ]
        bad_tag = bytearray(m)
        bad_tag[160] ^= 1
        sign(self.key, bad_tag)
        cases.append((bad_tag, stored, [(label, key)], "GCM_TAG"))
        bad_aad = bytearray(m)
        bad_aad[96] ^= 1
        sign(self.key, bad_aad)
        cases.append((bad_aad, stored, [(label, key)], "GCM_TAG"))
        wrong_digest, ciphertext = self.encrypted_manifest(self.image, key, label, bytes(32))
        cases.append((wrong_digest, ciphertext, [(label, key)], "PLAINTEXT_DIGEST"))
        tampered = bytes([stored[0] ^ 1]) + stored[1:]
        cases.append((bad_tag, tampered, [(label, key)], "STORED_DIGEST"))
        bad_signature = bytearray(bad_tag)
        bad_signature[200] ^= 1
        cases.append((bad_signature, tampered, [], "SIGNATURE"))
        for manifest, ciphertext, keys, cause in cases:
            with self.subTest(cause=cause):
                r = self.run_stream(blocks(manifest, ciphertext), aes_keys=keys)
                self.assert_rejected(r, cause)
                self.assertEqual((r["install"], r["mutations"], r["meta"]), (cause, 0, "EMPTY"))
                self.assertTrue(r["staging_unchanged"])

    def test_encrypted_install_recovers_from_interrupted_flash_operations(self):
        key, label = AESGCM.generate_key(bit_length=256), os.urandom(16)
        image = os.urandom(5001)
        m, stored = self.encrypted_manifest(image, key, label)
        output = os.path.join(self.tmp.name, "recovered.bin")
        for successful_operations in (2, 3, 4, 5, 6):
            with self.subTest(successful_operations=successful_operations):
                r = self.run_stream(blocks(m, stored), aes_keys=[(label, key)],
                    extra=["--fail-install-after", str(successful_operations), "--installed-out", output])
                self.assertEqual((r["verify"], r["install"], r["meta"], r["retry"], r["boot"]),
                                 ("OK", "STORAGE", "PENDING", "OK", "OK"), r)
                self.assertTrue(r["staging_unchanged"])
                self.assertEqual(Path(output).read_bytes(), image)

    # ---- Boot verification (section 9.2) -----------------------------

    def test_boot_detects_modified_image(self):
        r = self.run_stream(self.package(), extra=["--tamper-installed", "999"])
        self.assertEqual((r["verify"], r["boot"]), ("OK", "PLAINTEXT_DIGEST"))

    def test_encrypted_boot_checks_installed_plaintext_without_aes_keys(self):
        key, label = AESGCM.generate_key(bit_length=256), os.urandom(16)
        m, stored = self.encrypted_manifest(self.image, key, label)
        r = self.run_stream(blocks(m, stored), aes_keys=[(label, key)], extra=["--tamper-installed", "999"])
        self.assertEqual((r["verify"], r["install"], r["boot"]), ("OK", "OK", "PLAINTEXT_DIGEST"))


class KeyProvisioning(unittest.TestCase):
    tools = Path(__file__).resolve().parents[3] / "tools" / "firmware"
    generator = Path(__file__).resolve().parents[3] / "test-keys" / "generate-test-keys.py"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)

    def generate(self, *args):
        return subprocess.run([sys.executable, str(self.generator), "--output", str(self.folder / "local"),
                               *args], capture_output=True, text=True)

    def header(self, keys, *args):
        return subprocess.run([sys.executable, str(self.tools / "suf2_aes_keys_header.py"),
            "--output", str(self.folder / "keys.h"), *args, *(str(k) for k in keys)],
            capture_output=True, text=True)

    def test_generated_keys_are_private_and_refuse_overwrite(self):
        self.assertEqual(self.generate().returncode, 0)
        local = self.folder / "local"
        self.assertEqual(local.stat().st_mode & 0o777, 0o700)
        originals = {p.name: p.read_bytes() for p in local.iterdir()}
        self.assertEqual(len(originals), 4)
        for p in local.iterdir():
            self.assertEqual(p.stat().st_mode & 0o777, 0o600)
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertEqual(originals, {p.name: p.read_bytes() for p in local.iterdir()})
        key = local / "encryption-key.jwk"
        self.assertEqual(self.header([key]).returncode, 0)
        self.assertEqual((self.folder / "keys.h").stat().st_mode & 0o777, 0o600)
        key.unlink()
        self.assertEqual(self.generate("--aes-only").returncode, 0)
        for name in ("signing-private.pem", "signing-public.pem", "signing-test-cert.pem"):
            self.assertEqual((local / name).read_bytes(), originals[name])

    def test_header_rejects_invalid_duplicate_and_production_test_keys(self):
        valid = {"kty": "oct", "alg": "A256GCM", "kid": os.urandom(16).hex(),
                 "k": base64.urlsafe_b64encode(os.urandom(32)).rstrip(b"=").decode("ascii"),
                 "key_ops": ["decrypt"], "test_only": True}
        key = self.folder / "key.jwk"
        key.write_text(json.dumps(valid))
        self.assertNotEqual(self.header([key], "--production").returncode, 0)
        external = dict(valid, test_only=False)
        key.write_text(json.dumps(external))
        self.assertEqual(self.header([key], "--production").returncode, 0)
        self.assertNotEqual(self.header([key], "--production", "--forbid-dir", str(self.folder)).returncode, 0)
        self.assertNotEqual(self.header([key, key]).returncode, 0)
        other = self.folder / "other.jwk"
        other.write_text(json.dumps(dict(external, kid=os.urandom(16).hex())))
        self.assertNotEqual(self.header([key, other]).returncode, 0)
        invalid = [
            dict(valid, kid="0" * 32), dict(valid, k="short"),
            dict(valid, k=valid["k"] + "="), dict(valid, alg="A128GCM"),
            dict(valid, key_ops=["encrypt"]), dict(valid, k=valid["k"][:-1] + "/"),
        ]
        for jwk in invalid:
            key.write_text(json.dumps(jwk))
            self.assertNotEqual(self.header([key]).returncode, 0)
        key.write_text('{"kty":"oct","kty":"oct"}')
        self.assertNotEqual(self.header([key]).returncode, 0)

if __name__ == "__main__":
    if not HARNESS:
        sys.exit("usage: test_suf2_core.py PATH_TO_suf2_harness")
    unittest.main()
