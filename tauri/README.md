# OpenTS Tauri shell

The native shell around the WebAssembly engine, across desktop and mobile. Desktop (Windows and
macOS) is wired up here; the crate carries a mobile entry point (`run` in
`src-tauri/src/lib.rs`) for the iOS and Android work Tauri is chosen for.

## How it works

The engine already emits the browser build unchanged. The shell stages a frontend directory
into `src-tauri/web/` via `scripts/copy-web.mjs`, which Tauri embeds through `frontendDist` and
serves same origin, which keeps the engine's IndexedDB block cache and saves across relaunches.
The window opens the engine's page with the data server, a native-host marker, and the WebSocket
relay passed in the query the engine reads; the engine then fetches all game data from that
server itself (`code/manifest.cpp`). A bridge script forwards `window.OpenTS_State` to Rust and
wires Alt+Enter and the engine's quit request to the window.

## Build variants

`copy-web.mjs` stages one of two frontends, and that staging is what the exe embeds:

- **Minimal** (default): stages only `loader/index.html`, a spinner page. The ~9MB engine is
  not bundled. On first launch the signed updater (`src-tauri/src/updater.rs`) fetches the engine
  into the host cache, and the shell reloads the window onto the cached engine. The `opents://`
  server resolves cache before bundle, so after the reload the cached engine's `index.html` wins
  over the bundled loader. If the first-launch fetch fails, the loader polls the `module_status`
  command and shows the error instead of spinning. A warm cache skips the loader entirely.
- **Offline** (`OPENTS_OFFLINE=1`): stages the full engine page and modules, so the exe runs with
  nothing to download.

The data server defaults to `https://play-ts.net/`, persisted per machine; `get_server` /
`set_server` read and write it, taking effect on the next launch.

| Path | What it is |
| --- | --- |
| `src-tauri/src/lib.rs` | Window, state bridge, settings, fullscreen, quit, cache-clear. |
| `src-tauri/tauri.conf.json` | Frontend directory, `withGlobalTauri`, the CSP. |
| `src-tauri/capabilities/default.json` | The main window's capability. |
| `scripts/copy-web.mjs` | Stages the minimal loader or the full engine into `src-tauri/web/`. |
| `loader/index.html` | The minimal build's spinner page; polls `module_status` and awaits the reload. |
| `src-tauri/src/updater.rs` | Signed updater; on a cold minimal start it reloads onto the fetched engine. |
| `src-tauri/installer/DISTRIBUTION-NOTICE.rtf` | Third-party distribution disclaimer the NSIS and WiX installers show. |
| `docker/Dockerfile` | Linux image `build.sh --appimage` builds and runs the AppImage bundler in. |
| `docker/build-appimage.sh` | That image's entry point; runs `tauri build` and copies the AppImage out. |

## Build

- Rust (`rustup`), Node 18+, and Tauri v2's system dependencies (WebView2 on Windows; Xcode
  command-line tools on macOS).
- A WebAssembly engine built twice — `build-wasm/bin` (JSPI) and `build-wasm-asyncify/bin`
  (Asyncify) — with `-DOPENTS_WASM_NODERAWFS=OFF`, `-DOPENTS_MOVIE_FORMAT=MP4`, and
  `-sEXPORTED_RUNTIME_METHODS=FS,callMain`. WebView2 picks JSPI, WebKit picks Asyncify.
  `MP4` matches play-ts.net's release; a VQA engine finds no `MOVIES*.MIX` there and stops on
  "Failed to initialize".

A WebAssembly engine build is required only for the offline variant and for `npm run dev`; the
minimal build stages just the loader.

```sh
cd tauri
npm install
npm run dev                                 # stages web artifacts, launches the shell
OPENTS_WEB_DIRS="a/bin:b/bin" npm run dev   # override artifact sources (`;`-separated on Windows)
npx tauri icon src-tauri/icons/icon.png     # once, before a release build
npm run build                               # minimal: exe embeds only the loader, engine fetched on first launch
OPENTS_OFFLINE=1 npm run build              # offline: exe embeds the full engine
```

The Windows installers state that this is an unofficial third-party distribution of OpenTS
(`opents.net`) and not affiliated with the project: `bundle.publisher` and the descriptions in
`tauri.conf.json` carry it into Add/Remove Programs, and both bundlers show
`src-tauri/installer/DISTRIBUTION-NOTICE.rtf` during install.

### Signed macOS builds

`./build.sh` reads the signing and notarisation values from `tauri/.env`, which is not
committed; `.env.example` lists the names. It passes its arguments on to `tauri build`, so
`OPENTS_OFFLINE=1 ./build.sh` and `./build.sh --target …` work the same way.

```sh
cp .env.example .env                        # then fill in the identity and credentials
./build.sh
```

It refuses to build without `APPLE_SIGNING_IDENTITY`, because `tauri build` otherwise
leaves the bundle with the linker's ad-hoc signature and reports nothing: it runs on the
machine that built it and Gatekeeper refuses it anywhere else. Notarisation only runs for a
signed bundle, so a missing identity costs both.

`tauri build` notarises and staples the `.app`, then wraps the stapled copy in a disk image
that it signs but never submits. `build.sh` submits and staples that image afterwards, so a
machine opening it offline does not have to ask Apple about it. Builds are arm64 only.

### Linux AppImage

Tauri's AppImage bundler runs on Linux only, so `./build.sh --appimage` builds it in a
container instead. `docker/Dockerfile` pins Rust and the Tauri CLI on Debian bookworm, the
oldest Debian carrying `webkit2gtk-4.1`, and adds the AppImage tooling. The flag is consumed
by `build.sh`; the remaining arguments still reach `tauri build`, and the macOS signing and
notarisation steps are skipped.

```sh
./build.sh --appimage
OPENTS_APPIMAGE_PLATFORM=linux/arm64 ./build.sh --appimage   # host-native on an arm64 machine
```

The repository root is mounted, so `OPENTS_OFFLINE=1` still finds the `build-wasm` directories.
Cargo works in the `opents-appimage-target` volume rather than in the mounted `src-tauri/target`,
where rustc intermittently fails to see a dependency it has just written; the finished AppImage
is copied back to `src-tauri/target/<triple>/release/bundle/appimage/`, beside the host's own
builds rather than over them. The crate registry is cached in `opents-appimage-cargo` and the
tools tauri downloads for the bundler in `opents-appimage-cache`. Nothing signs the AppImage.

| Variable | What it does |
| --- | --- |
| `OPENTS_APPIMAGE_PLATFORM` | Container platform, and with it the AppImage's architecture. Defaults to `linux/amd64`. |
| `OPENTS_APPIMAGE_IMAGE` | Image tag to build and run. Defaults to `opents-appimage`. |

An arm64 machine builds the default `linux/amd64` container under emulation, which produces the
executable but then fails in `linuxdeploy`: an AppImage carries its own magic in the ELF header's
ABI version byte, and the emulator refuses to exec it. `linux/arm64` builds natively on such a
machine and finishes; an amd64 AppImage needs an amd64 host.

## Limitations

The CSP in `tauri.conf.json` is wide (`connect-src https: http:` for a user-set server,
`'unsafe-eval'` for the Emscripten glue); narrow it before shipping. No Apple-shell parity yet:
no settings panel or menu, no native loading overlay, and logging is `eprintln!` rather than
`tauri-plugin-log`.
