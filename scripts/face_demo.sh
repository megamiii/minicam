#!/usr/bin/env bash
# Reproduces the face detection demo in the README on two public-domain NASA
# crew portraits (download them first with scripts/download_faces.sh).
#
#   1. Camera simulation: each photo becomes a synthetic RAW frame with sensor
#      noise, then runs through the full ISP.
#   2. Template: the inner face (eyebrows to chin, no hair) of the front-left
#      STS-40 astronaut, cut from the ISP output and reduced 5x -> 48x48.
#   3. Detection on both photos over a pyramid of downscale factors 4..12,
#      default NCC threshold (0.49).
#
#   scripts/face_demo.sh [build dir] [output dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="${2:-$ROOT/out/faces}"
FACES="$ROOT/data/faces"
mkdir -p "$OUT"

for name in sts40_crew artemis2_crew; do
  "$BUILD/make_synthetic_raw" "$FACES/$name.jpg" "$OUT/$name.raw" \
    --shot-noise 0.001 --read-noise 0.003 --seed 1
done
"$BUILD/minicam" --input "$OUT/sts40_crew.raw" --output "$OUT/sts40_crew_isp.png"

# Inner-face box of the front-left astronaut, in ISP output pixels.
"$BUILD/make_face_template" "$OUT/sts40_crew_isp.png" "$OUT/template.png" \
  544 1482 240 240 5

for name in sts40_crew artemis2_crew; do
  echo "== $name"
  "$BUILD/minicam" --input "$OUT/$name.raw" --output "$OUT/${name}_faces.png" \
    --detect-faces "$OUT/template.png" --detect-downscale 4,5,6,7,8,9,10,11,12 \
    | grep -E "^face at|face_detect"
done
