#!/usr/bin/env bash
set -euo pipefail

START_SERVICE=0
ENABLE_SERVICE=1
SELF_CHECK=0

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\nFINAL RESULT: FAIL\n' "$1" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --start) START_SERVICE=1 ;;
    --no-enable) ENABLE_SERVICE=0 ;;
    --self-check) SELF_CHECK=1 ;;
    *) fail "unknown option: $1" ;;
  esac
  shift
done

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

[[ -f CMakeLists.txt && -d scripts && -d packaging && -d config ]] || fail "run from PI-SATELLITE-ROTATOR repository root"
pass "repository root verified"

[[ -f dist/pi-satellite-rotator ]] || fail "missing dist/pi-satellite-rotator; run ./scripts/build-rpi.sh first"
pass "dist/pi-satellite-rotator exists"

[[ -f dist/witmotion-tool ]] || fail "missing dist/witmotion-tool; run ./scripts/build-rpi.sh first"
pass "dist/witmotion-tool exists"

file dist/pi-satellite-rotator | grep -q "ARM aarch64" || fail "dist/pi-satellite-rotator is not ARM aarch64"
pass "pi-satellite-rotator artifact is ARM aarch64"

file dist/witmotion-tool | grep -q "ARM aarch64" || fail "dist/witmotion-tool is not ARM aarch64"
pass "witmotion-tool artifact is ARM aarch64"

[[ -f packaging/pi-satellite-rotator.service ]] || fail "missing packaging/pi-satellite-rotator.service"
pass "systemd service template exists"

[[ -f config/pi-satellite-rotator.env.example ]] || fail "missing config/pi-satellite-rotator.env.example"
pass "environment example exists"

[[ -f web/index.html && -f web/app.css && -f web/app.js ]] || fail "missing web assets under web/"
pass "web assets exist"

if [[ "$SELF_CHECK" -eq 1 ]]; then
  pass "installer self-check completed"
  echo "FINAL RESULT: PASS"
  exit 0
fi

[[ "$(uname -s)" == "Linux" ]] || fail "installer must run on Linux/Raspberry Pi OS"
pass "Linux host verified"

arch="$(uname -m)"
[[ "$arch" == "aarch64" || "$arch" == "arm64" ]] || fail "installer requires 64-bit Raspberry Pi OS aarch64; detected $arch"
pass "aarch64 host verified"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    exec sudo bash "$0" "$@"
  fi
  fail "installer must run as root or with sudo available"
fi
pass "root privileges verified"

install_user="satrot"
install_root="/opt/pi-satellite-rotator"
config_root="/etc/pi-satellite-rotator"
state_root="/var/lib/pi-satellite-rotator"
log_root="/var/log/pi-satellite-rotator"
service_path="/etc/systemd/system/pi-satellite-rotator.service"

if id "$install_user" >/dev/null 2>&1; then
  pass "service user already exists: $install_user"
else
  useradd --system --home-dir "$state_root" --shell /usr/sbin/nologin "$install_user"
  pass "service user created: $install_user"
fi

if getent group dialout >/dev/null 2>&1; then
  usermod -a -G dialout,gpio "$install_user"
  pass "service user added to dialout group"
else
  pass "dialout group not present; skipped supplementary group assignment"
fi

install -d -m 0755 "$install_root/bin"
install -d -m 0755 "$install_root/web"
install -d -m 0755 "$config_root"
install -d -m 0755 "$state_root"
install -d -m 0755 "$log_root"
chown "$install_user:$install_user" "$state_root" "$log_root"
pass "install/config/state/log directories prepared"

install -m 0755 dist/pi-satellite-rotator "$install_root/bin/pi-satellite-rotator"
install -m 0755 dist/witmotion-tool "$install_root/bin/witmotion-tool"
pass "binaries installed under $install_root/bin"

install -m 0644 web/index.html web/app.css web/app.js "$install_root/web/"
pass "web assets installed under $install_root/web"

install -m 0644 config/pi-satellite-rotator.env.example "$config_root/pi-satellite-rotator.env.example"
if [[ -f "$config_root/pi-satellite-rotator.env" ]]; then
  pass "existing environment file preserved"
else
  install -m 0644 config/pi-satellite-rotator.env.example "$config_root/pi-satellite-rotator.env"
  pass "default environment file installed"
fi

install -m 0644 packaging/pi-satellite-rotator.service "$service_path"
pass "systemd service installed"

systemctl daemon-reload
pass "systemd daemon reloaded"

if [[ "$ENABLE_SERVICE" -eq 1 ]]; then
  systemctl enable pi-satellite-rotator.service
  pass "systemd service enabled"
else
  pass "systemd service enable skipped by --no-enable"
fi

if [[ "$START_SERVICE" -eq 1 ]]; then
  systemctl restart pi-satellite-rotator.service
  pass "systemd service started"
else
  pass "systemd service start skipped; use --start to start now"
fi

lan_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [[ -n "$lan_ip" ]]; then
  pass "Pi LAN IP detected: $lan_ip"
  echo "Web UI: http://$lan_ip:8080/"
else
  pass "Pi LAN IP not detected"
fi

echo "FINAL RESULT: PASS"
