#!/usr/bin/env bash
set -euo pipefail

REQUIRE_ACTIVE=0
SELF_CHECK=0

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\nFINAL RESULT: FAIL\n' "$1" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --require-active) REQUIRE_ACTIVE=1 ;;
    --self-check) SELF_CHECK=1 ;;
    *) fail "unknown option: $1" ;;
  esac
  shift
done

if [[ "$SELF_CHECK" -eq 1 ]]; then
  [[ -f packaging/pi-satellite-rotator.service ]] || fail "missing packaging/pi-satellite-rotator.service"
  pass "service template exists"
  [[ -f config/pi-satellite-rotator.env.example ]] || fail "missing config/pi-satellite-rotator.env.example"
  pass "environment example exists"
  echo "FINAL RESULT: PASS"
  exit 0
fi

[[ "$(uname -s)" == "Linux" ]] || fail "validator must run on Linux/Raspberry Pi OS"
pass "Linux host verified"

arch="$(uname -m)"
[[ "$arch" == "aarch64" || "$arch" == "arm64" ]] || fail "expected aarch64 host; detected $arch"
pass "aarch64 host verified"

[[ -x /opt/pi-satellite-rotator/bin/pi-satellite-rotator ]] || fail "missing executable /opt/pi-satellite-rotator/bin/pi-satellite-rotator"
pass "pi-satellite-rotator executable installed"

[[ -x /opt/pi-satellite-rotator/bin/witmotion-tool ]] || fail "missing executable /opt/pi-satellite-rotator/bin/witmotion-tool"
pass "witmotion-tool executable installed"

[[ -f /etc/pi-satellite-rotator/pi-satellite-rotator.env ]] || fail "missing /etc/pi-satellite-rotator/pi-satellite-rotator.env"
pass "environment file installed"

[[ -f /etc/systemd/system/pi-satellite-rotator.service ]] || fail "missing systemd service file"
pass "systemd service file installed"

command -v systemctl >/dev/null 2>&1 || fail "systemctl not available"
pass "systemctl available"

systemctl cat pi-satellite-rotator.service >/dev/null
pass "systemd can load pi-satellite-rotator.service"

if command -v systemd-analyze >/dev/null 2>&1; then
  systemd-analyze verify /etc/systemd/system/pi-satellite-rotator.service
  pass "systemd-analyze verify passed"
else
  pass "systemd-analyze not available; skipped unit verification"
fi

if systemctl is-enabled pi-satellite-rotator.service >/dev/null 2>&1; then
  pass "service is enabled"
else
  fail "service is not enabled"
fi

if systemctl is-active pi-satellite-rotator.service >/dev/null 2>&1; then
  pass "service is active"
else
  if [[ "$REQUIRE_ACTIVE" -eq 1 ]]; then
    fail "service is not active"
  fi
  pass "service is installed but not active; --require-active not requested"
fi

lan_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [[ -n "$lan_ip" ]]; then
  pass "Pi LAN IP detected: $lan_ip"
  echo "Web UI: http://$lan_ip:8080/"
else
  pass "Pi LAN IP not detected"
fi

echo "FINAL RESULT: PASS"
