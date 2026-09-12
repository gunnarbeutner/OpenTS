#!/usr/bin/env bash
#
# Container entry point for the AppImage build. The repository is mounted at /work and the
# finished AppImage is copied back into it; everything else here is container-local.

set -euo pipefail

# Naming the target keeps the Linux artifacts in their own subdirectory rather than in
# target/release, which the host's own builds use.
target="$(rustc -vV | sed -n 's/^host: //p')"

# A container-local target directory, because rustc intermittently fails to see a
# dependency's freshly written rlib on the bind mount ("can't find crate for `zerofrom`").
export CARGO_TARGET_DIR=/target

# linuxdeploy and appimagetool are AppImages themselves, and mounting one needs a FUSE
# device the container does not have.
export APPIMAGE_EXTRACT_AND_RUN=1

echo "Building $target AppImage"

tauri build --target "$target" --bundles appimage "$@"

bundle="$CARGO_TARGET_DIR/$target/release/bundle/appimage"
destination="/work/tauri/src-tauri/target/$target/release/bundle/appimage"

mkdir -p "$destination"
cp -f "$bundle"/*.AppImage "$destination/"

for image in "$destination"/*.AppImage; do
	echo "Wrote tauri/src-tauri/target/$target/release/bundle/appimage/$(basename "$image")"
done
