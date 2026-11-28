import { MAX_BYTES, parseAddress, parseFill, parseFirmware } from "./input.js";
import { hex, packSigned } from "./packer.js";

const element = id => document.getElementById(id);
const form = element("pack-form");
let downloadUrl;

function clearKey() {
  element("key-file").value = "";
  element("key-pem").value = "";
}

function clearResult() {
  if (downloadUrl) URL.revokeObjectURL(downloadUrl);
  downloadUrl = undefined;
  element("download").removeAttribute("href");
  element("result").hidden = true;
  for (const id of ["image-size", "package-size", "package-id", "signing-id", "image-hash"]) element(id).textContent = "";
}

function reportError(message) {
  element("status").textContent = "";
  element("error").textContent = message;
  element("error").hidden = false;
  element("error").focus();
}

element("firmware").addEventListener("change", () => {
  const name = element("firmware").files[0]?.name.toLowerCase() ?? "";
  if (/\.(hex|ihex)$/.test(name)) element("format").value = "hex";
  else if (/\.(elf|axf)$/.test(name)) element("format").value = "elf";
  else element("format").value = "bin";
  clearResult();
});

element("clear").addEventListener("click", () => {
  form.reset();
  clearKey();
  clearResult();
  element("error").hidden = true;
  element("error").textContent = "";
  element("status").textContent = "All inputs and the download have been cleared.";
  element("firmware").focus();
});

form.addEventListener("submit", async event => {
  event.preventDefault();
  clearResult();
  element("error").hidden = true;
  element("error").textContent = "";
  element("status").textContent = "Parsing firmware and signing locally…";
  let privateKeyPem;
  let keyFile;
  try {
    const file = element("firmware").files[0];
    if (!file || !file.size || file.size > MAX_BYTES) throw new Error("Select a firmware file from 1 byte to 16 MiB.");
    keyFile = element("key-file").files[0];
    privateKeyPem = element("key-pem").value;
    if (Boolean(keyFile) === Boolean(privateKeyPem.trim())) throw new Error("Choose exactly one private key source: file or pasted PEM.");
    if (keyFile && (!keyFile.size || keyFile.size > 65536)) throw new Error("Private key file must be 1 byte to 64 KiB.");
    if (privateKeyPem.length > 65536) throw new Error("Pasted private key must be at most 64 KiB.");
    const base = parseAddress(element("base").value, "Base");
    const endText = element("end").value.trim();
    const fillText = element("fill").value.trim();
    if (Boolean(endText) !== Boolean(fillText)) throw new Error("Supply both exclusive end and fill byte, or neither.");
    const end = endText ? parseAddress(endText, "Exclusive end", true) : undefined;
    const fill = fillText ? parseFill(fillText) : undefined;
    for (const control of form.elements) control.disabled = true;
    const image = parseFirmware(new Uint8Array(await file.arrayBuffer()), {
      format: element("format").value, base, end, fill
    });
    if (keyFile) privateKeyPem = await keyFile.text();
    keyFile = null;
    clearKey();
    const result = await packSigned({ image, base, productId: element("product").value, privateKeyPem });
    privateKeyPem = null;
    downloadUrl = URL.createObjectURL(new Blob([result.bytes], { type: "application/octet-stream" }));
    element("download").href = downloadUrl;
    const stem = file.name.replace(/\.[^.]+$/, "").replace(/[^a-zA-Z0-9_-]/g, "_") || "firmware";
    element("download").download = `${stem}.suf2`;
    element("image-size").textContent = `${image.length.toLocaleString()} bytes`;
    element("package-size").textContent = `${result.bytes.length.toLocaleString()} bytes · ${result.bytes.length / 512} blocks`;
    element("package-id").textContent = hex(result.packageId);
    element("signing-id").textContent = hex(result.signingKeyId);
    element("image-hash").textContent = hex(result.imageDigest);
    element("result").hidden = false;
    element("status").textContent = "Package ready. Key inputs cleared. Firmware is signed, not encrypted.";
    element("download").focus();
  } catch (error) {
    reportError(error.message || "Packing failed.");
  } finally {
    privateKeyPem = null;
    keyFile = null;
    clearKey();
    for (const control of form.elements) control.disabled = false;
  }
});

window.addEventListener("pagehide", () => {
  clearKey();
  clearResult();
});

if (!globalThis.crypto?.subtle) {
  reportError("WebCrypto is unavailable. Serve this dashboard over HTTPS or localhost in a supported browser.");
  element("pack").disabled = true;
}
