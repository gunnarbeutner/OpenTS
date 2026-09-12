/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Serves the WebAssembly engine's page and modules from one stable local origin,
// the `opents://` custom scheme, so the engine keeps a single origin for its OPFS
// and IndexedDB storage no matter where the bytes came from. Each logical file is
// resolved host-cache-first, then from the copy bundled into the binary through
// `frontendDist`. The updater in `updater.rs` writes the host cache; this module
// only reads it.

use std::{borrow::Cow, path::PathBuf};

use tauri::{
    http::{self, Response},
    AppHandle, Manager, Runtime, UriSchemeContext,
};

/// The custom scheme the main window is served from.
pub const SCHEME: &str = "opents";

/// The document served for a request that names no file.
const DEFAULT_FILE: &str = "index.html";

/// The host module cache, `<app_local_data>/engine/`. The updater installs verified
/// files here under their logical name; a missing directory means bundled-only.
pub fn module_cache_dir<R: Runtime>(app: &AppHandle<R>) -> Option<PathBuf> {
    app.path().app_local_data_dir().ok().map(|dir| dir.join("engine"))
}

fn mime_for(name: &str) -> &'static str {
    if name.ends_with(".html") {
        "text/html"
    } else if name.ends_with(".js") || name.ends_with(".mjs") {
        "text/javascript"
    } else if name.ends_with(".wasm") {
        "application/wasm"
    } else if name.ends_with(".json") {
        "application/json"
    } else if name.ends_with(".css") {
        "text/css"
    } else if name.ends_with(".data") {
        "application/octet-stream"
    } else {
        "application/octet-stream"
    }
}

/// Maps a request path to a logical file name, rejecting anything that could climb
/// out of the cache or bundle directory.
fn logical_name(path: &str) -> Option<String> {
    let trimmed = path.trim_start_matches('/');
    let name = if trimmed.is_empty() { DEFAULT_FILE } else { trimmed };
    if name.contains("..") || name.contains('\\') || name.starts_with('/') {
        return None;
    }
    Some(name.to_string())
}

/// The configured Content-Security-Policy as a header string, if the config sets one.
fn csp_header<R: Runtime>(app: &AppHandle<R>) -> Option<String> {
    app.config().app.security.csp.as_ref().map(|csp| csp.to_string())
}

/// Whether the engine's page module resolves from the host cache or the bundle.
///
/// A minimal build embeds only the loader page, so `Game.wasm` is absent until the
/// updater installs it. The launch flow uses this to tell a cold minimal start (show
/// the loader, then reload) from an offline build or a warm cache (engine already there).
pub fn engine_available<R: Runtime>(app: &AppHandle<R>) -> bool {
    if let Some(dir) = module_cache_dir(app) {
        if dir.join("Game.wasm").exists() {
            return true;
        }
    }
    // The asset resolver answers an unknown path with index.html, so a minimal build that
    // bundles only the loader would report Game.wasm as present. Accept it only if the bytes
    // are a WebAssembly module (the `\0asm` magic), not the loader page.
    app.asset_resolver()
        .get("/Game.wasm".into())
        .is_some_and(|asset| asset.bytes.starts_with(b"\0asm"))
}

/// Reads a logical file from the host cache, falling back to the bundled copy.
fn load<R: Runtime>(app: &AppHandle<R>, name: &str) -> Option<Vec<u8>> {
    if let Some(dir) = module_cache_dir(app) {
        let path = dir.join(name);
        if let Ok(bytes) = std::fs::read(&path) {
            return Some(bytes);
        }
    }
    app.asset_resolver().get(format!("/{name}")).map(|asset| asset.bytes)
}

/// Serves an engine file for a custom-scheme request. Relative requests the page
/// makes (`Game.js`, `Game.wasm`, ...) arrive here as their own paths and resolve
/// the same host-cache-then-bundle way as the top-level document.
pub fn handle_request<R: Runtime>(
    ctx: UriSchemeContext<'_, R>,
    request: http::Request<Vec<u8>>,
) -> Response<Cow<'static, [u8]>> {
    let app = ctx.app_handle();

    let Some(name) = logical_name(request.uri().path()) else {
        return error_response(http::StatusCode::BAD_REQUEST);
    };

    let Some(bytes) = load(app, &name) else {
        return error_response(http::StatusCode::NOT_FOUND);
    };

    let mut builder = Response::builder()
        .status(http::StatusCode::OK)
        .header(http::header::CONTENT_TYPE, mime_for(&name))
        .header(http::header::ACCESS_CONTROL_ALLOW_ORIGIN, "*");

    if name.ends_with(".html") {
        if let Some(csp) = csp_header(app) {
            builder = builder.header("Content-Security-Policy", csp);
        }
    }

    builder
        .body(Cow::Owned(bytes))
        .unwrap_or_else(|_| error_response(http::StatusCode::INTERNAL_SERVER_ERROR))
}

fn error_response(status: http::StatusCode) -> Response<Cow<'static, [u8]>> {
    Response::builder()
        .status(status)
        .header(http::header::CONTENT_TYPE, "text/plain")
        .body(Cow::Borrowed(b"".as_slice()))
        .expect("static error response")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn logical_name_defaults_to_index() {
        assert_eq!(logical_name("/").as_deref(), Some("index.html"));
        assert_eq!(logical_name("").as_deref(), Some("index.html"));
    }

    #[test]
    fn logical_name_rejects_traversal() {
        assert_eq!(logical_name("/../secret"), None);
        assert_eq!(logical_name("/a/../../b"), None);
        assert_eq!(logical_name("/a\\b"), None);
    }

    #[test]
    fn logical_name_keeps_module_files() {
        assert_eq!(logical_name("/Game.wasm").as_deref(), Some("Game.wasm"));
        assert_eq!(logical_name("/Game-asyncify.js").as_deref(), Some("Game-asyncify.js"));
    }

    #[test]
    fn mime_covers_module_files() {
        assert_eq!(mime_for("index.html"), "text/html");
        assert_eq!(mime_for("Game.js"), "text/javascript");
        assert_eq!(mime_for("Game.wasm"), "application/wasm");
        assert_eq!(mime_for("engine.json"), "application/json");
    }
}
