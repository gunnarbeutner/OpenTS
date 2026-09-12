#!/usr/bin/env bash
#
# Container entry point for the Windows installer. The repository is mounted at /work and
# the finished installer is copied back into it; everything else here is container-local.

set -euo pipefail

target=x86_64-pc-windows-msvc

# A container-local target directory, for the same reason the AppImage build uses one: the
# bind mount intermittently loses a dependency's freshly written rlib.
export CARGO_TARGET_DIR=/target

# Acceptance of Microsoft's terms for the CRT and SDK headers cargo-xwin downloads.
export XWIN_ACCEPT_LICENSE=1

echo "Building $target installer"

tauri build --runner cargo-xwin --target "$target" --bundles nsis "$@"

bundle="$CARGO_TARGET_DIR/$target/release/bundle/nsis"
destination="/work/tauri/src-tauri/target/$target/release/bundle/nsis"

mkdir -p "$destination"
cp -f "$bundle"/*.exe "$destination/"

for installer in "$destination"/*.exe; do
	echo "Wrote tauri/src-tauri/target/$target/release/bundle/nsis/$(basename "$installer")"
done
