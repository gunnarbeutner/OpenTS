/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

fn main() {
    // Bake the public trust anchor as the pinned updater key. The key is mandatory:
    // a build with no `.sigkey.pub` fails here rather than producing a shell whose
    // updater can never verify a release. A runtime OPENTS_ENGINE_PUBKEY still
    // overrides this baked value in updater.rs. The file is per-environment and
    // gitignored, not committed.
    println!("cargo:rerun-if-changed=../../.sigkey.pub");
    let key = std::fs::read_to_string("../../.sigkey.pub").unwrap_or_default();
    let key = key.trim();
    if key.is_empty() {
        panic!(
            "no .sigkey.pub found at the repo root -- run \
             'node tools/sign-engine/sign-engine.mjs keygen' (dev) or provide the \
             release public key"
        );
    }
    println!("cargo:rustc-env=OPENTS_ENGINE_PUBKEY={key}");

    tauri_build::build()
}
