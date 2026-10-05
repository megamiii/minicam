#!/usr/bin/env bash
# Downloads a few bursts of the Google HDR+ Burst Photography Dataset
# (https://hdrplusdata.org/dataset.html) into data/hdrplus/.
#
#   scripts/download_hdrplus.sh                 # first 3 bursts of the subset
#   scripts/download_hdrplus.sh -n 5            # first 5 bursts
#   scripts/download_hdrplus.sh <burst> ...     # specific bursts by name
#   scripts/download_hdrplus.sh --list          # list burst names only
#
# Only part of the curated subset is fetched (the whole subset is 153 bursts,
# about 37 GiB). For each burst this downloads
#   data/hdrplus/<burst>/payload_N*.dng, rgb2rgb.txt, lens_shading_map_N*.tiff
#   data/hdrplus/<burst>/results/final.jpg           Google's final output
#   data/hdrplus/<burst>/results/reference_frame.txt index of the base frame
# Roughly 30-80 MB per burst.
#
# Uses gsutil (Google Cloud SDK) as recommended by the dataset authors. The
# bucket is publicly readable, so without gsutil the script falls back to
# plain HTTPS downloads with curl.
#
# The dataset is released under CC BY-SA 4.0 and must not be committed;
# data/ is listed in .gitignore.
set -euo pipefail

BUCKET="hdrplusdata"
SUBSET="20171106_subset"
RESULTS="results_20171023"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/data/hdrplus"

count=3
list_only=false
bursts=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -n) count="$2"; shift 2 ;;
    --list) list_only=true; shift ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) bursts+=("$1"); shift ;;
  esac
done

have_gsutil=false
if command -v gsutil > /dev/null; then have_gsutil=true; fi

# Prints object names under a prefix, one per line. With a trailing "/" and
# folders=1, prints the immediate sub-folders instead.
gcs_list() {
  local prefix="$1" folders="${2:-0}"
  if $have_gsutil; then
    gsutil ls "gs://$BUCKET/$prefix" | sed "s|^gs://$BUCKET/||" |
      { if [[ "$folders" == 1 ]]; then grep '/$'; else grep -v '/$'; fi; }
  else
    python3 - "$BUCKET" "$prefix" "$folders" <<'PY'
import json, sys, urllib.parse, urllib.request
bucket, prefix, folders = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
token = None
while True:
    query = {"prefix": prefix, "delimiter": "/",
             "fields": "prefixes,items(name),nextPageToken"}
    if token:
        query["pageToken"] = token
    url = (f"https://storage.googleapis.com/storage/v1/b/{bucket}/o?"
           + urllib.parse.urlencode(query))
    with urllib.request.urlopen(url) as response:
        data = json.load(response)
    for name in (data.get("prefixes", []) if folders
                 else [i["name"] for i in data.get("items", [])]):
        print(name)
    token = data.get("nextPageToken")
    if not token:
        break
PY
  fi
}

# Copies one object to a local file, skipping files that are already there.
fetch() {
  local object="$1" target="$2"
  if [[ -s "$target" ]]; then return; fi
  mkdir -p "$(dirname "$target")"
  if $have_gsutil; then
    gsutil -q cp "gs://$BUCKET/$object" "$target"
  else
    curl --fail --silent --show-error --location -o "$target.part" \
      "https://storage.googleapis.com/$BUCKET/$object"
    mv "$target.part" "$target"
  fi
}

if [[ ${#bursts[@]} -eq 0 ]]; then
  mapfile -t bursts < <(gcs_list "$SUBSET/bursts/" 1 | sed 's|.*/bursts/||; s|/$||')
  if $list_only; then printf '%s\n' "${bursts[@]}"; exit 0; fi
  bursts=("${bursts[@]:0:$count}")
fi

echo "Using $($have_gsutil && echo gsutil || echo 'curl (gsutil not found)')"
for burst in "${bursts[@]}"; do
  echo "== $burst"
  mapfile -t objects < <(gcs_list "$SUBSET/bursts/$burst/")
  if [[ ${#objects[@]} -eq 0 ]]; then
    echo "error: no burst named '$burst' in $SUBSET" >&2
    exit 1
  fi
  for object in "${objects[@]}"; do
    fetch "$object" "$DEST/$burst/$(basename "$object")"
  done
  for file in final.jpg reference_frame.txt; do
    fetch "$SUBSET/$RESULTS/$burst/$file" "$DEST/$burst/results/$file"
  done
  echo "   $(ls "$DEST/$burst"/payload_N*.dng | wc -l) frames, $(du -sh "$DEST/$burst" | cut -f1)"
done
echo "Done: data in $DEST"
