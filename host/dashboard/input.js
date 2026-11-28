export const MAX_BYTES = 16 * 1024 * 1024;
const ADDRESS_LIMIT = 0x100000000;

function fail(message) {
  throw new Error(message);
}

function bytesOf(input) {
  const bytes = input instanceof ArrayBuffer ? new Uint8Array(input) : input;
  if (!(bytes instanceof Uint8Array)) fail("Input must be bytes.");
  if (!bytes.length || bytes.length > MAX_BYTES) fail("Input must be 1 byte to 16 MiB.");
  return bytes;
}

export function parseAddress(text, name = "Address", exclusive = false) {
  if (typeof text !== "string" || !/^(?:0x[0-9a-f]+|[0-9]+)$/i.test(text.trim())) {
    fail(`${name} must be a decimal integer or hexadecimal integer prefixed with 0x.`);
  }
  const value = Number(text.trim());
  if (!Number.isSafeInteger(value) || value < 0 || value > (exclusive ? ADDRESS_LIMIT : ADDRESS_LIMIT - 1)) {
    fail(`${name} is outside the 32-bit address range.`);
  }
  return value;
}

export function parseFill(text) {
  if (typeof text !== "string" || !/^(?:0x)?[0-9a-f]{2}$/i.test(text.trim())) {
    fail("Fill byte must be exactly two hexadecimal digits, optionally prefixed with 0x.");
  }
  return Number.parseInt(text.trim().replace(/^0x/i, ""), 16);
}

function checkBase(base) {
  if (!Number.isInteger(base) || base < 0 || base >= ADDRESS_LIMIT) fail("Base must be a 32-bit unsigned address.");
}

export function parseIntelHex(input) {
  const bytes = bytesOf(input);
  if (bytes.some(byte => byte > 127)) fail("Intel HEX must contain ASCII text.");
  const lines = new TextDecoder().decode(bytes).split(/\r?\n/);
  const segments = [];
  let upper = 0;
  let eof = false;
  let startSeen = false;
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    if (line === "") continue;
    const label = `Intel HEX line ${index + 1}`;
    if (eof) fail(`${label}: record after EOF.`);
    if (line.length < 11 || line.length > 521 || !/^:[0-9a-f]+$/i.test(line) || (line.length - 1) % 2) {
      fail(`${label}: malformed record.`);
    }
    const record = Uint8Array.from(line.slice(1).match(/../g), pair => Number.parseInt(pair, 16));
    const count = record[0];
    if (record.length !== count + 5) fail(`${label}: incorrect record length.`);
    if (record.reduce((sum, byte) => sum + byte, 0) % 256 !== 0) fail(`${label}: checksum mismatch.`);
    const address = record[1] * 256 + record[2];
    const type = record[3];
    const data = record.subarray(4, 4 + count);
    if (type === 0) {
      if (address + count > 0x10000) fail(`${label}: data crosses a 16-bit record address boundary.`);
      const absolute = upper + address;
      if (absolute + count > ADDRESS_LIMIT) fail(`${label}: address overflow.`);
      if (count) segments.push({ address: absolute, bytes: data });
    } else if (type === 1) {
      if (count !== 0 || address !== 0) fail(`${label}: EOF must have length and address zero.`);
      eof = true;
    } else if (type === 2 || type === 4) {
      if (count !== 2 || address !== 0) fail(`${label}: extended address must have length 2 and address zero.`);
      upper = (data[0] * 256 + data[1]) * (type === 2 ? 16 : 65536);
    } else if (type === 3 || type === 5) {
      if (count !== 4 || address !== 0) fail(`${label}: start address must have length 4 and address zero.`);
      if (startSeen) fail(`${label}: duplicate start address.`);
      startSeen = true;
      // Start records describe an entry point, not the execution base.
    } else {
      fail(`${label}: unsupported record type ${type}.`);
    }
  }
  if (!eof) fail("Intel HEX is missing its EOF record.");
  return segments;
}

export function parseElf(input) {
  const bytes = bytesOf(input);
  if (bytes.length < 16 || bytes[0] !== 0x7f || bytes[1] !== 0x45 || bytes[2] !== 0x4c || bytes[3] !== 0x46) {
    fail("ELF magic is invalid.");
  }
  const elf64 = bytes[4] === 2;
  if (bytes[4] !== 1 && !elf64) fail("Only ELF32 and ELF64 are supported.");
  if (bytes[5] !== 1) fail("Only little-endian ELF is supported.");
  if (bytes[6] !== 1) fail("Unsupported ELF identification version.");
  const size = elf64 ? 64 : 52;
  if (bytes.length < size) fail("ELF header is truncated.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u16 = offset => view.getUint16(offset, true);
  const u32 = offset => view.getUint32(offset, true);
  const wide = offset => elf64 ? view.getBigUint64(offset, true) : BigInt(u32(offset));
  if (u16(16) !== 2) fail("ELF must be ET_EXEC, not relocatable or a shared object.");
  if (u32(20) !== 1) fail("Unsupported ELF version.");
  if (u16(elf64 ? 52 : 40) !== size) fail("ELF header size is invalid.");
  const phOffset = wide(elf64 ? 32 : 28);
  const phSize = u16(elf64 ? 54 : 42);
  const phCount = u16(elf64 ? 56 : 44);
  if (!phCount || phCount === 0xffff) fail("ELF needs an ordinary, nonempty program header table.");
  if (phSize !== (elf64 ? 56 : 32)) fail("ELF program header size is invalid.");
  if (phOffset < BigInt(size) || phOffset + BigInt(phSize * phCount) > BigInt(bytes.length)) {
    fail("ELF program header table is out of bounds.");
  }
  const segments = [];
  for (let index = 0; index < phCount; index++) {
    const offset = Number(phOffset) + index * phSize;
    if (u32(offset) !== 1) continue;
    const fileOffset = wide(offset + (elf64 ? 8 : 4));
    const address = wide(offset + (elf64 ? 24 : 12));
    const fileSize = wide(offset + (elf64 ? 32 : 16));
    const memorySize = wide(offset + (elf64 ? 40 : 20));
    if (fileSize > memorySize) fail(`ELF PT_LOAD ${index}: filesz exceeds memsz.`);
    if (!fileSize) continue; // BSS is not firmware file content.
    if (fileOffset + fileSize > BigInt(bytes.length)) fail(`ELF PT_LOAD ${index}: file bytes are out of bounds.`);
    if (address >= BigInt(ADDRESS_LIMIT) || address + fileSize > BigInt(ADDRESS_LIMIT)) {
      fail(`ELF PT_LOAD ${index}: physical address is outside the 32-bit range.`);
    }
    segments.push({
      address: Number(address),
      bytes: bytes.subarray(Number(fileOffset), Number(fileOffset + fileSize))
    });
  }
  return segments;
}

export function normalizeSegments(segments, { base, end, fill } = {}) {
  checkBase(base);
  const explicit = end !== undefined;
  if (explicit !== (fill !== undefined)) fail("Exclusive end and fill byte must be supplied together.");
  if (explicit && (!Number.isInteger(end) || end <= base || end > ADDRESS_LIMIT)) {
    fail("Exclusive end must be above base and at most 0x100000000.");
  }
  if (explicit && (!Number.isInteger(fill) || fill < 0 || fill > 255)) fail("Fill must be one byte.");
  const sorted = [...segments].sort((a, b) => a.address - b.address);
  if (!sorted.length) fail("Firmware contains no file-backed image bytes.");
  let cursor = base;
  for (const segment of sorted) {
    if (!(segment.bytes instanceof Uint8Array) || !segment.bytes.length ||
        !Number.isInteger(segment.address) || segment.address < 0 ||
        segment.address + segment.bytes.length > ADDRESS_LIMIT) fail("Invalid image segment.");
  }
  if (sorted[0].address !== base) fail("Base must equal the lowest included firmware address.");
  for (const segment of sorted) {
    if (segment.address < cursor) fail("Firmware contains overlapping address ranges.");
    if (segment.address > cursor && !explicit) fail("Sparse firmware gaps require an explicit exclusive end and fill byte.");
    cursor = segment.address + segment.bytes.length;
    if (explicit && cursor > end) fail("Firmware bytes lie outside the explicit range; no bytes may be dropped.");
  }
  const length = (explicit ? end : cursor) - base;
  if (length <= 0 || length > MAX_BYTES) fail("Normalized image must be 1 byte to 16 MiB.");
  const image = new Uint8Array(length);
  if (explicit) image.fill(fill);
  for (const segment of sorted) image.set(segment.bytes, segment.address - base);
  return image;
}

export function parseFirmware(input, { format, base, end, fill } = {}) {
  const bytes = bytesOf(input);
  checkBase(base);
  let segments;
  if (format === "bin") segments = [{ address: base, bytes }];
  else if (format === "hex") segments = parseIntelHex(bytes);
  else if (format === "elf") segments = parseElf(bytes);
  else fail("Select BIN, Intel HEX, or ELF.");
  return normalizeSegments(segments, { base, end, fill });
}
