import {readFile, writeFile} from "node:fs/promises";
import path from "node:path";
import {webcrypto} from "node:crypto";
import {parseFirmware, packSigned} from "../../dashboard/packer.js";

const args = process.argv.slice(2);
if (args.shift() !== "pack" || args.length % 2 !== 0) {
  throw new Error("Expected pack and paired options");
}
const options = Object.fromEntries(Array.from({length: args.length / 2},
  (_, index) => [args[index * 2], args[index * 2 + 1]]));

try {
  const base = Number(options["--base"]);
  const image = parseFirmware(new Uint8Array(await readFile(options["--input"])), {
    format: path.extname(options["--input"]).slice(1),
    base,
    end: options["--end"] === undefined ? undefined : Number(options["--end"]),
    fill: options["--fill"] === undefined ? undefined : Number(options["--fill"]),
  });
  const result = await packSigned({
    image,
    base,
    productId: options["--product-id"],
    privateKeyPem: await readFile(options["--key"], "utf8"),
  }, webcrypto);
  await writeFile(options["--output"], result.bytes, {flag: "wx"});
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
