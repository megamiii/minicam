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
  1. Open https://r0k.us/graphics/kodak/ in a browser.
  2. Save kodim01.png ... kodim24.png into $DEST
  3. Re-run this script to check that all 24 are present.
EOF
    exit 1
  fi
  mv "$DEST/$name.part" "$DEST/$name"
done
echo "done: $(ls "$DEST"/*.png | wc -l) images in $DEST"
