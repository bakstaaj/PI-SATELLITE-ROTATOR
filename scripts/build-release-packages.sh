#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/docker-common.sh

RUN_TESTS="${RUN_TESTS:-1}"
VERSION="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"
VERSION_SAFE="$(printf '%s' "$VERSION" | tr -c 'A-Za-z0-9._-' '-')"

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\nFINAL RESULT: FAIL\n' "$1" >&2; exit 1; }

if [[ "$RUN_TESTS" == "1" ]]; then
  ./scripts/test.sh
  pass "native Docker tests passed"
else
  pass "native Docker tests skipped by RUN_TESTS=$RUN_TESTS"
fi

./scripts/build-rpi64.sh
./scripts/build-rpi-zero-32.sh
pass "both cross builds completed"

file dist/rpi64/pi-satellite-rotator | grep -Eq 'ELF 64-bit.*ARM aarch64|ARM aarch64' || fail "rpi64 binary is not aarch64"
file dist/rpi-zero-32/pi-satellite-rotator | grep -Eq 'ELF 32-bit.*ARM' || fail "rpi-zero-32 binary is not 32-bit ARM"
pass "binary architecture checks passed"

release_root="dist/releases"
rm -rf "$release_root/stage"
mkdir -p "$release_root/stage" "$release_root"

make_package() {
  local package_id="$1"
  local package_arch="$2"
  local source_dir="$3"
  local expected_uname_regex="$4"
  local expected_file_regex="$5"
  local package_dir="$release_root/stage/pi-satellite-rotator-${VERSION_SAFE}-${package_id}"
  local archive="$release_root/pi-satellite-rotator-${VERSION_SAFE}-${package_id}.tar.gz"

  rm -rf "$package_dir"
  mkdir -p "$package_dir/bin"

  install -m 0755 "$source_dir/pi-satellite-rotator" "$package_dir/bin/pi-satellite-rotator"
  install -m 0755 "$source_dir/witmotion-tool" "$package_dir/bin/witmotion-tool"

  cp -R web "$package_dir/web"
  cp -R config "$package_dir/config"
  cp -R packaging "$package_dir/packaging"
  cp -R docs "$package_dir/docs"
  [[ -f README.md ]] && cp README.md "$package_dir/README.md"

  PACKAGE_ARCH="$package_arch" \
  EXPECTED_UNAME_REGEX="$expected_uname_regex" \
  EXPECTED_FILE_REGEX="$expected_file_regex" \
  python3 - "$package_dir/install.sh" <<'PYTEMPLATE'
from pathlib import Path
import os
import sys

template = Path("packaging/install-release-template.sh").read_text(encoding="utf-8")
template = template.replace("%PACKAGE_ARCH%", os.environ["PACKAGE_ARCH"])
template = template.replace("%EXPECTED_UNAME_REGEX%", os.environ["EXPECTED_UNAME_REGEX"])
template = template.replace("%EXPECTED_FILE_REGEX%", os.environ["EXPECTED_FILE_REGEX"])
Path(sys.argv[1]).write_text(template, encoding="utf-8", newline="\n")
PYTEMPLATE
  chmod +x "$package_dir/install.sh"

  cat > "$package_dir/RELEASE_MANIFEST.txt" <<EOF
Package: pi-satellite-rotator
Version: $VERSION
Package ID: $package_id
Architecture: $package_arch
Built from commit: $(git rev-parse HEAD)
Built at: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Install:
  sudo ./install.sh --start

Safety:
  Keep PI_SATELLITE_ROTATOR_ARGS configured with --motor-backend simulator
  until mechanical assembly, limit switches, and zeroing have been validated.
EOF

  tar -C "$release_root/stage" -czf "$archive" "$(basename "$package_dir")"
  sha256sum "$archive" > "$archive.sha256"
  pass "created release package: $archive"
}

make_package \
  "rpi64-aarch64" \
  "Raspberry Pi 64-bit aarch64" \
  "dist/rpi64" \
  "^(aarch64|arm64)$" \
  "ELF 64-bit.*(ARM aarch64|aarch64)"

make_package \
  "rpi-zero-32-armv6" \
  "Raspberry Pi Zero W 32-bit ARMv6 hard-float" \
  "dist/rpi-zero-32" \
  "^(armv6l|armv7l)$" \
  "ELF 32-bit.*ARM"

cat > "$release_root/RELEASE_FILES.txt" <<EOF
Release package files for $VERSION:

$(ls -1 "$release_root"/*.tar.gz "$release_root"/*.sha256 2>/dev/null)

Upload with:
  scripts/upload-release-packages.sh <tag>
EOF

cat "$release_root/RELEASE_FILES.txt"
echo "FINAL RESULT: PASS"
