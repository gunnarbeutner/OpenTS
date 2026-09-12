// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors
//
// Publishes and signs the engine-module update tree the Tauri shell's updater
// consumes (tauri/src-tauri/src/updater.rs). It is the signing counterpart to that
// client: the descriptor bytes this tool signs are the exact bytes the client
// verifies with Ed25519 `verify_strict`, and the sha256 hex and base64 signature
// encodings match what the client parses.
//
// This is a SEPARATE representation from the browser-direct hashed serving in the
// Dockerfile. The browser loads Game.<hash>.js by a patched page.js on this same
// origin; the shell installs the seven files under its OWN origin and serves them by
// logical name, so the files signed here must be the RAW build output: a page.js that
// still references the logical "Game.js" and "Game-asyncify.js", and an index.html that
// still references "page.css" and "page.js".
//
// Signer: Node's built-in crypto, no dependencies. The private key is a PKCS#8 PEM
// Ed25519 key (`-----BEGIN PRIVATE KEY-----`), as produced by
// `openssl genpkey -algorithm ed25519 -out key.pem` or by this tool's `keygen`.
//
// Usage:
//   node sign-engine.mjs keygen [--out .sigkey] [--pub pub.b64] [--force]
//   node sign-engine.mjs sign \
//     --index <build/bin/index.html> \
//     --page-css <build/bin/page.css> --page-js <build/bin/page.js> \
//     --game-js <build/bin/Game.js> --game-wasm <build/bin/Game.wasm> \
//     --asyncify-js <build-asyncify/bin/Game-asyncify.js> \
//     --asyncify-wasm <build-asyncify/bin/Game-asyncify.wasm> \
//     --key <key.pem> --out <tree-dir> --sequence <YYYYMMDDHHMMSS> \
//     [--base <url-prefix>] [--version <str>] [--commit <str>]
//
// `keygen` writes the private key to `.sigkey` at the repo root and the raw 32-byte
// public key, standard base64, to `.sigkey.pub` beside it. Both are gitignored and
// supplied per environment (dev: keygen; production/CI: the release key files), never
// committed. `.sigkey.pub` is the client's trust anchor: build.rs bakes it as the
// pinned key. keygen refuses to overwrite `.sigkey` without `--force`. Feed the same
// `.sigkey` to the build as the secret:
//   docker build --secret id=engine_signing_key,src=.sigkey ...

import { createHash, createPrivateKey, generateKeyPairSync, sign } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, extname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

const POINTER_FORMAT = "opents-web-engine-pointer-v1";
const DESCRIPTOR_FORMAT = "opents-web-engine-v1";
const REQUIRES = { assets: "opents-web-assets-v2", movies: "MP4" };

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const token = argv[i];
    if (!token.startsWith("--")) throw new Error(`unexpected argument: ${token}`);
    const key = token.slice(2);
    const next = argv[i + 1];
    if (next === undefined || next.startsWith("--")) {
      args[key] = true;
    } else {
      args[key] = next;
      i++;
    }
  }
  return args;
}

function require(args, key) {
  const value = args[key];
  if (value === undefined || value === true) throw new Error(`missing --${key}`);
  return value;
}

function sha256Hex(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function keygen(args) {
  const out = args.out && args.out !== true ? args.out : join(repoRoot, ".sigkey");
  if (existsSync(out) && args.force !== true) {
    throw new Error(`${out} exists; pass --force to overwrite`);
  }
  const { privateKey, publicKey } = generateKeyPairSync("ed25519");
  writeFileSync(out, privateKey.export({ format: "pem", type: "pkcs8" }), { mode: 0o600 });
  const raw = Buffer.from(publicKey.export({ format: "jwk" }).x, "base64url").toString("base64");
  const pub = args.pub && args.pub !== true ? args.pub : join(repoRoot, ".sigkey.pub");
  writeFileSync(pub, raw + "\n");
  process.stderr.write(`wrote private key to ${out} and public key to ${pub}:\n`);
  process.stdout.write(raw + "\n");
}

// The seven logical files, each with the suspension strategy the client's engine
// picks between at load time. `index.html` names page.css and page.js, and page.js
// names the two module loaders, all by logical name.
//
// The shell installs them in this order, so index.html comes last: an install that
// stops partway then leaves the previous page in place, naming files that are still
// there, rather than a new page naming files that have not arrived.
function moduleInputs(args) {
  return [
    { name: "page.css", source: require(args, "page-css") },
    { name: "page.js", source: require(args, "page-js") },
    { name: "Game.js", source: require(args, "game-js"), suspend: "jspi" },
    { name: "Game.wasm", source: require(args, "game-wasm"), suspend: "jspi" },
    { name: "Game-asyncify.js", source: require(args, "asyncify-js"), suspend: "asyncify" },
    { name: "Game-asyncify.wasm", source: require(args, "asyncify-wasm"), suspend: "asyncify" },
    { name: "index.html", source: require(args, "index") },
  ];
}

function sequenceFrom(args) {
  if (args.sequence && args.sequence !== true) {
    if (!/^\d{14}$/.test(args.sequence)) throw new Error("--sequence must be 14 digits YYYYMMDDHHMMSS");
    return Number(args.sequence);
  }
  const now = new Date();
  const p = (n, w = 2) => String(n).padStart(w, "0");
  return Number(
    `${p(now.getUTCFullYear(), 4)}${p(now.getUTCMonth() + 1)}${p(now.getUTCDate())}` +
      `${p(now.getUTCHours())}${p(now.getUTCMinutes())}${p(now.getUTCSeconds())}`,
  );
}

function sign_tree(args) {
  const outDir = require(args, "out");
  const base = args.base && args.base !== true ? args.base : "";
  const sequence = sequenceFrom(args);
  const version = args.version && args.version !== true ? args.version : String(sequence);
  const commit = args.commit && args.commit !== true ? args.commit : "unknown";

  const privateKey = createPrivateKey(readFileSync(require(args, "key")));

  const filesDir = join(outDir, "engine", "files");
  mkdirSync(filesDir, { recursive: true });

  const files = moduleInputs(args).map((input) => {
    const bytes = readFileSync(input.source);
    const hash = sha256Hex(bytes);
    const ext = extname(input.name).slice(1);
    const stem = basename(input.name, extname(input.name));
    const rel = `${base}engine/files/${stem}.${hash.slice(0, 16)}.${ext}`;

    const dest = join(outDir, rel.slice(base.length));
    mkdirSync(dirname(dest), { recursive: true });
    writeFileSync(dest, bytes);

    const entry = { name: input.name, path: rel, sha256: hash, size: bytes.length };
    if (input.suspend) entry.suspend = input.suspend;
    return entry;
  });

  const descriptor = {
    format: DESCRIPTOR_FORMAT,
    sequence,
    version,
    commit,
    entry: "index.html",
    requires: REQUIRES,
    files,
  };

  // These exact bytes are what the client verifies and re-hashes, so the file
  // written and the bytes signed must be identical.
  const descriptorBytes = Buffer.from(JSON.stringify(descriptor, null, 2), "utf8");
  const descriptorHash = sha256Hex(descriptorBytes);
  const descriptorRel = `${base}engine/${descriptorHash}.json`;
  const descriptorDest = join(outDir, descriptorRel.slice(base.length));
  mkdirSync(dirname(descriptorDest), { recursive: true });
  writeFileSync(descriptorDest, descriptorBytes);

  const signature = sign(null, descriptorBytes, privateKey).toString("base64");
  const pointer = { format: POINTER_FORMAT, descriptor: descriptorRel, signature };
  writeFileSync(join(outDir, "engine.json"), Buffer.from(JSON.stringify(pointer, null, 2), "utf8"));

  process.stdout.write(`signed sequence ${sequence}, descriptor ${descriptorRel}\n`);
}

function main() {
  const [command, ...rest] = process.argv.slice(2);
  const args = parseArgs(rest);
  if (command === "keygen") {
    keygen(args);
  } else if (command === "sign") {
    sign_tree(args);
  } else {
    process.stderr.write("usage: sign-engine.mjs <keygen|sign> [options]\n");
    process.exit(2);
  }
}

main();
