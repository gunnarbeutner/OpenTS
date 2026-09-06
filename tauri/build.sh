#!/usr/bin/env bash
#
# Builds the macOS bundle with the signing and notarisation values kept out of the shell
# history. They are read from tauri/.env, which is not committed; .env.example lists the
# names. Anything passed here goes on to "tauri build", so OPENTS_OFFLINE=1 and flags such
# as --target still work.
#
# With --appimage it builds a Linux AppImage, and with --windows an unsigned NSIS
# installer, each in its own container under docker/ and neither reaching the macOS signing
# below. The flags are consumed here; the rest of the arguments still reach "tauri build".
#
# Signing is off unless APPLE_SIGNING_IDENTITY names an identity: tauri leaves the bundle
# with the linker's ad-hoc signature and says nothing about it, which passes on the machine
# that built it and is refused everywhere else. Notarisation in turn only runs for a signed
# bundle, so a missing identity silently costs both.
#
# The disk image is notarised and stapled here afterwards, which tauri does not do for it.

set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

appimage=0
windows=0
publish=1
args=()

for argument in "$@"; do
	if [ "$argument" = "--appimage" ]; then
		appimage=1
	elif [ "$argument" = "--windows" ]; then
		windows=1
	elif [ "$argument" = "--no-publish" ]; then
		publish=0
	else
		args+=("$argument")
	fi
done

set -- ${args[@]+"${args[@]}"}

if [ "$appimage" -eq 1 ] && [ "$windows" -eq 1 ]; then
	echo "Error: --appimage and --windows each build one target; run them separately." >&2
	exit 1
fi

if [ -f .env ]; then
	# Exported rather than sourced into the shell alone, since tauri reads them from the
	# environment of the process it runs.
	set -a
	# shellcheck disable=SC1091
	. ./.env
	set +a
else
	echo "Note: no $here/.env; see .env.example for the names it holds." >&2
fi

# Publishing is part of building rather than a step to remember afterwards: a tree holding
# an installer the manifest does not name is one the page will not offer, and nothing else
# reports that. Each build merges into whatever the tree already holds, so the platforms
# built elsewhere are left alone.
publish_download() {
	local artifact="$1"
	local tree="${OPENTS_DOWNLOADS:-$here/../downloads}"

	if [ "$publish" -eq 0 ]; then
		echo "Not published: $(basename "$artifact")"
		return 0
	fi

	if [ ! -d "$tree" ]; then
		echo "Note: $tree does not exist, so $(basename "$artifact") was not published." >&2
		return 0
	fi

	python3 "$here/../tools/downloads/publish.py" --tree "$tree" "$artifact"
}


# The newest file matching a pattern, since a build directory keeps what earlier runs left.
newest_matching() {
	local found=""

	for candidate in "$@"; do
		if [ -f "$candidate" ] && { [ -z "$found" ] || [ "$candidate" -nt "$found" ]; }; then
			found="$candidate"
		fi
	done

	printf '%s' "$found"
}


if [ "$windows" -eq 1 ]; then
	if ! command -v docker >/dev/null 2>&1; then
		echo "Error: docker was not found. The Windows installer is cross-built in one." >&2
		exit 1
	fi

	image="${OPENTS_WINDOWS_IMAGE:-opents-windows}"

	# Native for the host: cargo-xwin links for Windows from whatever it runs on, so
	# nothing here has to be emulated.
	echo "Building the Windows installer in $image; it is not signed and Windows will"
	echo "warn about it. macOS signing does not apply."

	docker build --tag "$image" --file "$here/docker/Dockerfile.windows" "$here/docker"

	docker run --rm \
		--volume "$(cd -- "$here/.." && pwd):/work" \
		--volume opents-windows-cargo:/usr/local/cargo/registry \
		--volume opents-windows-target:/target \
		--volume opents-windows-xwin:/root/.cache/cargo-xwin \
		--workdir /work/tauri \
		--env "OPENTS_OFFLINE=${OPENTS_OFFLINE:-}" \
		"$image" "$@"

	installer="$(newest_matching src-tauri/target/*/release/bundle/nsis/*.exe)"

	if [ -n "$installer" ]; then
		publish_download "$installer"
	else
		echo "Note: the container produced no installer to publish." >&2
	fi

	exit 0
fi

if [ "$appimage" -eq 1 ]; then
	if ! command -v docker >/dev/null 2>&1; then
		echo "Error: docker was not found. The AppImage bundler only runs on Linux." >&2
		exit 1
	fi

	image="${OPENTS_APPIMAGE_IMAGE:-opents-appimage}"

	# amd64 unless asked otherwise, because the AppImage takes the container's architecture
	# and an arm64 host would otherwise quietly produce an arm64 one.
	platform="${OPENTS_APPIMAGE_PLATFORM:-linux/amd64}"

	echo "Building the AppImage in $image ($platform); macOS signing does not apply."

	docker build --platform "$platform" --tag "$image" "$here/docker"

	# The repository root rather than tauri/, so an OPENTS_OFFLINE build still reaches the
	# build-wasm directories copy-web.mjs stages from. The named volumes hold the crate
	# registry, the Linux target directory and the AppImage tools tauri downloads, all of
	# which otherwise start empty on every run.
	docker run --rm --platform "$platform" \
		--volume "$(cd -- "$here/.." && pwd):/work" \
		--volume opents-appimage-cargo:/usr/local/cargo/registry \
		--volume opents-appimage-target:/target \
		--volume opents-appimage-cache:/root/.cache/tauri \
		--workdir /work/tauri \
		--env "OPENTS_OFFLINE=${OPENTS_OFFLINE:-}" \
		"$image" "$@"

	bundle="$(newest_matching src-tauri/target/*/release/bundle/appimage/*.AppImage)"

	if [ -n "$bundle" ]; then
		publish_download "$bundle"
	else
		echo "Note: the container produced no AppImage to publish." >&2
	fi

	exit 0
fi

# Homebrew's rustup keeps its shims in its own prefix rather than ~/.cargo/bin, so a shell
# that has not been told about it fails in "cargo metadata" long after the build starts.
if ! command -v cargo >/dev/null 2>&1; then
	for prefix in "$(brew --prefix rustup 2>/dev/null || true)" "$HOME/.cargo"; do
		if [ -n "$prefix" ] && [ -x "$prefix/bin/cargo" ]; then
			PATH="$prefix/bin:$PATH"
			export PATH
			break
		fi
	done
fi

if ! command -v cargo >/dev/null 2>&1; then
	echo "Error: cargo was not found. Install a Rust toolchain (rustup default stable)." >&2
	exit 1
fi

if [ -z "${APPLE_SIGNING_IDENTITY:-}" ]; then
	echo "Error: APPLE_SIGNING_IDENTITY is not set, so the bundle would be unsigned." >&2
	echo "       Set it in .env; 'security find-identity -v -p codesigning' lists the names." >&2
	exit 1
fi

if ! security find-identity -v -p codesigning | grep -qF -- "$APPLE_SIGNING_IDENTITY"; then
	echo "Error: no codesigning identity matches '$APPLE_SIGNING_IDENTITY'." >&2
	security find-identity -v -p codesigning >&2
	exit 1
fi

# Three ways to authenticate, in the order notarytool is asked to use them. A keychain
# profile is the one that keeps the password out of the process arguments.
notarising=0

if [ -n "${APPLE_KEYCHAIN_PROFILE:-}" ]; then
	notarising=1
elif [ -n "${APPLE_API_KEY:-}" ] && [ -n "${APPLE_API_ISSUER:-}" ] && [ -n "${APPLE_API_KEY_PATH:-}" ]; then
	notarising=1
elif [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_PASSWORD:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ]; then
	notarising=1
fi

echo "Signing as: $APPLE_SIGNING_IDENTITY"

if [ "$notarising" -eq 1 ]; then
	echo "Notarising: yes -- this uploads the bundle to Apple and waits for the ticket."
else
	echo "Notarising: no -- Gatekeeper will refuse the result on other machines." >&2
	echo "            Set APPLE_ID, APPLE_PASSWORD and APPLE_TEAM_ID to notarise." >&2
fi

npm run build -- "$@"

if [ "$notarising" -eq 0 ]; then
	unnotarised="$(newest_matching src-tauri/target/release/bundle/dmg/*.dmg \
		src-tauri/target/*/release/bundle/dmg/*.dmg)"
	if [ -n "$unnotarised" ]; then publish_download "$unnotarised"; fi
	exit 0
fi

# tauri notarises and staples the .app and then wraps the stapled copy, so the disk image
# it hands out carries no ticket of its own. Gatekeeper falls back to asking Apple about
# an unstapled image, which is a check that fails on a machine opening it offline.
image=""

for candidate in \
	src-tauri/target/release/bundle/dmg/*.dmg \
	src-tauri/target/*/release/bundle/dmg/*.dmg; do
	if [ -f "$candidate" ] && { [ -z "$image" ] || [ "$candidate" -nt "$image" ]; }; then
		image="$candidate"
	fi
done

if [ -z "$image" ]; then
	echo "Note: no disk image was produced, so there is nothing to staple." >&2
	exit 0
fi

if xcrun stapler validate "$image" >/dev/null 2>&1; then
	echo "Already stapled: $image"
	exit 0
fi

echo "Notarising $image -- this uploads it and waits for Apple to answer."

if [ -n "${APPLE_KEYCHAIN_PROFILE:-}" ]; then
	xcrun notarytool submit "$image" --keychain-profile "$APPLE_KEYCHAIN_PROFILE" --wait
elif [ -n "${APPLE_API_KEY:-}" ]; then
	xcrun notarytool submit "$image" --key "$APPLE_API_KEY_PATH" \
		--key-id "$APPLE_API_KEY" --issuer "$APPLE_API_ISSUER" --wait
else
	xcrun notarytool submit "$image" --apple-id "$APPLE_ID" \
		--team-id "$APPLE_TEAM_ID" --password "$APPLE_PASSWORD" --wait
fi

xcrun stapler staple "$image"
xcrun stapler validate "$image"
publish_download "$image"
