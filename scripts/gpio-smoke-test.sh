#!/usr/bin/env bash
set -euo pipefail

# GPIO/L298N motor smoke test for PI-SATELLITE-ROTATOR.
#
# Safe defaults:
#   - dry-run mode by default
#   - requires --execute to touch GPIO
#   - requires --confirm-unloaded with --execute
#   - stops all configured outputs on exit/INT/TERM
#
# Example dry run:
#   ./scripts/gpio-smoke-test.sh --axis both
#
# Example real test on Raspberry Pi with motors mechanically unloaded:
#   sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis both --seconds 0.5

AZ_ENABLE=12
AZ_FORWARD=5
AZ_REVERSE=6
EL_ENABLE=13
EL_FORWARD=16
EL_REVERSE=20
SECONDS_PER_DIRECTION="0.5"
AXIS="both"
EXECUTE=0
CONFIRM_UNLOADED=0
SELF_CHECK=0
AZ_INVERT=0
EL_INVERT=0
CONFIGURED=0
ORIGINAL_ARGS=("$@")

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\nFINAL RESULT: FAIL\n' "$1" >&2; exit 1; }

usage() {
  cat <<'USAGE'
Usage:
  gpio-smoke-test.sh [options]

Safe dry-run:
  ./scripts/gpio-smoke-test.sh --axis both

Real GPIO test on Raspberry Pi:
  sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis both --seconds 0.5

Options:
  --self-check
  --dry-run
  --execute
  --confirm-unloaded
  --axis az|el|both
  --seconds N
  --az-enable-gpio N
  --az-forward-gpio N
  --az-reverse-gpio N
  --el-enable-gpio N
  --el-forward-gpio N
  --el-reverse-gpio N
  --az-motor-invert
  --el-motor-invert
USAGE
}

is_number() {
  [[ "$1" =~ ^[0-9]+([.][0-9]+)?$ ]]
}

is_gpio() {
  [[ "$1" =~ ^[0-9]+$ ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --self-check)
      SELF_CHECK=1
      shift
      ;;
    --dry-run)
      EXECUTE=0
      shift
      ;;
    --execute)
      EXECUTE=1
      shift
      ;;
    --confirm-unloaded)
      CONFIRM_UNLOADED=1
      shift
      ;;
    --axis)
      [[ $# -ge 2 ]] || fail "--axis requires az, el, or both"
      AXIS="$2"
      [[ "$AXIS" == "az" || "$AXIS" == "el" || "$AXIS" == "both" ]] || fail "--axis must be az, el, or both"
      shift 2
      ;;
    --seconds)
      [[ $# -ge 2 ]] || fail "--seconds requires a value"
      is_number "$2" || fail "--seconds must be numeric"
      SECONDS_PER_DIRECTION="$2"
      shift 2
      ;;
    --az-enable-gpio)
      [[ $# -ge 2 ]] || fail "--az-enable-gpio requires a value"
      is_gpio "$2" || fail "--az-enable-gpio must be a GPIO number"
      AZ_ENABLE="$2"
      shift 2
      ;;
    --az-forward-gpio)
      [[ $# -ge 2 ]] || fail "--az-forward-gpio requires a value"
      is_gpio "$2" || fail "--az-forward-gpio must be a GPIO number"
      AZ_FORWARD="$2"
      shift 2
      ;;
    --az-reverse-gpio)
      [[ $# -ge 2 ]] || fail "--az-reverse-gpio requires a value"
      is_gpio "$2" || fail "--az-reverse-gpio must be a GPIO number"
      AZ_REVERSE="$2"
      shift 2
      ;;
    --el-enable-gpio)
      [[ $# -ge 2 ]] || fail "--el-enable-gpio requires a value"
      is_gpio "$2" || fail "--el-enable-gpio must be a GPIO number"
      EL_ENABLE="$2"
      shift 2
      ;;
    --el-forward-gpio)
      [[ $# -ge 2 ]] || fail "--el-forward-gpio requires a value"
      is_gpio "$2" || fail "--el-forward-gpio must be a GPIO number"
      EL_FORWARD="$2"
      shift 2
      ;;
    --el-reverse-gpio)
      [[ $# -ge 2 ]] || fail "--el-reverse-gpio requires a value"
      is_gpio "$2" || fail "--el-reverse-gpio must be a GPIO number"
      EL_REVERSE="$2"
      shift 2
      ;;
    --az-motor-invert)
      AZ_INVERT=1
      shift
      ;;
    --el-motor-invert)
      EL_INVERT=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

if [[ "$SELF_CHECK" -eq 1 ]]; then
  pass "gpio-smoke-test argument parser is available"
  pass "default AZ pins: EN=${AZ_ENABLE} FWD=${AZ_FORWARD} REV=${AZ_REVERSE}"
  pass "default EL pins: EN=${EL_ENABLE} FWD=${EL_FORWARD} REV=${EL_REVERSE}"
  echo "FINAL RESULT: PASS"
  exit 0
fi

if [[ "$EXECUTE" -eq 1 && "$CONFIRM_UNLOADED" -ne 1 ]]; then
  fail "--execute requires --confirm-unloaded"
fi

if [[ "$EXECUTE" -eq 1 ]]; then
  case "$(uname -s)" in
    Linux) ;;
    *) fail "--execute is supported only on Linux/Raspberry Pi" ;;
  esac
  if [[ ! -d /sys/class/gpio ]]; then
    fail "sysfs GPIO directory not found"
  fi
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
      exec sudo bash "$0" "${ORIGINAL_ARGS[@]}"
    fi
    fail "--execute requires root privileges"
  fi
fi

write_gpio_file() {
  local path="$1"
  local value="$2"
  if [[ "$EXECUTE" -eq 1 ]]; then
    printf '%s' "$value" > "$path"
  else
    printf 'DRY-RUN: write %s -> %s\n' "$value" "$path"
  fi
}

gpio_path() {
  printf '/sys/class/gpio/gpio%s' "$1"
}

export_gpio() {
  local gpio="$1"
  local path
  path="$(gpio_path "$gpio")"
  if [[ "$EXECUTE" -eq 1 && ! -d "$path" ]]; then
    write_gpio_file /sys/class/gpio/export "$gpio"
    sleep 0.05
  elif [[ "$EXECUTE" -eq 0 ]]; then
    printf 'DRY-RUN: export GPIO%s\n' "$gpio"
  fi
}

set_gpio() {
  local gpio="$1"
  local value="$2"
  write_gpio_file "$(gpio_path "$gpio")/value" "$value"
}

configure_gpio() {
  local gpio="$1"
  export_gpio "$gpio"
  write_gpio_file "$(gpio_path "$gpio")/direction" out
  set_gpio "$gpio" 0
}

axis_stop() {
  local en="$1"
  local fwd="$2"
  local rev="$3"
  set_gpio "$en" 0
  set_gpio "$fwd" 0
  set_gpio "$rev" 0
}

stop_all() {
  if [[ "$CONFIGURED" -eq 1 ]]; then
    axis_stop "$AZ_ENABLE" "$AZ_FORWARD" "$AZ_REVERSE" || true
    axis_stop "$EL_ENABLE" "$EL_FORWARD" "$EL_REVERSE" || true
  fi
}

trap stop_all EXIT INT TERM

configure_axis() {
  local en="$1"
  local fwd="$2"
  local rev="$3"
  configure_gpio "$en"
  configure_gpio "$fwd"
  configure_gpio "$rev"
}

configure_all() {
  configure_axis "$AZ_ENABLE" "$AZ_FORWARD" "$AZ_REVERSE"
  configure_axis "$EL_ENABLE" "$EL_FORWARD" "$EL_REVERSE"
  CONFIGURED=1
  stop_all
}

axis_drive() {
  local label="$1"
  local en="$2"
  local fwd="$3"
  local rev="$4"
  local dir="$5"
  local invert="$6"

  if [[ "$invert" -eq 1 ]]; then
    if [[ "$dir" == "positive" ]]; then
      dir="negative"
    elif [[ "$dir" == "negative" ]]; then
      dir="positive"
    fi
  fi

  printf 'TEST: %s %s for %s second(s)\n' "$label" "$dir" "$SECONDS_PER_DIRECTION"

  if [[ "$dir" == "positive" ]]; then
    set_gpio "$en" 0
    set_gpio "$rev" 0
    set_gpio "$fwd" 1
    set_gpio "$en" 1
  else
    set_gpio "$en" 0
    set_gpio "$fwd" 0
    set_gpio "$rev" 1
    set_gpio "$en" 1
  fi

  sleep "$SECONDS_PER_DIRECTION"
  axis_stop "$en" "$fwd" "$rev"
  sleep 0.25
}

test_axis() {
  local label="$1"
  local en="$2"
  local fwd="$3"
  local rev="$4"
  local invert="$5"

  axis_drive "$label" "$en" "$fwd" "$rev" positive "$invert"
  axis_drive "$label" "$en" "$fwd" "$rev" negative "$invert"
}

echo "GPIO/L298N smoke test"
echo "Mode: $([[ "$EXECUTE" -eq 1 ]] && echo execute || echo dry-run)"
echo "Axis: ${AXIS}"
echo "Duration: ${SECONDS_PER_DIRECTION}s per direction"
echo "AZ pins: EN=${AZ_ENABLE} FWD=${AZ_FORWARD} REV=${AZ_REVERSE} INVERT=${AZ_INVERT}"
echo "EL pins: EN=${EL_ENABLE} FWD=${EL_FORWARD} REV=${EL_REVERSE} INVERT=${EL_INVERT}"

configure_all

case "$AXIS" in
  az)
    test_axis azimuth "$AZ_ENABLE" "$AZ_FORWARD" "$AZ_REVERSE" "$AZ_INVERT"
    ;;
  el)
    test_axis elevation "$EL_ENABLE" "$EL_FORWARD" "$EL_REVERSE" "$EL_INVERT"
    ;;
  both)
    test_axis azimuth "$AZ_ENABLE" "$AZ_FORWARD" "$AZ_REVERSE" "$AZ_INVERT"
    test_axis elevation "$EL_ENABLE" "$EL_FORWARD" "$EL_REVERSE" "$EL_INVERT"
    ;;
esac

stop_all
pass "all selected GPIO smoke-test motions completed and outputs stopped"
echo "FINAL RESULT: PASS"
