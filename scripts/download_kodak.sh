#!/usr/bin/env bash
# Downloads the Kodak Lossless True Color Image Suite (24 PNGs, ~15 MB) into
# data/kodak/. These 768x512 photos are the standard benchmark for
# demosaicing quality. Source: https://r0k.us/graphics/kodak/
#
# The images are not part of this repository (data/ is git-ignored).
set -euo pipefail

BASE_URL="https://r0k.us/graphics/kodak/kodak"
DEST="$(cd "$(dirname "$0")/.." && pwd)/data/kodak"
mkdir -p "$DEST"

for i in $(seq -w 1 24); do
  name="kodim${i}.png"
  if [[ -s "$DEST/$name" ]]; then
    # A saved web page renamed to .png would otherwise pass silently.
    if [[ "$(head -c 4 "$DEST/$name" | tail -c 3)" != "PNG" ]]; then
      echo "error: $DEST/$name exists but is not a PNG file" >&2
      exit 1
    fi
    echo "skip $name (already present)"
    continue
  fi
  echo "download $name"
  if ! curl --fail --silent --show-error --location -o "$DEST/$name.part" \
      "$BASE_URL/$name"; then
    rm -f "$DEST/$name.part"
    cat >&2 <<EOF

The server refused the scripted download (it currently only serves browsers).
Download the images manually instead:
  1. In a browser, open each image directly:
       https://r0k.us/graphics/kodak/kodak/kodim01.png ... kodim24.png
     (The links on https://r0k.us/graphics/kodak/ lead to HTML pages, so
     "Save link as" there saves a page, not the image.)
  2. Save each image (Ctrl+S, or right-click > "Save image as") into
       $DEST
  3. Re-run this script to check that all 24 are present.
EOF
    exit 1
  fi
  mv "$DEST/$name.part" "$DEST/$name"
done
echo "done: $(ls "$DEST"/*.png | wc -l) images in $DEST"
