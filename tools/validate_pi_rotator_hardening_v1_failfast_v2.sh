#!/usr/bin/env bash
set -euo pipefail

# validate_pi_rotator_hardening_v1_failfast_v2.sh
#
# Fail-fast PASS/FAIL validator for PI-SATELLITE-ROTATOR hardening-status-feedback.
# This v2 validator checks control logic markers instead of brittle exact error text.
#
# Run from repo root:
#
#   cd ~/sdrdev/PI-SATELLITE-ROTATOR
#   ./tools/validate_pi_rotator_hardening_v1_failfast_v2.sh
#
# Behavior:
# - Prints PASS for each completed check.
# - Prints FAIL for the first failed check.
# - Exits immediately on the first failure.
# - Does not run Docker builds unless all source/content checks pass.
# - Does not use PowerShell, cmd.exe, WSL, or git diff.

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  echo "FINAL RESULT: FAIL" >&2
  exit 1
}

require_command() {
  local command_name="$1"
  local description="$2"

  if command -v "$command_name" >/dev/null 2>&1; then
    pass "$description"
  else
    fail "$description - missing command: $command_name"
  fi
}

require_file() {
  local file="$1"
  local description="$2"

  if [[ -f "$file" ]]; then
    pass "$description"
  else
    fail "$description - missing file: $file"
  fi
}

require_file_contains_any() {
  local file="$1"
  local description="$2"
  shift 2
  local needles=("$@")

  require_file "$file" "source file present: $file" >/dev/null

  if python3 - "$file" "${needles[@]}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
needles = sys.argv[2:]
data = path.read_text(encoding="utf-8")
sys.exit(0 if any(needle in data for needle in needles) else 1)
PY
  then
    pass "$description"
  else
    printf 'FAIL: %s\n' "$description" >&2
    printf '      file: %s\n' "$file" >&2
    printf '      expected one of these source markers:\n' >&2
    for needle in "${needles[@]}"; do
      printf '        - %s\n' "$needle" >&2
    done
    echo "FINAL RESULT: FAIL" >&2
    exit 1
  fi
}

require_file_contains_all() {
  local file="$1"
  local description="$2"
  shift 2
  local needles=("$@")

  require_file "$file" "source file present: $file" >/dev/null

  if python3 - "$file" "${needles[@]}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
needles = sys.argv[2:]
data = path.read_text(encoding="utf-8")
missing = [needle for needle in needles if needle not in data]
if missing:
    for needle in missing:
        print(needle)
    sys.exit(1)
sys.exit(0)
PY
  then
    pass "$description"
  else
    printf 'FAIL: %s\n' "$description" >&2
    printf '      file: %s\n' "$file" >&2
    printf '      expected all listed source markers.\n' >&2
    echo "FINAL RESULT: FAIL" >&2
    exit 1
  fi
}

echo "PI-SATELLITE-ROTATOR hardening v1 fail-fast validation v2"
echo "========================================================="

require_command git "git command available"
require_command python3 "python3 command available"
require_command file "file command available"

if [[ -f "CMakeLists.txt" && -d "src" && -d "include" && -d "scripts" ]]; then
  pass "running from PI-SATELLITE-ROTATOR repository root"
else
  fail "run this from ~/sdrdev/PI-SATELLITE-ROTATOR"
fi

branch="$(git branch --show-current 2>/dev/null || true)"
if [[ "$branch" == "hardening-status-feedback" ]]; then
  pass "on feature branch hardening-status-feedback"
else
  fail "current branch is '$branch' not 'hardening-status-feedback'"
fi

require_file_contains_any "include/rotator/rotator.hpp"   "ControllerStatus type exists"   "struct ControllerStatus"

require_file_contains_any "include/rotator/rotator.hpp"   "controller status API exists"   "ControllerStatus status() const;"

require_file_contains_any "include/rotator/rotator.hpp"   "controller stores feedback timeout"   "feedback_timeout_"

require_file_contains_any "src/rotator.cpp"   "stale feedback helper exists"   "feedback_stale_locked"

require_file_contains_all "src/rotator.cpp"   "motion is rejected before first live feedback sample"   "external_feedback_"   "!feedback_received_"   "return false;"

require_file_contains_all "src/rotator.cpp"   "motion is rejected when feedback is stale"   "feedback_stale_locked(now)"   "return false;"

require_file_contains_any "include/rotator/easycomm.hpp"   "EasyComm status command kind exists"   "status"

require_file_contains_any "src/easycomm.cpp"   "EasyComm STATUS parser exists"   "STATUS"

require_file_contains_any "src/tcp_server.cpp"   "TCP server returns JSON status"   "format_status_json"

require_file_contains_any "src/tcp_server.cpp"   "move command returns explicit OK"   "OK MOVE"

require_file_contains_any "src/tcp_server.cpp"   "stop command returns explicit OK"   "OK STOP"

require_file_contains_any "src/web_server.cpp"   "web status endpoint exists"   "/api/status"

require_file_contains_any "src/web_server.cpp"   "web proxies EasyComm STATUS command"   "STATUS\n"

require_file_contains_any "src/web_server.cpp"   "web request receive timeout exists"   "SO_RCVTIMEO"

require_file_contains_any "src/main.cpp"   "feedback timeout command-line option exists"   "--feedback-timeout-ms"

require_file_contains_any "tests/test_easycomm.cpp"   "EasyComm/controller tests cover STATUS"   "STATUS"

require_file_contains_any "tests/test_easycomm.cpp"   "controller tests cover stale feedback"   "feedback_stale"

require_file_contains_any "tests/test_web.cpp"   "web tests cover status JSON fields"   "feedback_stale"

require_file_contains_any "docs/web-control.md"   "web control docs mention feedback timeout"   "--feedback-timeout-ms"

echo
echo "Source checks passed. Running Docker/native tests..."
if ./scripts/test.sh; then
  pass "Docker native CMake tests"
else
  fail "Docker native CMake tests"
fi

echo
echo "Docker/native tests passed. Running Docker ARM64 build..."
if ./scripts/build-rpi.sh; then
  pass "Docker ARM64 build"
else
  fail "Docker ARM64 build"
fi

echo
echo "Checking ARM64 artifacts..."
if [[ -f dist/pi-satellite-rotator ]]; then
  pass "dist/pi-satellite-rotator exists"
else
  fail "dist/pi-satellite-rotator exists"
fi

if [[ -f dist/witmotion-tool ]]; then
  pass "dist/witmotion-tool exists"
else
  fail "dist/witmotion-tool exists"
fi

if file dist/pi-satellite-rotator 2>/dev/null | python3 -c 'import sys; s=sys.stdin.read(); sys.exit(0 if "ARM aarch64" in s else 1)'; then
  pass "pi-satellite-rotator artifact is ARM aarch64"
else
  fail "pi-satellite-rotator artifact is ARM aarch64"
fi

if file dist/witmotion-tool 2>/dev/null | python3 -c 'import sys; s=sys.stdin.read(); sys.exit(0 if "ARM aarch64" in s else 1)'; then
  pass "witmotion-tool artifact is ARM aarch64"
else
  fail "witmotion-tool artifact is ARM aarch64"
fi

echo
echo "Checking git working tree state..."
if git status --porcelain 2>/dev/null | python3 -c 'import sys; data=sys.stdin.read(); sys.exit(0 if data.strip() else 1)'; then
  pass "repository has working-tree changes ready to commit"
else
  fail "repository has no working-tree changes; patch may be unapplied, already committed, or applied on the wrong branch"
fi

echo "========================================================="
echo "FINAL RESULT: PASS"
