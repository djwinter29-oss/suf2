import test from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import { MAX_BYTES, normalizeSegments, parseAddress, parseFill, parseFirmware, parseIntelHex, parseElf } from "../input.js";
import { hex, lowSSignature, packSigned, parseProductId } from "../packer.js";

const encoder = new TextEncoder();
const productId = "00112233445566778899aabbccddeeff";
const order = BigInt("0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551");
const int = bytes => BigInt(`0x${hex(bytes)}`);
const zero = bytes => assert.ok(bytes.every(byte => byte === 0));

async function createKey(curve = "P-256") {
  const pair = await webcrypto.subtle.generateKey({ name: "ECDSA", namedCurve: curve }, true, ["sign", "verify"]);
  const der = new Uint8Array(await webcrypto.subtle.exportKey("pkcs8", pair.privateKey));
  const pem = `-----BEGIN PRIVATE KEY-----\n${Buffer.from(der).toString("base64")}\n-----END PRIVATE KEY-----`;
  der.fill(0);
  return { pem, publicKey: pair.publicKey };
}

function record(type, address = 0, data = []) {
  const bytes = [data.length, address >> 8, address & 255, type, ...data];
  bytes.push((-bytes.reduce((sum, byte) => sum + byte, 0)) & 255);
  return `:${hex(bytes)}`;
}

const eof = record(1);
const ihex = (...records) => encoder.encode(records.join("\n"));
const readHex = (input, options = {}) => parseFirmware(input, { format: "hex", base: 0x1000, ...options });

function elfFixture(elf64 = false, segments = [
  { address: 0x1000, data: [0x11, 0x22], memorySize: 6 },
  { address: 0x1002, data: [0x33] }
]) {
  const headerSize = elf64 ? 64 : 52;
  const phSize = elf64 ? 56 : 32;
  const tableEnd = headerSize + segments.length * phSize;
  const bytes = new Uint8Array(tableEnd + segments.reduce((sum, segment) => sum + (segment.data?.length ?? 0), 0));
  bytes.set([0x7f, 0x45, 0x4c, 0x46, elf64 ? 2 : 1, 1, 1]);
  const view = new DataView(bytes.buffer);
  const u16 = (offset, value) => view.setUint16(offset, value, true);
  const u32 = (offset, value) => view.setUint32(offset, value, true);
  const wide = (offset, value) => elf64 ? view.setBigUint64(offset, BigInt(value), true) : u32(offset, value);
  u16(16, 2);
  u16(18, 40);
  u32(20, 1);
  wide(elf64 ? 24 : 24, 0x1001);
  wide(elf64 ? 32 : 28, headerSize);
  u16(elf64 ? 52 : 40, headerSize);
  u16(elf64 ? 54 : 42, phSize);
  u16(elf64 ? 56 : 44, segments.length);
  let cursor = tableEnd;
  for (let index = 0; index < segments.length; index++) {
    const segment = segments[index];
    const data = segment.data ?? [];
    const offset = headerSize + index * phSize;
    u32(offset, segment.type ?? 1);
    wide(offset + (elf64 ? 8 : 4), cursor);
    wide(offset + (elf64 ? 16 : 8), 0x9000 + index * 16);
    wide(offset + (elf64 ? 24 : 12), segment.address);
    wide(offset + (elf64 ? 32 : 16), data.length);
    wide(offset + (elf64 ? 40 : 20), segment.memorySize ?? data.length);
    bytes.set(data, cursor);
    cursor += data.length;
  }
  return bytes;
}

test("real signed package verifies independently and has the exact SUF2 wire layout", async () => {
  const key = await createKey();
  const image = Uint8Array.from({ length: 257 }, (_, index) => (index * 7) & 255);
  const base = 0x08004000;
  const { bytes, manifest } = await packSigned({ image, base, productId, privateKeyPem: key.pem }, webcrypto);
  assert.equal(bytes.length, 1536);
  assert.equal(new TextDecoder().decode(manifest.subarray(0, 8)), "SUF2MFST");
  const m = new DataView(manifest.buffer);
  assert.equal(m.getUint32(8, true), 1);
  assert.equal(m.getUint32(12, true), 0);
  assert.equal(hex(manifest.subarray(16, 32)), productId);
  assert.equal(m.getUint32(32, true), base);
  assert.equal(m.getUint32(36, true), 257);
  zero(manifest.subarray(56, 96));
  zero(manifest.subarray(160, 192));
  const digest = new Uint8Array(await webcrypto.subtle.digest("SHA-256", image));
  assert.deepEqual(manifest.subarray(96, 128), digest);
  assert.deepEqual(manifest.subarray(128, 160), digest);
  const point = new Uint8Array(await webcrypto.subtle.exportKey("raw", key.publicKey));
  assert.equal(point.length, 65);
  const keyId = new Uint8Array(await webcrypto.subtle.digest("SHA-256", point)).slice(0, 16);
  assert.deepEqual(manifest.subarray(40, 56), keyId);
  const independentKey = await webcrypto.subtle.importKey("raw", point, { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
  const message = new Uint8Array(209);
  message.set(encoder.encode("SUF2-MANIFEST-V1\0"));
  message.set(manifest.subarray(0, 192), 17);
  const signature = manifest.subarray(192);
  assert.ok(int(signature.subarray(0, 32)) > 0n && int(signature.subarray(0, 32)) < order);
  assert.ok(int(signature.subarray(32)) > 0n && int(signature.subarray(32)) <= (order - 1n) / 2n);
  assert.equal(await webcrypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, independentKey, signature, message), true);
  message[33] ^= 1;
  assert.equal(await webcrypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, independentKey, signature, message), false);
  const packageId = new Uint8Array(await webcrypto.subtle.digest("SHA-256", manifest)).slice(0, 16);
  const v = new DataView(bytes.buffer);
  const reconstructed = new Uint8Array(512);
  for (let block = 0; block < 3; block++) {
    const offset = block * 512;
    assert.equal(v.getUint32(offset, true), 0x0a324655);
    assert.equal(v.getUint32(offset + 4, true), 0x9e5d5157);
    assert.equal(v.getUint32(offset + 8, true), 0x2001);
    assert.equal(v.getUint32(offset + 12, true), 0);
    assert.equal(v.getUint32(offset + 16, true), 272);
    assert.equal(v.getUint32(offset + 20, true), block);
    assert.equal(v.getUint32(offset + 24, true), 3);
    assert.equal(v.getUint32(offset + 28, true), 0x1e0424ed);
    assert.deepEqual(bytes.subarray(offset + 32, offset + 48), packageId);
    zero(bytes.subarray(offset + 304, offset + 508));
    assert.equal(v.getUint32(offset + 508, true), 0x0ab16f30);
    if (block === 0) assert.deepEqual(bytes.subarray(offset + 48, offset + 304), manifest);
    else reconstructed.set(bytes.subarray(offset + 48, offset + 304), (block - 1) * 256);
  }
  assert.deepEqual(reconstructed.subarray(0, 257), image);
  zero(reconstructed.subarray(257));
});

test("signature canonicalization converts high-S and rejects invalid integers or DER", () => {
  const signature = new Uint8Array(64);
  signature[31] = 1;
  signature.set(Uint8Array.from(Buffer.from((order - 1n).toString(16), "hex")), 32);
  const low = lowSSignature(signature);
  assert.equal(int(low.subarray(32)), 1n);
  assert.equal(int(signature.subarray(32)), order - 1n);
  assert.throws(() => lowSSignature(new Uint8Array(70)), /P1363/);
  assert.throws(() => lowSSignature(new Uint8Array(64)), /integers/);
  signature.set(Uint8Array.from(Buffer.from(order.toString(16), "hex")), 32);
  assert.throws(() => lowSSignature(signature), /integers/);
});

test("packer rejects invalid identity, image range, unsupported PEM, and other curves", async () => {
  const key = await createKey();
  const args = { image: new Uint8Array([1]), base: 0, productId, privateKeyPem: key.pem };
  for (const patch of [
    { productId: "00" }, { image: new Uint8Array() }, { image: new Uint8Array(MAX_BYTES + 1) },
    { base: -1 }, { base: 0xffffffff, image: new Uint8Array([1, 2]) },
    { privateKeyPem: "-----BEGIN EC PRIVATE KEY-----\nAAAA\n-----END EC PRIVATE KEY-----" },
    { privateKeyPem: "-----BEGIN ENCRYPTED PRIVATE KEY-----\nAAAA\n-----END ENCRYPTED PRIVATE KEY-----" },
    { privateKeyPem: "-----BEGIN PRIVATE KEY-----\n!!!!\n-----END PRIVATE KEY-----" },
    { privateKeyPem: `${key.pem}\n${key.pem}` }, { privateKeyPem: "x".repeat(65537) }
  ]) {
    await assert.rejects(packSigned({ ...args, ...patch }, webcrypto));
  }
  await assert.rejects(packSigned(args, {}), /WebCrypto/);
  const wrongCurve = await createKey("P-384");
  await assert.rejects(packSigned({ ...args, privateKeyPem: wrongCurve.pem }, webcrypto), /P-256/);
});

test("addresses, product IDs, and fill bytes require exact input forms", () => {
  assert.equal(parseAddress(" 0x08000000 "), 0x08000000);
  assert.equal(parseAddress("4096"), 4096);
  assert.equal(parseAddress("0x100000000", "End", true), 0x100000000);
  for (const input of ["", "-1", "1.2", "1e3", "FF", "0x100000000", "0x1p2", "0x", "Infinity"]) {
    assert.throws(() => parseAddress(input));
  }
  assert.equal(parseFill("FF"), 255);
  assert.equal(parseFill("0x00"), 0);
  for (const input of ["F", "100", "-1", "0x", "ZZ"]) assert.throws(() => parseFill(input));
  assert.equal(parseProductId(productId).length, 16);
  assert.throws(() => parseProductId(`${productId} `));
});

test("BIN respects execution bounds and explicit trailing fill", () => {
  const bytes = new Uint8Array([1, 2]);
  assert.deepEqual(parseFirmware(bytes, { format: "bin", base: 0xfffffffe }), bytes);
  assert.deepEqual(parseFirmware(bytes, { format: "bin", base: 8, end: 12, fill: 255 }), new Uint8Array([1, 2, 255, 255]));
  assert.throws(() => parseFirmware(bytes, { format: "bin", base: 0xffffffff }), /segment/);
  assert.throws(() => parseFirmware(bytes, { format: "bin", base: 8, end: 9, fill: 0 }), /outside/);
  assert.throws(() => parseFirmware(new Uint8Array(), { format: "bin", base: 0 }), /1 byte/);
  assert.throws(() => parseFirmware(new Uint8Array(MAX_BYTES + 1), { format: "bin", base: 0 }), /16 MiB/);
  assert.throws(() => parseFirmware(bytes, { format: "raw", base: 0 }), /Select BIN/);
  assert.equal(parseFirmware(new Uint8Array(MAX_BYTES), { format: "bin", base: 0 }).length, MAX_BYTES);
});

test("Intel HEX accepts all defined record types, extended addresses, and CRLF", () => {
  assert.deepEqual(readHex(ihex(record(0, 0x1000, [1, 2]), record(0, 0x1002, [3]), eof)), new Uint8Array([1, 2, 3]));
  const segment = ihex(record(2, 0, [0x01, 0x00]), record(0, 2, [7]), record(3, 0, [0, 0, 0x10, 2]), eof);
  assert.deepEqual(readHex(segment, { base: 0x1002 }), new Uint8Array([7]));
  const linear = [record(4, 0, [0x08, 0]), record(0, 0x1000, [8]), record(5, 0, [0x08, 0, 0x10, 0]), eof].join("\r\n");
  assert.deepEqual(readHex(encoder.encode(linear), { base: 0x08001000 }), new Uint8Array([8]));
  assert.deepEqual(readHex(ihex(record(4, 0, [255, 255]), record(0, 65535, [42]), eof), { base: 0xffffffff }), new Uint8Array([42]));
});

test("Intel HEX rejects malformed records, checksum errors, overflow, missing EOF, and overlap", () => {
  const data = record(0, 0x1000, [1, 2]);
  const cases = [
    [encoder.encode(`${data.slice(0, -2)}00\n${eof}`), /checksum/],
    [encoder.encode(":0100000001FE0\n" + eof), /malformed/],
    [encoder.encode(":0200000001FD\n" + eof), /length/],
    [encoder.encode(`:${"00".repeat(261)}\n${eof}`), /malformed/],
    [ihex(data), /EOF/],
    [ihex(eof, data), /after EOF/],
    [ihex(data, eof, eof), /after EOF/],
    [ihex(record(1, 1)), /EOF must/],
    [ihex(record(1, 0, [0])), /EOF must/],
    [ihex(record(6), eof), /unsupported record type/],
    [ihex(record(2, 1, [0, 0]), eof), /extended address/],
    [ihex(record(4, 0, [0]), eof), /extended address/],
    [ihex(record(3, 0, [0]), eof), /start address/],
    [ihex(record(5, 1, [0, 0, 0, 0]), eof), /start address/],
    [ihex(record(5, 0, [0, 0, 0, 0]), record(5, 0, [0, 0, 0, 0]), eof), /duplicate start/],
    [ihex(record(0, 65535, [1, 2]), eof), /boundary/],
    [ihex(data, record(0, 0x1001, [2]), eof), /overlapping/],
    [ihex(data, data, eof), /overlapping/],
    [ihex(eof), /no file-backed/],
    [encoder.encode("é"), /ASCII/],
    [encoder.encode(` ${data}\n${eof}`), /malformed/]
  ];
  for (const [input, pattern] of cases) assert.throws(() => readHex(input), pattern);
});

test("HEX sparse ranges require both explicit fields, exact lowest base, and include every byte", () => {
  const input = ihex(record(0, 0x1003, [3]), record(0, 0x1000, [1]), eof);
  assert.throws(() => readHex(input), /gaps/);
  assert.throws(() => readHex(input, { end: 0x1004 }), /together/);
  assert.throws(() => readHex(input, { fill: 255 }), /together/);
  assert.throws(() => readHex(input, { base: 0xfff, end: 0x1004, fill: 0 }), /lowest/);
  assert.throws(() => readHex(input, { base: 0x1001, end: 0x1004, fill: 0 }), /lowest/);
  assert.throws(() => readHex(input, { end: 0x1003, fill: 0 }), /outside/);
  assert.deepEqual(readHex(input, { end: 0x1006, fill: 255 }), new Uint8Array([1, 255, 255, 3, 255, 255]));
  assert.throws(() => readHex(input, { end: 0x1000 + MAX_BYTES + 1, fill: 0 }), /16 MiB/);
  assert.throws(() => readHex(input, { end: 0x1000, fill: 0 }), /above base/);
  assert.throws(() => readHex(input, { end: 0x1004, fill: 256 }), /one byte/);
});

for (const elf64 of [false, true]) {
  const name = elf64 ? "ELF64" : "ELF32";
  test(`${name} uses PT_LOAD physical addresses, excludes BSS, and ignores non-load segments`, () => {
    const input = elfFixture(elf64, [
      { address: 0x1000, data: [1, 2], memorySize: 99 },
      { address: 0x1002, data: [3] },
      { address: 0x1003, data: [], memorySize: 4096 },
      { address: 0x5000, data: [9], type: 4 }
    ]);
    assert.deepEqual(parseFirmware(input, { format: "elf", base: 0x1000 }), new Uint8Array([1, 2, 3]));
    assert.equal(parseElf(input).length, 2);
    assert.throws(() => parseFirmware(input, { format: "elf", base: 0x9000 }), /lowest/);
  });

  test(`${name} rejects malformed/truncated headers and invalid file-backed segments`, () => {
    const headerSize = elf64 ? 64 : 52;
    const mutations = [
      [view => view.setUint8(0, 0), /magic/],
      [view => view.setUint8(4, 3), /ELF32/],
      [view => view.setUint8(5, 2), /little-endian/],
      [view => view.setUint8(6, 2), /identification version/],
      [view => view.setUint16(16, 3, true), /ET_EXEC/],
      [view => view.setUint32(20, 2, true), /ELF version/],
      [view => view.setUint16(elf64 ? 52 : 40, 1, true), /header size/],
      [view => view.setUint16(elf64 ? 54 : 42, 1, true), /program header size/],
      [view => view.setUint16(elf64 ? 56 : 44, 0xffff, true), /ordinary/],
      [view => view.setUint16(elf64 ? 56 : 44, 500, true), /table is out of bounds/],
      [view => view.setUint32(elf64 ? 32 : 28, 0, true), /table is out of bounds/],
      [view => view.setUint32(headerSize + (elf64 ? 8 : 4), 0xffff, true), /file bytes/],
      [view => view.setUint32(headerSize + (elf64 ? 40 : 20), 1, true), /filesz exceeds memsz/],
      [view => view.setUint32(headerSize + (elf64 ? 24 : 12), 0xffffffff, true), /physical address/]
    ];
    for (const [mutate, pattern] of mutations) {
      const input = elfFixture(elf64);
      mutate(new DataView(input.buffer));
      assert.throws(() => parseElf(input), pattern);
    }
    const input = elfFixture(elf64);
    assert.throws(() => parseElf(input.subarray(0, 15)), /magic/);
    assert.throws(() => parseElf(input.subarray(0, headerSize - 1)), /header is truncated/);
    assert.throws(() => parseElf(input.subarray(0, input.length - 1)), /file bytes/);
    if (elf64) {
      const wide = elfFixture(true);
      new DataView(wide.buffer).setBigUint64(64 + 24, 0x100000000n, true);
      assert.throws(() => parseElf(wide), /physical address/);
      new DataView(wide.buffer).setBigUint64(32, 1n << 63n, true);
      assert.throws(() => parseElf(wide), /table is out of bounds/);
    }
  });

  test(`${name} rejects overlaps and sparse/out-of-range data without silently dropping it`, () => {
    const parse = (bytes, options = {}) => parseFirmware(bytes, { format: "elf", base: 0x1000, ...options });
    assert.throws(() => parse(elfFixture(elf64, [{ address: 0x1000, data: [1, 2] }, { address: 0x1001, data: [2] }])), /overlapping/);
    const sparse = elfFixture(elf64, [{ address: 0x1000, data: [1] }, { address: 0x1003, data: [4] }]);
    assert.throws(() => parse(sparse), /gaps/);
    assert.throws(() => parse(sparse, { end: 0x1003, fill: 0 }), /outside/);
    assert.throws(() => parse(sparse, { base: 0xff0, end: 0x1004, fill: 0 }), /lowest/);
    assert.deepEqual(parse(sparse, { end: 0x1005, fill: 0 }), new Uint8Array([1, 0, 0, 4, 0]));
    assert.throws(() => parse(elfFixture(elf64, [{ address: 0x1000, data: [], memorySize: 100 }])), /no file-backed/);
  });
}

test("segment normalization rejects invalid values before allocating output", () => {
  const bytes = new Uint8Array([1]);
  assert.throws(() => normalizeSegments([{ address: 0, bytes }], { base: undefined }), /Base/);
  assert.throws(() => normalizeSegments([{ address: NaN, bytes }], { base: 0 }), /segment/);
  assert.throws(() => normalizeSegments([{ address: 0, bytes: [] }], { base: 0 }), /segment/);
  assert.throws(() => parseIntelHex(new Uint8Array(MAX_BYTES + 1)), /16 MiB/);
});
