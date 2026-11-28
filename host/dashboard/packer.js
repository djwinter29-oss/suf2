import { MAX_BYTES } from "./input.js";
export { parseFirmware } from "./input.js";

export const P256_ORDER = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
const DOMAIN = new TextEncoder().encode("SUF2-MANIFEST-V1\0");

export function hex(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("");
}

export function parseProductId(value) {
  if (typeof value !== "string" || !/^[0-9a-f]{32}$/i.test(value)) {
    throw new Error("Product ID must be exactly 32 hexadecimal digits.");
  }
  return Uint8Array.from(value.match(/../g), pair => Number.parseInt(pair, 16));
}

function decodeBase64(value) {
  return Uint8Array.from(atob(value), character => character.charCodeAt(0));
}

function pemBytes(pem) {
  if (typeof pem !== "string" || pem.length > 65536) throw new Error("Private key PEM must be at most 64 KiB.");
  const match = /^\s*-----BEGIN PRIVATE KEY-----\r?\n([A-Za-z0-9+/=\r\n]+)\r?\n-----END PRIVATE KEY-----\s*$/.exec(pem);
  if (!match) throw new Error("Use an unencrypted PKCS#8 PEM (BEGIN PRIVATE KEY), not SEC1 or an encrypted key.");
  const body = match[1].replace(/[\r\n]/g, "");
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(body) || !body.length) {
    throw new Error("Private key PEM contains invalid base64.");
  }
  return decodeBase64(body);
}

export function lowSSignature(signature) {
  if (!(signature instanceof Uint8Array) || signature.length !== 64) {
    throw new Error("WebCrypto must return a 64-byte IEEE P1363 P-256 signature.");
  }
  const integer = bytes => BigInt(`0x${hex(bytes)}`);
  const r = integer(signature.subarray(0, 32));
  let s = integer(signature.subarray(32));
  if (r < 1n || r >= P256_ORDER || s < 1n || s >= P256_ORDER) throw new Error("Invalid ECDSA signature integers.");
  if (s > (P256_ORDER - 1n) / 2n) s = P256_ORDER - s;
  const result = signature.slice();
  result.set(Uint8Array.from(s.toString(16).padStart(64, "0").match(/../g), pair => Number.parseInt(pair, 16)), 32);
  return result;
}

export async function packSigned({ image, base, productId, privateKeyPem }, crypto = globalThis.crypto) {
  if (!crypto?.subtle) throw new Error("WebCrypto is unavailable. Use HTTPS or localhost in a supported browser.");
  if (!(image instanceof Uint8Array) || !image.length || image.length > MAX_BYTES) {
    throw new Error("Image must be 1 byte to 16 MiB.");
  }
  if (!Number.isInteger(base) || base < 0 || base > 0xffffffff || base + image.length > 0x100000000) {
    throw new Error("Image execution range is outside the 32-bit address space.");
  }
  const product = parseProductId(productId);
  image = image.slice();
  let der;
  let temporaryKey;
  let signingKey;
  let jwk;
  try {
    der = pemBytes(privateKeyPem);
    privateKeyPem = null;
    try {
      temporaryKey = await crypto.subtle.importKey("pkcs8", der, { name: "ECDSA", namedCurve: "P-256" }, true, ["sign"]);
      jwk = await crypto.subtle.exportKey("jwk", temporaryKey);
      temporaryKey = null;
      const coordinate = value => decodeBase64(value.replace(/-/g, "+").replace(/_/g, "/") + "=");
      const x = coordinate(jwk.x);
      const y = coordinate(jwk.y);
      if (x.length !== 32 || y.length !== 32) throw new Error("Invalid public coordinates.");
      const publicPoint = new Uint8Array(65);
      publicPoint[0] = 4;
      publicPoint.set(x, 1);
      publicPoint.set(y, 33);
      delete jwk.d;
      jwk = null;
      signingKey = await crypto.subtle.importKey("pkcs8", der, { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
      der.fill(0);
      const signingKeyId = new Uint8Array(await crypto.subtle.digest("SHA-256", publicPoint)).slice(0, 16);
      const manifest = new Uint8Array(256);
      const view = new DataView(manifest.buffer);
      manifest.set(new TextEncoder().encode("SUF2MFST"));
      view.setUint32(8, 1, true);
      manifest.set(product, 16);
      view.setUint32(32, base, true);
      view.setUint32(36, image.length, true);
      manifest.set(signingKeyId, 40);
      const imageDigest = new Uint8Array(await crypto.subtle.digest("SHA-256", image));
      manifest.set(imageDigest, 96);
      manifest.set(imageDigest, 128);
      const message = new Uint8Array(DOMAIN.length + 192);
      message.set(DOMAIN);
      message.set(manifest.subarray(0, 192), DOMAIN.length);
      const signature = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, signingKey, message);
      signingKey = null;
      manifest.set(lowSSignature(new Uint8Array(signature)), 192);
      const verifyingKey = await crypto.subtle.importKey("raw", publicPoint, { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
      if (!await crypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, verifyingKey, manifest.subarray(192), message)) {
        throw new Error("The private key does not match its public point; no package was produced.");
      }
      const packageId = new Uint8Array(await crypto.subtle.digest("SHA-256", manifest)).slice(0, 16);
      const blocks = Math.ceil(image.length / 256) + 1;
      const bytes = new Uint8Array(blocks * 512);
      const transport = new DataView(bytes.buffer);
      for (let block = 0; block < blocks; block++) {
        const offset = block * 512;
        transport.setUint32(offset, 0x0a324655, true);
        transport.setUint32(offset + 4, 0x9e5d5157, true);
        transport.setUint32(offset + 8, 0x2001, true);
        transport.setUint32(offset + 16, 272, true);
        transport.setUint32(offset + 20, block, true);
        transport.setUint32(offset + 24, blocks, true);
        transport.setUint32(offset + 28, 0x1e0424ed, true);
        bytes.set(packageId, offset + 32);
        bytes.set(block === 0 ? manifest : image.subarray((block - 1) * 256, block * 256), offset + 48);
        transport.setUint32(offset + 508, 0x0ab16f30, true);
      }
      return { bytes, manifest, packageId, signingKeyId, imageDigest };
    } catch (error) {
      if (error.name === "DataError" || error.name === "OperationError" || error.name === "SyntaxError") {
        throw new Error("Unable to use this key. Supply a valid unencrypted PKCS#8 ECDSA P-256 private key.", { cause: error });
      }
      throw error;
    }
  } finally {
    der?.fill(0);
    if (jwk) delete jwk.d;
    jwk = null;
    temporaryKey = null;
    signingKey = null;
    privateKeyPem = null;
  }
}
