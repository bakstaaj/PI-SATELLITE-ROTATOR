#!/usr/bin/env bash
set -euo pipefail

# validate_pi_rotator_hardening_v1.sh
#
# PASS/FAIL validator for the PI-SATELLITE-ROTATOR hardening-status-feedback patch.
# Run from the repository root in MSYS2 UCRT64:
#
#   cd ~/sdrdev/PI-SATELLITE-ROTATOR
#   ./tools/validate_pi_rotator_hardening_v1.sh
#
# This script does not use PowerShell, cmd.exe, WSL, or git diff.
# It prints one PASS/FAIL line per validation and exits non-zero on failure.

FAILURES=0

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}

check_file_contains() {
  local file="$1"
  local text="$2"
  local description="$3"

  if [[ ! -f "$file" ]]; then
    fail "$description - missing file: $file"
    return
  fi

  if python3 - "$file" "$text" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
needle = sys.argv[2]
try:
    data = path.read_text(encoding="utf-8")
except Exception as exc:
    print(exc, file=sys.stderr)
    sys.exit(2)
sys.exit(0 if needle in data else 1)
PY
  then
    pass "$description"
  else
    fail "$description"
  fi
}

echo "PI-SATELLITE-ROTATOR hardening v1 validation"
echo "================================================"

if [[ -f "CMakeLists.txt" && -d "src" && -d "include" && -d "scripts" ]]; then
  pass "running from repository root"
else
  fail "run this from ~/sdrdev/PI-SATELLITE-ROTATOR"
fi

branch="$(git branch --show-current 2>/dev/null || true)"
if [[ "$branch" == "hardening-status-feedback" ]]; then
  pass "on feature branch hardening-status-feedback"
else
  fail "current branch is '$branch' not 'hardening-status-feedback'"
fi

check_file_contains "include/rotator/rotator.hpp" "struct ControllerStatus" "ControllerStatus type exists"
check_file_contains "include/rotator/rotator.hpp" "feedback_timeout_" "controller stores feedback timeout"
check_file_contains "src/rotator.cpp" "feedback_stale_locked" "stale feedback detection exists"
check_file_contains "src/rotator.cpp" "external feedback has not produced a valid sample" "motion is rejected before first live feedback sample"
check_file_contains "src/rotator.cpp" "external feedback is stale" "motion is rejected when feedback is stale"
check_file_contains "include/rotator/easycomm.hpp" "status" "EasyComm status command kind exists"
check_file_contains "src/easycomm.cpp" "STATUS" "EasyComm STATUS parser exists"
check_file_contains "src/tcp_server.cpp" "format_status_json" "TCP server returns JSON status"
check_file_contains "src/tcp_server.cpp" "OK MOVE" "move command returns explicit OK"
check_file_contains "src/tcp_server.cpp" "OK STOP" "stop command returns explicit OK"
check_file_contains "src/web_server.cpp" "/api/status" "web status endpoint exists"
check_file_contains "src/web_server.cpp" "SO_RCVTIMEO" "web request receive timeout exists"
check_file_contains "src/main.cpp" "--feedback-timeout-ms" "feedback timeout command-line option exists"
check_file_contains "tests/test_easycomm.cpp" "STATUS" "EasyComm/controller tests cover STATUS"
check_file_contains "tests/test_web.cpp" "feedback_stale" "web tests cover status JSON fields"
check_file_contains "docs/web-control.md" "--feedback-timeout-ms" "web control docs mention feedback timeout"

echo
echo "Running Docker/native tests..."
if ./scripts/test.sh; then
  pass "Docker native CMake tests"
else
  fail "Docker native CMake tests"
fi

echo
echo "Running Docker ARM64 build..."
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
  fail "pi-satellite-rotator artifact is not ARM aarch64"
fi

if file dist/witmotion-tool 2>/dev/null | python3 -c 'import sys; s=sys.stdin.read(); sys.exit(0 if "ARM aarch64" in s else 1)'; then
  pass "witmotion-tool artifact is ARM aarch64"
else
  fail "witmotion-tool artifact is not ARM aarch64"
fi

echo
echo "Checking git state..."
if git status --porcelain 2>/dev/null | python3 -c 'import sys; data=sys.stdin.read(); sys.exit(0 if data.strip() else 1)'; then
  pass "repository has changes ready to review/commit"
else
  fail "repository has no working-tree changes; patch may not be applied or may already be committed"
fi

echo
echo "================================================"
if [[ "$FAILURES" -eq 0 ]]; then
  echo "FINAL RESULT: PASS"
  exit 0
else
  echo "FINAL RESULT: FAIL ($FAILURES failure(s))"
  exit 1
fi
