#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

tag="${1:-${RELEASE_TAG:-}}"
[[ -n "$tag" ]] || { echo "Usage: scripts/upload-release-packages.sh <tag>" >&2; exit 1; }

command -v gh >/dev/null 2>&1 || { echo "gh CLI is required" >&2; exit 1; }

shopt -s nullglob
artifacts=(dist/releases/*.tar.gz dist/releases/*.sha256)
[[ ${#artifacts[@]} -gt 0 ]] || { echo "No release artifacts found. Run scripts/build-release-packages.sh first." >&2; exit 1; }

notes_file="dist/releases/RELEASE_FILES.txt"
if [[ ! -f "$notes_file" ]]; then
  notes_file="$(mktemp)"
  printf 'PI-SATELLITE-ROTATOR release packages for %s\n' "$tag" > "$notes_file"
fi

if gh release view "$tag" >/dev/null 2>&1; then
  gh release upload "$tag" "${artifacts[@]}" --clobber
else
  gh release create "$tag" "${artifacts[@]}" --title "$tag" --notes-file "$notes_file"
fi

echo "FINAL RESULT: PASS"
echo "RELEASE_TAG=$tag"
echo "UPLOADED_ARTIFACTS=${#artifacts[@]}"
