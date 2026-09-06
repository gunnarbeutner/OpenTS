/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A proof-of-concept Tauri shell around the WebAssembly engine, the cross-platform
// counterpart to the native shells under apple/. The engine, its page and its modules are
// staged into the frontend directory by scripts/copy-web.mjs, which becomes the bundled
// fallback, and served from the fixed `opents://localhost` origin by the custom protocol in
// module.rs. Serving from one stable origin regardless of where the module bytes came from is
// what keeps the engine's OPFS, IndexedDB block cache and saved games available across a
// relaunch. The game data is not bundled: the engine fetches it itself over HTTP from the
// address in `?manifestBase=`, the same switch the browser build and the Apple shells use.

use std::{fs, path::PathBuf};

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, WebviewUrl, WebviewWindowBuilder};

mod cache;
mod module;
mod updater;

/// The address a fresh install plays from: the project's own deployment.
const DEFAULT_BASE: &str = "https://play-ts.net/";

/// The WebSocket relay that stands in for a LAN, which enables the LAN multiplayer option.
const RELAY_URL: &str = "wss://relay.play-ts.net/";

/// Runs at document start in the engine's page. It reads the counters the engine keeps and
/// forwards a snapshot to the Rust side once a second, which is the POC's proof that the shell
/// can observe a run. `withGlobalTauri` puts `window.__TAURI__` in reach for the invoke.
const BRIDGE: &str = r#"
(function () {
  function core() {
    var c = window.__TAURI__ && window.__TAURI__.core;
    return c && typeof c.invoke === "function" ? c : null;
  }
  function fwd(line) {
    try { var c = core(); if (c) c.invoke("log_line", { line: String(line) }); } catch (e) {}
  }

  // The engine calls this from Prog_End when it exits, whether from the Exit menu or after a
  // fatal message box is dismissed.
  window.OpenTS_Quit = function () { try { var c = core(); if (c) c.invoke("quit_app"); } catch (e) {} };

  // Alt+Enter toggles fullscreen. Capture phase, so the engine's own key handling does not
  // consume it first.
  window.addEventListener("keydown", function (e) {
    if (e.altKey && (e.key === "Enter" || e.code === "Enter" || e.keyCode === 13)) {
      e.preventDefault();
      try { var c = core(); if (c) c.invoke("toggle_fullscreen"); } catch (ex) {}
    }
  }, true);

  // The engine's print and printErr land on console.log/console.error, so forwarding the
  // console carries the engine's own output out along with any JavaScript error.
  ["log", "info", "warn", "error"].forEach(function (level) {
    var original = console[level];
    console[level] = function () {
      try { fwd("[" + level + "] " + Array.prototype.map.call(arguments, String).join(" ")); } catch (e) {}
      return original.apply(console, arguments);
    };
  });
  window.addEventListener("error", function (e) {
    fwd("[window.error] " + (e.message || "") + " @ " + (e.filename || "") + ":" + (e.lineno || "") +
        (e.error && e.error.stack ? "\n" + e.error.stack : ""));
  });
  window.addEventListener("unhandledrejection", function (e) {
    var r = e.reason;
    fwd("[unhandledrejection] " + ((r && (r.stack || r.message)) || r));
  });

  // What the shell suspects for a blank frame: no WebGL2 context, or a JSPI module on a
  // WebView that cannot suspend.
  try {
    var probe = document.createElement("canvas");
    fwd("[probe] href=" + window.location.href);
    fwd("[probe] webgl2=" + !!probe.getContext("webgl2") +
        " jspi=" + (typeof WebAssembly !== "undefined" && typeof WebAssembly.Suspending === "function"));
  } catch (e) { fwd("[probe] failed: " + e); }

  function call(fn) { try { return typeof fn === "function" ? fn() : null; } catch (e) { return null; } }
  function snapshot() {
    var s = window.OpenTS_State || {};
    var m = window.Module || {};
    return {
      started: !!s.started,
      restored: s.restored || false,
      persistent: s.persistent || false,
      stallSeconds: s.stallSeconds || 0,
      isoRequests: call(m._OpenTS_Iso_Requests),
      isoFetched: call(m._OpenTS_Iso_Fetched),
      storeState: call(m._OpenTS_Iso_Store_State)
    };
  }
  function send() {
    try { var c = core(); if (c) c.invoke("report_state", { state: JSON.stringify(snapshot()) }); } catch (e) {}
  }
  setInterval(send, 1000);
})();
"#;

#[derive(Serialize, Deserialize, Clone)]
struct Settings {
    manifest_base: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self { manifest_base: DEFAULT_BASE.to_string() }
    }
}

fn is_usable(base: &str) -> bool {
    (base.starts_with("http://") && base.len() > "http://".len())
        || (base.starts_with("https://") && base.len() > "https://".len())
}

fn settings_file(app: &AppHandle) -> Option<PathBuf> {
    app.path().app_config_dir().ok().map(|dir| dir.join("settings.json"))
}

fn load_settings(app: &AppHandle) -> Settings {
    if let Some(path) = settings_file(app) {
        if let Ok(text) = fs::read_to_string(&path) {
            if let Ok(settings) = serde_json::from_str::<Settings>(&text) {
                if is_usable(&settings.manifest_base) {
                    return settings;
                }
            }
        }
    }
    Settings::default()
}

fn save_settings(app: &AppHandle, settings: &Settings) -> Result<(), String> {
    let path = settings_file(app).ok_or("no application config directory")?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let text = serde_json::to_string_pretty(settings).map_err(|e| e.to_string())?;
    fs::write(&path, text).map_err(|e| e.to_string())
}

/// Percent-encodes a manifest base so it survives as one query value.
fn encode_component(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    for byte in input.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(byte as char)
            }
            _ => out.push_str(&format!("%{byte:02X}")),
        }
    }
    out
}

#[tauri::command]
fn report_state(state: String) {
    // Stands in for the Apple shells' unified-log `page` category; swap in tauri-plugin-log
    // to reach a structured sink.
    eprintln!("[opents::page] {state}");
}

#[tauri::command]
fn log_line(line: String) {
    eprintln!("[opents::console] {line}");
}

#[tauri::command]
fn quit_app(app: AppHandle) {
    app.exit(0);
}

#[tauri::command]
fn toggle_fullscreen(window: tauri::WebviewWindow) -> Result<(), String> {
    let full = window.is_fullscreen().map_err(|e| e.to_string())?;
    window.set_fullscreen(!full).map_err(|e| e.to_string())
}

#[tauri::command]
fn get_server(app: AppHandle) -> String {
    load_settings(&app).manifest_base
}

/// Records the data server for the next launch. A running engine reads `?manifestBase=` once
/// at start, so the change takes effect when the page next loads rather than mid-run.
#[tauri::command]
fn set_server(app: AppHandle, base: String) -> Result<(), String> {
    if !is_usable(&base) {
        return Err("the server address must be an http or https URL".into());
    }
    save_settings(&app, &Settings { manifest_base: base })
}

/// Empties the engine's fetched block cache, the OPFS `opents-iso` directory the store keeps
/// blocks in. Saved games, kept in a separate IndexedDB database on the same origin, are
/// untouched.
#[tauri::command]
fn clear_iso_cache(window: tauri::WebviewWindow) -> Result<(), String> {
    window
        .eval(
            "navigator.storage.getDirectory().then(function (root) { \
                 return root.removeEntry('opents-iso', { recursive: true }); \
             }).catch(function () {});",
        )
        .map_err(|e| e.to_string())
}

/// Reports engine availability to the bundled loader page: `"loading"`, `"ready"`, or
/// `"error:<msg>"`. Only the minimal build's cold start ever sees anything but `"ready"`.
#[tauri::command]
fn module_status(state: tauri::State<updater::ModuleState>) -> String {
    state.0.lock().map(|guard| guard.clone()).unwrap_or_else(|_| "error:status unavailable".into())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .register_uri_scheme_protocol(module::SCHEME, module::handle_request)
        .invoke_handler(tauri::generate_handler![
            report_state,
            log_line,
            get_server,
            set_server,
            clear_iso_cache,
            module_status,
            quit_app,
            toggle_fullscreen,
            cache::cache_probe,
            cache::cache_read,
            cache::cache_write,
            cache::cache_room,
            cache::cache_clear
        ])
        .setup(|app| {
            let engine_present = module::engine_available(app.handle());
            app.manage(updater::ModuleState::new(if engine_present { "ready" } else { "loading" }));

            let base = load_settings(app.handle()).manifest_base;
            let url = format!(
                "{}://localhost/index.html?manifestBase={}&hosted=1&relay={}",
                module::SCHEME,
                encode_component(&base),
                encode_component(RELAY_URL)
            );
            let url = tauri::Url::parse(&url).map_err(|e| e.to_string())?;

            WebviewWindowBuilder::new(app.handle(), "main", WebviewUrl::CustomProtocol(url))
                .title("OpenTS")
                .inner_size(1280.0, 800.0)
                .initialization_script(BRIDGE)
                .build()?;

            updater::spawn(app.handle(), base, engine_present);

            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running the OpenTS shell");
}
