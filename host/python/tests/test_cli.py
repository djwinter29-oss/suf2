import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


class CliTests(unittest.TestCase):
    def test_pack_and_refuse_overwrite_or_invalid_input(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image = root / "firmware.bin"
            image.write_bytes(b"firmware")
            key = root / "key.pem"
            key.write_bytes(ec.generate_private_key(ec.SECP256R1()).private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()))
            output = root / "firmware.suf2"
            command = [sys.executable, "-m", "suf2", "pack", "--input", str(image),
                       "--output", str(output), "--key", str(key), "--product-id", "00" * 16,
                       "--base", "0x10010000"]
            env = dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parents[1] / "src"))
            first = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            original = output.read_bytes()
            second = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            output.unlink()
            image.write_bytes(b"")
            invalid = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertNotEqual(invalid.returncode, 0)
            self.assertIn("nonempty", invalid.stderr)
            self.assertFalse(output.exists())
            self.assertNotIn("PRIVATE KEY", invalid.stderr)


if __name__ == "__main__":
    unittest.main()
