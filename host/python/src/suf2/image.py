import struct
from pathlib import Path

MAX_SIZE = 16 * 1024 * 1024


def intel_hex(data: bytes) -> list[tuple[int, bytes]]:
    segments = []
    offset = 0
    ended = False
    for line in data.decode("ascii").splitlines():
        if not line.strip():
            continue
        if ended:
            raise ValueError("Intel HEX data follows the end-of-file record")
        if not line.startswith(":") or len(line) % 2 != 1:
            raise ValueError("Invalid Intel HEX record")
        if any(c not in "0123456789abcdefABCDEF" for c in line[1:]):
            raise ValueError("Invalid Intel HEX hexadecimal data")
        raw = bytes.fromhex(line[1:])
        if len(raw) < 5 or len(raw) != raw[0] + 5 or sum(raw) % 256:
            raise ValueError("Invalid Intel HEX length or checksum")
        count, address, kind = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
        payload = raw[4:-1]
        if kind == 0:
            if address + count > 0x10000:
                raise ValueError("Intel HEX record crosses its address window")
            if count:
                segments.append((offset + address, payload))
        elif kind == 1:
            if count != 0 or address != 0:
                raise ValueError("Invalid Intel HEX end-of-file record")
            ended = True
        elif kind in (2, 4):
            if count != 2 or address != 0:
                raise ValueError("Invalid Intel HEX extended-address record")
            offset = int.from_bytes(payload, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5):
            if count != 4 or address != 0:
                raise ValueError("Invalid Intel HEX entry-point record")
        else:
            raise ValueError("Unsupported Intel HEX record type")
    if not ended:
        raise ValueError("Intel HEX end-of-file record is missing")
    return segments


def elf(data: bytes) -> list[tuple[int, bytes]]:
    if len(data) < 16 or data[:4] != b"\x7fELF":
        raise ValueError("Invalid ELF header")
    elf_class = data[4]
    if data[5:7] != b"\x01\x01" or elf_class not in (1, 2):
        raise ValueError("Only little-endian ELF32/ELF64 version 1 is supported")
    header_size = 52 if elf_class == 1 else 64
    if len(data) < header_size:
        raise ValueError("Truncated ELF header")
    if struct.unpack_from("<H", data, 16)[0] != 2:
        raise ValueError("Only executable ELF files (ET_EXEC) are supported")
    if struct.unpack_from("<I", data, 20)[0] != 1:
        raise ValueError("Unsupported ELF version")
    if elf_class == 1:
        phoff = struct.unpack_from("<I", data, 28)[0]
        ehsize, phsize, count = struct.unpack_from("<HHH", data, 40)
        expected = 32
    else:
        phoff = struct.unpack_from("<Q", data, 32)[0]
        ehsize, phsize, count = struct.unpack_from("<HHH", data, 52)
        expected = 56
    if ehsize != header_size or phsize != expected or count in (0, 0xFFFF):
        raise ValueError("Unsupported ELF program-header layout")
    if phoff < header_size or phoff + phsize * count > len(data):
        raise ValueError("Truncated ELF program-header table")
    segments = []
    for index in range(count):
        pos = phoff + index * phsize
        if elf_class == 1:
            kind, start, _, address, size, memory, _, _ = struct.unpack_from("<8I", data, pos)
        else:
            kind, _, start, _, address, size, memory, _ = struct.unpack_from("<II6Q", data, pos)
        if kind != 1:
            continue
        if size > memory or start + size > len(data):
            raise ValueError("Invalid ELF load segment")
        if size:
            segments.append((address, data[start:start + size]))
    return segments


def assemble(segments: list[tuple[int, bytes]], base: int,
             end: int | None = None, fill: int | None = None) -> bytes:
    if not 0 <= base <= 0xFFFFFFFF:
        raise ValueError("Execution base must be an unsigned 32-bit address")
    if (end is None) != (fill is None):
        raise ValueError("Sparse conversion requires both --end and --fill")
    if fill is not None and not 0 <= fill <= 255:
        raise ValueError("Fill must be a byte (0..255)")
    segments = sorted(segments)
    if not segments:
        raise ValueError("Firmware image must be nonempty")
    if segments[0][0] != base:
        raise ValueError("Execution base must match the lowest loaded address")
    cursor = base
    has_gap = False
    for address, payload in segments:
        if address < cursor:
            raise ValueError("Overlapping firmware segments are not supported")
        if address + len(payload) > 0x100000000:
            raise ValueError("Firmware address range exceeds 32 bits")
        has_gap |= address != cursor
        cursor = address + len(payload)
    limit = cursor if end is None else end
    if limit < cursor or limit > 0x100000000:
        raise ValueError("Image range must include every loaded byte without overflow")
    length = limit - base
    if not 0 < length <= MAX_SIZE:
        raise ValueError("Image must be nonempty and at most 16 MiB")
    if has_gap and fill is None:
        raise ValueError("Firmware contains gaps; explicitly choose --end and --fill")
    result = bytearray([0 if fill is None else fill]) * length
    for address, payload in segments:
        start = address - base
        result[start:start + len(payload)] = payload
    return bytes(result)


def load(path: Path, base: int, end: int | None = None,
         fill: int | None = None) -> bytes:
    with path.open("rb") as source:
        data = source.read(MAX_SIZE + 1)
    if len(data) > MAX_SIZE:
        raise ValueError("Input file exceeds the 16 MiB limit")
    extension = path.suffix.lower()
    if extension == ".bin":
        segments = [(base, data)] if data else []
    elif extension == ".hex":
        segments = intel_hex(data)
    elif extension == ".elf":
        segments = elf(data)
    else:
        raise ValueError("Input must be a .bin, .hex or .elf file")
    return assemble(segments, base, end, fill)
