#!/usr/bin/env bash
# Downloads the two public-domain NASA crew portraits used for the face
# detection demo into data/faces/ (git-ignored):
#   sts40_crew.jpg     STS-40 crew portrait (NASA), 4039x3231, ~2.1 MB
#                      https://commons.wikimedia.org/wiki/File:STS-40_crew.jpg
#   artemis2_crew.jpg  Artemis II crew portrait (NASA/Josh Valcarcel),
#                      5878x7348, ~5.4 MB
#                      https://commons.wikimedia.org/wiki/File:Artemis_2_Crew_Portrait.jpg
# Both are public domain as works of NASA ("NASA material is not protected by
# copyright unless noted").
set -euo pipefail

DEST="$(cd "$(dirname "$0")/.." && pwd)/data/faces"
mkdir -p "$DEST"
# Wikimedia asks automated clients to identify themselves.
AGENT="minicam-demo/1.0 (https://github.com/megamiii/minicam)"

fetch() {
  local url="$1" name="$2"
  if [[ -s "$DEST/$name" ]]; then
    echo "skip $name (already present)"
    return
  fi
  echo "download $name"
  curl --fail --silent --show-error --location -A "$AGENT" \
    -o "$DEST/$name.part" "$url"
  mv "$DEST/$name.part" "$DEST/$name"
}

fetch "https://upload.wikimedia.org/wikipedia/commons/8/85/STS-40_crew.jpg" \
  sts40_crew.jpg
fetch "https://upload.wikimedia.org/wikipedia/commons/e/e5/Artemis_2_Crew_Portrait.jpg" \
  artemis2_crew.jpg
ls -l "$DEST"
