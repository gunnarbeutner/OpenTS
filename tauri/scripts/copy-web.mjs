/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stages the frontend directory Tauri embeds through `frontendDist`. Two variants:
//
// - Minimal (default): stage only tauri/loader/index.html as the bundled index.html.
//   The ~9MB engine is not embedded; the signed updater fetches it into the host cache
//   on first launch and the shell reloads the window onto the cached engine.
// - Offline (OPENTS_OFFLINE=1): stage the full engine page and modules so the exe runs
//   with nothing to download.
//
// The WebAssembly artifacts are built by CMake and are not in the repository, so the
// offline variant fails loudly rather than shipping a shell with nothing to run.
//
// OPENTS_WEB_DIRS is a list of `bin` directories, separated by the platform path delimiter
// (`;` on Windows, `:` elsewhere). More than one because the two suspension builds are
// separate configurations: a JSPI build emits Game.js, an ASYNCIFY build emits
// Game-asyncify.js, and the page picks between them at load time.

import { existsSync, mkdirSync, rmSync, copyFileSync, readdirSync } from "node:fs";
import { dirname, join, delimiter, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url)); // tauri/scripts
const projectRoot = resolve(here, ".."); // tauri
const repoRoot = resolve(projectRoot, ".."); // repository root
const destination = join(projectRoot, "src-tauri", "web");

const offline = process.env.OPENTS_OFFLINE === "1";

rmSync(destination, { recursive: true, force: true });
mkdirSync(destination, { recursive: true });

if (!offline) {
  const loader = join(projectRoot, "loader", "index.html");
  if (!existsSync(loader)) {
    console.error(`error: loader page not found at ${loader}.`);
    process.exit(1);
  }
  copyFileSync(loader, join(destination, "index.html"));
  console.log(`note: minimal build, bundled ${readdirSync(destination).join(" ")} (engine fetched on first launch)`);
  process.exit(0);
}

const defaults = [
  join(repoRoot, "build-wasm", "bin"),
  join(repoRoot, "build-wasm-asyncify", "bin"),
].join(delimiter);

const dirs = (process.env.OPENTS_WEB_DIRS || defaults).split(delimiter).filter(Boolean);
const modules = ["Game.js", "Game.wasm", "Game-asyncify.js", "Game-asyncify.wasm"];

let copiedPage = false;
let copiedModule = false;

for (const dir of dirs) {
  if (!existsSync(dir)) continue;

  if (!copiedPage && existsSync(join(dir, "index.html"))) {
    copyFileSync(join(dir, "index.html"), join(destination, "index.html"));
    copiedPage = true;
  }

  for (const name of modules) {
    const source = join(dir, name);
    if (existsSync(source) && !existsSync(join(destination, name))) {
      copyFileSync(source, join(destination, name));
      copiedModule = true;
    }
  }
}

if (!copiedPage || !copiedModule) {
  console.error(`error: no WebAssembly build found under OPENTS_WEB_DIRS (${dirs.join(delimiter)}).`);
  console.error("note: build the engine with Emscripten first; see ../docs/BUILDING.md.");
  process.exit(1);
}

console.log(`note: offline build, bundled ${readdirSync(destination).join(" ")}`);
