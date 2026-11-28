import hashlib
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.exceptions import UnsupportedAlgorithm

from .image import MAX_SIZE

ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def pack(image: bytes, base: int, product_id: bytes, pem: bytes) -> bytes:
    if not image or len(image) > MAX_SIZE:
        raise ValueError("Image must be nonempty and at most 16 MiB")
    if not 0 <= base <= 0xFFFFFFFF or base + len(image) > 0x100000000:
        raise ValueError("Invalid execution range")
    if len(product_id) != 16:
        raise ValueError("Product ID must contain exactly 16 bytes")
    if not pem.startswith(b"-----BEGIN PRIVATE KEY-----"):
        raise ValueError("Use an unencrypted PKCS#8 PEM private key")
    try:
        key = serialization.load_pem_private_key(pem, password=None)
    except (ValueError, TypeError, UnsupportedAlgorithm) as error:
        raise ValueError("Cannot parse the PKCS#8 signing key") from error
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Signing key must use ECDSA P-256")
    point = key.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    manifest = bytearray(256)
    manifest[:8] = b"SUF2MFST"
    struct.pack_into("<II", manifest, 8, 1, 0)
    manifest[16:32] = product_id
    struct.pack_into("<II", manifest, 32, base, len(image))
    manifest[40:56] = hashlib.sha256(point).digest()[:16]
    digest = hashlib.sha256(image).digest()
    manifest[96:128] = manifest[128:160] = digest
    signature = key.sign(b"SUF2-MANIFEST-V1\0" + manifest[:192], ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(signature)
    s = min(s, ORDER - s)
    manifest[192:224] = r.to_bytes(32, "big")
    manifest[224:256] = s.to_bytes(32, "big")
    package_id = hashlib.sha256(manifest).digest()[:16]
    count = (len(image) + 255) // 256 + 1
    result = bytearray()
    for number in range(count):
        content = bytes(manifest) if number == 0 else image[(number - 1) * 256:number * 256]
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, 0x2001,
                         0, 272, number, count, 0x1E0424ED)
        block[32:48] = package_id
        block[48:48 + len(content)] = content
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        result.extend(block)
    return bytes(result)
