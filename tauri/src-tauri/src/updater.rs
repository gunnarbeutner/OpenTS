/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A signed, offline-safe updater for the engine module served by `module.rs`.
//
// The trust root is a pinned Ed25519 public key. Without one the updater never
// runs and the shell serves only the host cache and the bundled fallback, so a
// live remote-code path can never open by accident. When a key is configured the
// launch-time pass fetches a pointer and a descriptor, verifies the descriptor's
// signature against the pinned key, and installs it only if it is strictly newer
// than everything already trusted, every file's sha256 matches, and its `requires`
// block matches what this shell supports. Any failure -- offline, missing pointer,
// bad signature, bad hash, incompatible requirements, or a rollback -- leaves the
// cache untouched.
//
// `sequence` is a stateless publish-time stamp: the UTC signing time as the 14
// digit integer YYYYMMDDHHMMSS, which fits a JSON number. The client treats it as
// an opaque monotonic integer and never parses the date. The anti-rollback floor
// is `max(cached, bundled)`: the sequence persisted for this base URL (an anchor an
// attacker off the trusted server cannot lower) and the sequence stamped into the
// bundled fallback. A fresh install with no cache floors at the bundled sequence.
//
// `run_update` holds the whole trust flow and takes a `Fetch` and explicit floors
// so the unit tests drive it with in-memory bytes and never touch the network.

use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
    sync::Mutex,
    time::Duration,
};

use base64::Engine as _;
use ed25519_dalek::{Signature, VerifyingKey};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use tauri::{AppHandle, Manager, Runtime};

/// The pinned Ed25519 public key, base64-encoded, or empty to disable updates.
/// A deployment overrides this at runtime with `OPENTS_ENGINE_PUBKEY`, which lets
/// the same build ship inert and later gain a trust root without a rebuild.
const PINNED_PUBKEY_B64: &str = "";

/// The sequence stamped into the bundled fallback, used when no bundled descriptor
/// ships and `OPENTS_ENGINE_BUNDLED_SEQUENCE` is unset. Zero means "oldest": any
/// signed release outranks it. A build that bundles a known module should stamp the
/// real publish sequence here or ship a bundled `descriptor.json`.
const BUNDLED_SEQUENCE: u64 = 0;

const POINTER_FORMAT: &str = "opents-web-engine-pointer-v1";
const DESCRIPTOR_FORMAT: &str = "opents-web-engine-v1";

/// The per-base-URL record of the highest installed sequence, the rollback anchor.
const SEQUENCES_FILE: &str = "sequences.json";

/// The requirement values this shell can run, checked against a descriptor's
/// `requires` block. A required key this shell does not know, or a value it does
/// not match, rejects the release.
fn supported_requirement(key: &str) -> Option<&'static str> {
    match key {
        "assets" => Some("opents-web-assets-v2"),
        "movies" => Some("MP4"),
        _ => None,
    }
}

#[derive(Deserialize)]
struct Pointer {
    format: String,
    descriptor: String,
    signature: String,
}

#[derive(Deserialize)]
struct FileEntry {
    name: String,
    path: String,
    sha256: String,
    #[serde(default)]
    size: Option<u64>,
}

#[derive(Deserialize)]
struct Descriptor {
    format: String,
    sequence: u64,
    #[serde(default)]
    requires: HashMap<String, String>,
    files: Vec<FileEntry>,
}

pub struct UpdateReport {
    pub sequence: u64,
    pub installed: usize,
}

/// The engine module's availability, as the bundled loader page polls it through the
/// `module_status` command. Its string form is the command's contract:
/// `"loading"` while a cold minimal start fetches the engine, `"ready"` once it is
/// installed or already present, and `"error:<msg>"` when the fetch fails.
pub struct ModuleState(pub Mutex<String>);

impl ModuleState {
    pub fn new(initial: &str) -> Self {
        ModuleState(Mutex::new(initial.to_string()))
    }
}

fn set_status<R: Runtime>(app: &AppHandle<R>, value: String) {
    if let Some(state) = app.try_state::<ModuleState>() {
        if let Ok(mut guard) = state.0.lock() {
            *guard = value;
        }
    }
}

/// Fetches bytes for a path relative to the configured base URL.
pub trait Fetch {
    fn get(&self, rel: &str) -> Result<Vec<u8>, String>;
}

fn sha256_hex(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut out = String::with_capacity(64);
    for byte in digest {
        out.push_str(&format!("{byte:02x}"));
    }
    out
}

fn read_sequences(engine_dir: &Path) -> HashMap<String, u64> {
    fs::read(engine_dir.join(SEQUENCES_FILE))
        .ok()
        .and_then(|raw| serde_json::from_slice(&raw).ok())
        .unwrap_or_default()
}

/// The sequence already trusted for this base URL, or zero when none is recorded.
fn cached_sequence(engine_dir: &Path, base_key: &str) -> u64 {
    read_sequences(engine_dir).get(base_key).copied().unwrap_or(0)
}

fn record_sequence(engine_dir: &Path, base_key: &str, sequence: u64) -> Result<(), String> {
    let mut map = read_sequences(engine_dir);
    map.insert(base_key.to_string(), sequence);
    let raw = serde_json::to_vec(&map).map_err(|e| e.to_string())?;
    fs::write(engine_dir.join(SEQUENCES_FILE), raw).map_err(|e| e.to_string())
}

/// Rejects a file name that could escape the engine directory.
fn safe_name(name: &str) -> Result<&str, String> {
    if name.is_empty() || name.contains("..") || name.contains('/') || name.contains('\\') {
        return Err(format!("descriptor names an unsafe file: {name}"));
    }
    Ok(name)
}

fn requirements_met(requires: &HashMap<String, String>) -> Result<(), String> {
    for (key, value) in requires {
        match supported_requirement(key) {
            Some(supported) if supported == value => {}
            _ => return Err(format!("unsupported requirement {key}={value}")),
        }
    }
    Ok(())
}

/// Runs the full update flow against `fetch`, installing into `engine_dir`.
///
/// Accepts a fetched descriptor only if its signature verifies against `pubkey`,
/// its `requires` block is satisfied, every changed file's sha256 matches, and its
/// sequence is strictly above `max(cached, bundled_sequence)` where `cached` is the
/// sequence last recorded for `base_key`. On any error nothing in `engine_dir`
/// changes. Returns the installed sequence and how many files were replaced.
pub fn run_update(
    engine_dir: &Path,
    base_key: &str,
    bundled_sequence: u64,
    pubkey: &VerifyingKey,
    fetch: &dyn Fetch,
) -> Result<UpdateReport, String> {
    let pointer_bytes = fetch.get("engine.json")?;
    let pointer: Pointer =
        serde_json::from_slice(&pointer_bytes).map_err(|e| format!("bad pointer: {e}"))?;
    if pointer.format != POINTER_FORMAT {
        return Err(format!("unexpected pointer format: {}", pointer.format));
    }

    let sig_bytes = base64::engine::general_purpose::STANDARD
        .decode(pointer.signature.trim())
        .map_err(|e| format!("bad signature encoding: {e}"))?;
    let signature =
        Signature::from_slice(&sig_bytes).map_err(|e| format!("bad signature: {e}"))?;

    let descriptor_bytes = fetch.get(&pointer.descriptor)?;
    pubkey
        .verify_strict(&descriptor_bytes, &signature)
        .map_err(|_| "descriptor signature does not match the pinned key".to_string())?;

    let descriptor: Descriptor =
        serde_json::from_slice(&descriptor_bytes).map_err(|e| format!("bad descriptor: {e}"))?;
    if descriptor.format != DESCRIPTOR_FORMAT {
        return Err(format!("unexpected descriptor format: {}", descriptor.format));
    }

    requirements_met(&descriptor.requires)?;

    let floor = bundled_sequence.max(cached_sequence(engine_dir, base_key));
    if descriptor.sequence <= floor {
        return Err(format!(
            "refusing rollback: descriptor sequence {} is not above floor {floor}",
            descriptor.sequence
        ));
    }

    fs::create_dir_all(engine_dir).map_err(|e| format!("cannot create engine dir: {e}"))?;

    let staged = stage_changed_files(engine_dir, &descriptor, fetch)?;
    let installed = staged.len();

    for (temp, dest) in &staged {
        fs::rename(temp, dest).map_err(|e| format!("cannot install {}: {e}", dest.display()))?;
    }

    record_sequence(engine_dir, base_key, descriptor.sequence)?;

    Ok(UpdateReport { sequence: descriptor.sequence, installed })
}

/// Downloads and verifies every file whose installed copy does not already match,
/// writing each to a temp file next to its destination. On any error every temp
/// written so far is removed so the cache stays as it was.
fn stage_changed_files(
    engine_dir: &Path,
    descriptor: &Descriptor,
    fetch: &dyn Fetch,
) -> Result<Vec<(PathBuf, PathBuf)>, String> {
    let mut staged: Vec<(PathBuf, PathBuf)> = Vec::new();

    let result = (|| {
        for file in &descriptor.files {
            let name = safe_name(&file.name)?;
            let dest = engine_dir.join(name);
            let want = file.sha256.to_lowercase();

            if let Ok(existing) = fs::read(&dest) {
                if sha256_hex(&existing) == want {
                    continue;
                }
            }

            let bytes = fetch.get(&file.path)?;
            if let Some(size) = file.size {
                if bytes.len() as u64 != size {
                    return Err(format!("{name}: size {} does not match {size}", bytes.len()));
                }
            }
            let got = sha256_hex(&bytes);
            if got != want {
                return Err(format!("{name}: sha256 {got} does not match {want}"));
            }

            let temp = engine_dir.join(format!("{name}.tmp-{}", descriptor.sequence));
            fs::write(&temp, &bytes).map_err(|e| format!("cannot stage {name}: {e}"))?;
            staged.push((temp, dest));
        }
        Ok(())
    })();

    if let Err(err) = result {
        for (temp, _) in &staged {
            let _ = fs::remove_file(temp);
        }
        return Err(err);
    }

    Ok(staged)
}

/// The pinned key, or `None` when none is configured, which keeps the updater inert.
///
/// Resolution precedence: a runtime `OPENTS_ENGINE_PUBKEY` override, then the value
/// `build.rs` baked from a committed `.sigkey.pub` (also named `OPENTS_ENGINE_PUBKEY`,
/// read here through `option_env!`), then the `PINNED_PUBKEY_B64` constant.
fn pinned_key() -> Option<VerifyingKey> {
    let encoded = std::env::var("OPENTS_ENGINE_PUBKEY")
        .ok()
        .filter(|value| !value.trim().is_empty())
        .or_else(|| {
            option_env!("OPENTS_ENGINE_PUBKEY")
                .map(str::to_string)
                .filter(|value| !value.trim().is_empty())
        })
        .unwrap_or_else(|| PINNED_PUBKEY_B64.to_string());
    if encoded.trim().is_empty() {
        return None;
    }

    let raw = match base64::engine::general_purpose::STANDARD.decode(encoded.trim()) {
        Ok(raw) => raw,
        Err(err) => {
            eprintln!("[opents::engine] ignoring malformed pinned key: {err}");
            return None;
        }
    };
    let bytes: [u8; 32] = match raw.try_into() {
        Ok(bytes) => bytes,
        Err(_) => {
            eprintln!("[opents::engine] pinned key is not 32 bytes");
            return None;
        }
    };
    match VerifyingKey::from_bytes(&bytes) {
        Ok(key) => Some(key),
        Err(err) => {
            eprintln!("[opents::engine] pinned key is not a valid Ed25519 key: {err}");
            None
        }
    }
}

/// The bundled fallback's sequence, from a bundled `descriptor.json` if the engine
/// build ships one, then `OPENTS_ENGINE_BUNDLED_SEQUENCE`, then `BUNDLED_SEQUENCE`.
fn bundled_sequence<R: Runtime>(app: &AppHandle<R>) -> u64 {
    #[derive(Deserialize)]
    struct SequenceOnly {
        sequence: u64,
    }

    if let Some(asset) = app.asset_resolver().get("/descriptor.json".into()) {
        if let Ok(parsed) = serde_json::from_slice::<SequenceOnly>(&asset.bytes) {
            return parsed.sequence;
        }
    }
    std::env::var("OPENTS_ENGINE_BUNDLED_SEQUENCE")
        .ok()
        .and_then(|value| value.trim().parse().ok())
        .unwrap_or(BUNDLED_SEQUENCE)
}

struct HttpFetch {
    client: reqwest::blocking::Client,
    base: reqwest::Url,
}

impl HttpFetch {
    fn new(base: &str) -> Result<Self, String> {
        let mut base = base.to_string();
        if !base.ends_with('/') {
            base.push('/');
        }
        let base = reqwest::Url::parse(&base).map_err(|e| format!("bad base url: {e}"))?;
        let client = reqwest::blocking::Client::builder()
            .connect_timeout(Duration::from_secs(10))
            .timeout(Duration::from_secs(30))
            .build()
            .map_err(|e| format!("cannot build http client: {e}"))?;
        Ok(HttpFetch { client, base })
    }
}

impl Fetch for HttpFetch {
    fn get(&self, rel: &str) -> Result<Vec<u8>, String> {
        let url = self.base.join(rel).map_err(|e| format!("bad url for {rel}: {e}"))?;
        let response = self.client.get(url).send().map_err(|e| e.to_string())?;
        if !response.status().is_success() {
            return Err(format!("{rel}: http {}", response.status()));
        }
        Ok(response.bytes().map_err(|e| e.to_string())?.to_vec())
    }
}

/// Starts the launch-time update pass on a background thread when a pinned key is
/// configured. The pass is non-fatal to an installed engine: every failure is logged
/// and the shell keeps serving the current cache and bundle. `base` is the update
/// source, which defaults to the game-data server and can be overridden with
/// `OPENTS_ENGINE_BASE`.
///
/// `engine_present` is whether the engine already resolves at launch (an offline bundle
/// or a warm cache). When it does not, this is a cold minimal start showing the loader:
/// a successful install reloads the main window onto the now-cached engine, and any
/// failure sets `module_status` to `error:<msg>` so the loader stops spinning.
pub fn spawn<R: Runtime>(app: &AppHandle<R>, base: String, engine_present: bool) {
    let Some(pubkey) = pinned_key() else {
        if !engine_present {
            set_status(app, "error:no engine bundled and no update key configured".into());
        }
        return;
    };
    let Some(engine_dir) = crate::module::module_cache_dir(app) else {
        eprintln!("[opents::engine] no local data directory; skipping update");
        if !engine_present {
            set_status(app, "error:no local data directory".into());
        }
        return;
    };
    let base = std::env::var("OPENTS_ENGINE_BASE")
        .ok()
        .filter(|value| !value.trim().is_empty())
        .unwrap_or(base);
    let bundled = bundled_sequence(app);
    let app = app.clone();

    std::thread::spawn(move || {
        let fetch = match HttpFetch::new(&base) {
            Ok(fetch) => fetch,
            Err(err) => {
                eprintln!("[opents::engine] {err}");
                if !engine_present {
                    set_status(&app, format!("error:{err}"));
                }
                return;
            }
        };
        match run_update(&engine_dir, &base, bundled, &pubkey, &fetch) {
            Ok(report) => {
                eprintln!(
                    "[opents::engine] up to date at sequence {} ({} file(s) installed)",
                    report.sequence, report.installed
                );
                set_status(&app, "ready".into());
                if !engine_present {
                    if let Some(window) = app.get_webview_window("main") {
                        let _ = window.eval("window.location.reload()");
                    }
                }
            }
            Err(err) => {
                eprintln!("[opents::engine] update skipped: {err}");
                if !engine_present {
                    set_status(&app, format!("error:{err}"));
                }
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    use ed25519_dalek::{Signer, SigningKey};

    const BASE: &str = "https://play-ts.net/";

    struct MapFetch {
        entries: HashMap<String, Vec<u8>>,
    }

    impl Fetch for MapFetch {
        fn get(&self, rel: &str) -> Result<Vec<u8>, String> {
            self.entries.get(rel).cloned().ok_or_else(|| format!("missing {rel}"))
        }
    }

    // Serves an on-disk signed tree the way the client fetches it: paths relative
    // to a base of "", so each request maps straight to a file under the tree root.
    struct DirFetch {
        root: PathBuf,
    }

    impl Fetch for DirFetch {
        fn get(&self, rel: &str) -> Result<Vec<u8>, String> {
            fs::read(self.root.join(rel)).map_err(|e| format!("{rel}: {e}"))
        }
    }

    fn temp_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "opents-updater-test-{}-{n}",
            std::process::id()
        ));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn signing_key() -> SigningKey {
        SigningKey::from_bytes(&[7u8; 32])
    }

    /// Builds a fetch map for a single release plus the raw descriptor bytes and
    /// their signature, so a test can then tamper before running.
    fn make_release(seq: u64) -> (MapFetch, Vec<u8>, String) {
        let files: Vec<(&str, &str, Vec<u8>)> = vec![
            ("index.html", "engine/files/index.h.html", b"<html>engine</html>".to_vec()),
            ("Game.js", "engine/files/Game.h.js", b"// jspi loader".to_vec()),
            ("Game.wasm", "engine/files/Game.h.wasm", b"\0asm-jspi".to_vec()),
            ("Game-asyncify.js", "engine/files/Game-asyncify.h.js", b"// asyncify".to_vec()),
            ("Game-asyncify.wasm", "engine/files/Game-asyncify.h.wasm", b"\0asm-async".to_vec()),
        ];

        let file_json: Vec<serde_json::Value> = files
            .iter()
            .map(|(name, path, bytes)| {
                serde_json::json!({
                    "name": name,
                    "path": path,
                    "sha256": sha256_hex(bytes),
                    "size": bytes.len(),
                })
            })
            .collect();

        let descriptor = serde_json::json!({
            "format": DESCRIPTOR_FORMAT,
            "sequence": seq,
            "version": "test",
            "commit": "deadbeef",
            "entry": "index.html",
            "requires": { "assets": "opents-web-assets-v2", "movies": "MP4" },
            "files": file_json,
        });
        let descriptor_bytes = serde_json::to_vec(&descriptor).unwrap();

        let key = signing_key();
        let signature = key.sign(&descriptor_bytes);
        let signature_b64 = base64::engine::general_purpose::STANDARD.encode(signature.to_bytes());

        let pointer = serde_json::json!({
            "format": POINTER_FORMAT,
            "descriptor": "engine/desc.json",
            "signature": signature_b64,
        });

        let mut entries = HashMap::new();
        entries.insert("engine.json".to_string(), serde_json::to_vec(&pointer).unwrap());
        entries.insert("engine/desc.json".to_string(), descriptor_bytes.clone());
        for (_, path, bytes) in &files {
            entries.insert(path.to_string(), bytes.clone());
        }

        (MapFetch { entries }, descriptor_bytes, signature_b64)
    }

    #[test]
    fn valid_release_installs() {
        let dir = temp_dir();
        let (fetch, _, _) = make_release(20260904143000);
        let key = signing_key().verifying_key();

        let report =
            run_update(&dir, BASE, 20260101000000, &key, &fetch).expect("valid release installs");
        assert_eq!(report.sequence, 20260904143000);
        assert_eq!(report.installed, 5);

        assert_eq!(fs::read(dir.join("index.html")).unwrap(), b"<html>engine</html>");
        assert_eq!(fs::read(dir.join("Game.wasm")).unwrap(), b"\0asm-jspi");
        assert_eq!(cached_sequence(&dir, BASE), 20260904143000);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn tampered_descriptor_is_rejected() {
        let dir = temp_dir();
        let (mut fetch, descriptor_bytes, _) = make_release(20260904143000);
        let mut tampered = descriptor_bytes.clone();
        tampered.push(b' '); // any change breaks the signature over exact bytes
        fetch.entries.insert("engine/desc.json".to_string(), tampered);
        let key = signing_key().verifying_key();

        assert!(run_update(&dir, BASE, 0, &key, &fetch).is_err());
        assert!(!dir.join("index.html").exists());
        assert_eq!(cached_sequence(&dir, BASE), 0);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn tampered_signature_is_rejected() {
        let dir = temp_dir();
        let (mut fetch, _, signature_b64) = make_release(20260904143000);
        let mut raw = base64::engine::general_purpose::STANDARD.decode(&signature_b64).unwrap();
        raw[0] ^= 0x01;
        let bad = base64::engine::general_purpose::STANDARD.encode(&raw);
        let pointer = serde_json::json!({
            "format": POINTER_FORMAT,
            "descriptor": "engine/desc.json",
            "signature": bad,
        });
        fetch.entries.insert("engine.json".to_string(), serde_json::to_vec(&pointer).unwrap());
        let key = signing_key().verifying_key();

        assert!(run_update(&dir, BASE, 0, &key, &fetch).is_err());
        assert!(!dir.join("index.html").exists());

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn bad_file_hash_is_rejected() {
        let dir = temp_dir();
        let (mut fetch, _, _) = make_release(20260904143000);
        // Serve the right path but corrupted bytes, so the sha256 no longer matches.
        fetch
            .entries
            .insert("engine/files/Game.h.wasm".to_string(), b"corrupted".to_vec());
        let key = signing_key().verifying_key();

        assert!(run_update(&dir, BASE, 0, &key, &fetch).is_err());
        assert!(!dir.join("index.html").exists());
        assert!(!dir.join("Game.wasm").exists());
        assert_eq!(cached_sequence(&dir, BASE), 0);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn requirement_mismatch_is_rejected() {
        let dir = temp_dir();
        // Sign a descriptor that demands an asset version this shell does not run.
        let files = serde_json::json!([{
            "name": "index.html",
            "path": "engine/files/index.h.html",
            "sha256": sha256_hex(b"x"),
            "size": 1,
        }]);
        let descriptor = serde_json::json!({
            "format": DESCRIPTOR_FORMAT,
            "sequence": 20260904143000u64,
            "entry": "index.html",
            "requires": { "assets": "opents-web-assets-v9" },
            "files": files,
        });
        let descriptor_bytes = serde_json::to_vec(&descriptor).unwrap();
        let signature = signing_key().sign(&descriptor_bytes);
        let signature_b64 = base64::engine::general_purpose::STANDARD.encode(signature.to_bytes());
        let pointer = serde_json::json!({
            "format": POINTER_FORMAT,
            "descriptor": "engine/desc.json",
            "signature": signature_b64,
        });
        let mut entries = HashMap::new();
        entries.insert("engine.json".to_string(), serde_json::to_vec(&pointer).unwrap());
        entries.insert("engine/desc.json".to_string(), descriptor_bytes);
        entries.insert("engine/files/index.h.html".to_string(), b"x".to_vec());
        let fetch = MapFetch { entries };
        let key = signing_key().verifying_key();

        assert!(run_update(&dir, BASE, 0, &key, &fetch).is_err());
        assert!(!dir.join("index.html").exists());

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn sequence_at_or_below_floor_is_rejected() {
        let dir = temp_dir();
        let key = signing_key().verifying_key();
        let floor = 20260904143000u64;

        // Equal to the bundled floor: rejected.
        let (equal, _, _) = make_release(floor);
        assert!(run_update(&dir, BASE, floor, &key, &equal).is_err());
        assert!(!dir.join("index.html").exists());

        // Below the bundled floor: rejected.
        let (below, _, _) = make_release(floor - 1);
        assert!(run_update(&dir, BASE, floor, &key, &below).is_err());
        assert!(!dir.join("index.html").exists());
        assert_eq!(cached_sequence(&dir, BASE), 0);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn strictly_above_floor_is_accepted_then_anchors() {
        let dir = temp_dir();
        let key = signing_key().verifying_key();
        let bundled = 20260101000000u64;

        // First release above the bundled floor installs and anchors the sequence.
        let first = 20260901000000u64;
        let (r1, _, _) = make_release(first);
        run_update(&dir, BASE, bundled, &key, &r1).expect("first above-floor release installs");
        assert_eq!(cached_sequence(&dir, BASE), first);

        // Re-serving the same sequence is now at the cached floor: rejected.
        let (same, _, _) = make_release(first);
        assert!(run_update(&dir, BASE, bundled, &key, &same).is_err());
        assert_eq!(cached_sequence(&dir, BASE), first);

        // A strictly newer release installs and re-anchors.
        let second = 20260902000000u64;
        let (r2, _, _) = make_release(second);
        run_update(&dir, BASE, bundled, &key, &r2).expect("newer release installs");
        assert_eq!(cached_sequence(&dir, BASE), second);

        let _ = fs::remove_dir_all(&dir);
    }

    fn node_command() -> Option<String> {
        for candidate in ["node", "node.exe"] {
            if std::process::Command::new(candidate)
                .arg("--version")
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status()
                .map(|s| s.success())
                .unwrap_or(false)
            {
                return Some(candidate.to_string());
            }
        }
        None
    }

    /// Proves the publisher and client agree byte-for-byte: keygen -> sign with the
    /// tools/sign-engine tool, then verify and install the tool's real output through
    /// the client's own `verify_strict` and `run_update` paths. Skips when node is
    /// absent so `cargo test` never depends on the environment.
    #[test]
    fn tool_output_round_trips() {
        use std::process::{Command, Stdio};

        let Some(node) = node_command() else {
            eprintln!("skipping tool_output_round_trips: node not found");
            return;
        };
        let tool = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("..")
            .join("..")
            .join("tools")
            .join("sign-engine")
            .join("sign-engine.mjs");
        if !tool.exists() {
            eprintln!("skipping tool_output_round_trips: {} not found", tool.display());
            return;
        }

        let work = temp_dir();
        let key_path = work.join("dev.sigkey");
        let pub_path = work.join("pub.b64");

        let ok = Command::new(&node)
            .arg(&tool)
            .args(["keygen", "--out"])
            .arg(&key_path)
            .arg("--pub")
            .arg(&pub_path)
            .stdout(Stdio::null())
            .status()
            .expect("run keygen")
            .success();
        assert!(ok, "keygen failed");

        let indir = work.join("in");
        fs::create_dir_all(&indir).unwrap();
        fs::write(
            indir.join("index.html"),
            b"<html><script src=\"Game.js\"></script><script src=\"Game-asyncify.js\"></script></html>",
        )
        .unwrap();
        fs::write(indir.join("Game.js"), b"// jspi loader").unwrap();
        fs::write(indir.join("Game.wasm"), b"\0asm-jspi").unwrap();
        fs::write(indir.join("Game-asyncify.js"), b"// asyncify loader").unwrap();
        fs::write(indir.join("Game-asyncify.wasm"), b"\0asm-async").unwrap();

        let tree = work.join("tree");
        let ok = Command::new(&node)
            .arg(&tool)
            .arg("sign")
            .arg("--index")
            .arg(indir.join("index.html"))
            .arg("--game-js")
            .arg(indir.join("Game.js"))
            .arg("--game-wasm")
            .arg(indir.join("Game.wasm"))
            .arg("--asyncify-js")
            .arg(indir.join("Game-asyncify.js"))
            .arg("--asyncify-wasm")
            .arg(indir.join("Game-asyncify.wasm"))
            .arg("--key")
            .arg(&key_path)
            .arg("--out")
            .arg(&tree)
            .args(["--sequence", "20260904143000", "--version", "test", "--commit", "abc123"])
            .stdout(Stdio::null())
            .status()
            .expect("run sign")
            .success();
        assert!(ok, "sign failed");

        let raw = base64::engine::general_purpose::STANDARD
            .decode(fs::read_to_string(&pub_path).unwrap().trim())
            .unwrap();
        let bytes: [u8; 32] = raw.try_into().expect("32-byte public key");
        let pubkey = VerifyingKey::from_bytes(&bytes).unwrap();

        // Verify the signature over the descriptor's exact bytes, the client's path.
        let pointer: serde_json::Value =
            serde_json::from_slice(&fs::read(tree.join("engine.json")).unwrap()).unwrap();
        let descriptor_rel = pointer["descriptor"].as_str().unwrap();
        let sig = base64::engine::general_purpose::STANDARD
            .decode(pointer["signature"].as_str().unwrap())
            .unwrap();
        let descriptor_bytes = fs::read(tree.join(descriptor_rel)).unwrap();
        pubkey
            .verify_strict(&descriptor_bytes, &Signature::from_slice(&sig).unwrap())
            .expect("client verify accepts the tool's signature");

        // Full client acceptance: fetch the tree, install, and confirm the installed
        // index.html is the logical-name version.
        let install = work.join("install");
        let report = run_update(&install, BASE, 0, &pubkey, &DirFetch { root: tree.clone() })
            .expect("client installs the tool's output");
        assert_eq!(report.sequence, 20260904143000);
        assert_eq!(report.installed, 5);
        let index = fs::read_to_string(install.join("index.html")).unwrap();
        assert!(index.contains("\"Game.js\""), "installed index keeps logical module names");

        let _ = fs::remove_dir_all(&work);
    }
}
