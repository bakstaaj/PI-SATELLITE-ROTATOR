#!/usr/bin/env bash
set -euo pipefail

START_SERVICE=0
ENABLE_SERVICE=1

PACKAGE_ARCH="%PACKAGE_ARCH%"
EXPECTED_UNAME_REGEX="%EXPECTED_UNAME_REGEX%"
EXPECTED_FILE_REGEX="%EXPECTED_FILE_REGEX%"

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\nFINAL RESULT: FAIL\n' "$1" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --start) START_SERVICE=1 ;;
    --no-enable) ENABLE_SERVICE=0 ;;
    *) fail "unknown option: $1" ;;
  esac
  shift
done

package_root="$(cd "$(dirname "$0")" && pwd)"
cd "$package_root"

[[ -f bin/pi-satellite-rotator ]] || fail "missing bin/pi-satellite-rotator"
[[ -f bin/witmotion-tool ]] || fail "missing bin/witmotion-tool"
[[ -f packaging/pi-satellite-rotator.service ]] || fail "missing systemd service file"
[[ -f config/pi-satellite-rotator.env.example ]] || fail "missing environment example"
[[ -f web/index.html && -f web/app.css && -f web/app.js ]] || fail "missing web assets"
pass "release package layout verified: $PACKAGE_ARCH"

[[ "$(uname -s)" == "Linux" ]] || fail "installer must run on Linux/Raspberry Pi OS"
arch="$(uname -m)"
if [[ ! "$arch" =~ $EXPECTED_UNAME_REGEX ]]; then
  fail "this package is for $PACKAGE_ARCH; detected uname -m=$arch"
fi
pass "host architecture accepted: $arch"

bin_info="$(file bin/pi-satellite-rotator)"
printf '%s\n' "$bin_info"
if [[ ! "$bin_info" =~ $EXPECTED_FILE_REGEX ]]; then
  fail "binary architecture does not match package $PACKAGE_ARCH"
fi
pass "binary architecture verified"

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

for group in dialout gpio; do
  if getent group "$group" >/dev/null 2>&1; then
    usermod -a -G "$group" "$install_user"
    pass "service user added to $group group"
  else
    pass "$group group not present; skipped supplementary group assignment"
  fi
done

install -d -m 0755 "$install_root/bin"
install -d -m 0755 "$install_root/web"
install -d -m 0755 "$config_root"
install -d -m 0755 "$state_root"
install -d -m 0755 "$log_root"
chown "$install_user:$install_user" "$state_root" "$log_root"
pass "install/config/state/log directories prepared"

install -m 0755 bin/pi-satellite-rotator "$install_root/bin/pi-satellite-rotator"
install -m 0755 bin/witmotion-tool "$install_root/bin/witmotion-tool"
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
