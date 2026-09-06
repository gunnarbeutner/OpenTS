#!/usr/bin/env bash
#
# Rebuilds the two icon files the page serves at its root. They are committed, so this only
# has to run when the mark itself changes.
#
# Both are rasterised from opents-flat.svg rather than opents.svg, whose gradients need a
# librsvg delegate that ImageMagick does not always have; the flat mark renders the same
# everywhere, and neither of these is ever drawn large enough for the gradients to show.

set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mark="$here/../code/resources/app-icon/opents-flat.svg"

if ! command -v magick >/dev/null 2>&1; then
	echo "Error: ImageMagick 'magick' was not found." >&2
	exit 1
fi

# A browser tab draws the icon against whatever colour it likes, so the mark keeps its own
# shape and no ground.
magick \
	\( -background none "$mark" -resize 48x48 \) \
	\( -background none "$mark" -resize 32x32 \) \
	\( -background none "$mark" -resize 16x16 \) \
	"$here/favicon.ico"

# iOS draws this one onto a rounded tile of its own and reads transparency as black, so it
# carries the page's own background. The mark is inset because the corners it rounds are the
# two the mark reaches into.
magick -background none "$mark" -resize 156x156 \
	-background '#101216' -gravity center -extent 180x180 \
	-alpha remove -alpha off -depth 8 -strip \
	"PNG24:$here/apple-touch-icon.png"

# An installed web app draws its own tile from these, at whatever size the launcher wants.
# Maskable icons are cropped to a circle on some launchers, so the mark keeps the safe inset
# the specification asks for rather than reaching the edge.
for size in 192 512; do
	magick -background none "$mark" -resize "$((size * 5 / 8))x$((size * 5 / 8))" \
		-background '#101216' -gravity center -extent "${size}x${size}" \
		-alpha remove -alpha off -depth 8 -strip \
		"PNG24:$here/icon-${size}.png"
done

echo "Wrote $here/favicon.ico, $here/apple-touch-icon.png and icon-192.png, icon-512.png"
