# Raspberry Pi Satellite Rotator

Current release: **v0.2.0**

Raspberry Pi Zero 2 W controller for a two-axis antenna rotator using EasyComm over TCP. The intended hardware is an L298N dual DC motor driver and a WitMotion WT901-series angle sensor.

This repository currently contains a C++20 EasyComm TCP daemon, simulator mode, live WT901 USB feedback, a WT901 calibration/diagnostic utility, native protocol tests, and a Docker-based ARM64 cross-build. It does not energize GPIO yet.

## Development environment

All commands below run from an MSYS2 shell. Docker Desktop supplies the Linux build containers; no Windows compiler or PowerShell build step is used.

```bash
./scripts/test.sh
./scripts/build-rpi.sh
file dist/pi-satellite-rotator
```

The cross-build outputs are `dist/pi-satellite-rotator` and `dist/witmotion-tool`, targeting 64-bit Raspberry Pi OS (`aarch64`).

## Simulator and web control on Linux

```bash
./pi-satellite-rotator
printf 'AZ120.0 EL35.0\nAZ EL\n' | nc 127.0.0.1 4553
```

Open `http://PI_ADDRESS:8080/` for the integrated control page. Every web action is proxied over TCP to the EasyComm listener on port 4553, including status, move, jog, stop, zero, and park. Use `--web-port PORT` to change the HTTP port or `--no-web` to disable it.

Expected query response:

```text
AZ120.0 EL35.0
```

Read [the architecture](docs/architecture.md), [web control guide](docs/web-control.md), [wiring diagrams](docs/wiring.md), [hardware safety notes](docs/hardware.md), and [WT901 setup and calibration guide](docs/witmotion.md) before adding GPIO support.


## Raspberry Pi service install

After building the ARM64 artifacts with `./scripts/build-rpi.sh`, install them on a 64-bit Raspberry Pi OS system with:

```bash
sudo ./scripts/install-pi.sh --start
./scripts/validate-pi-install.sh --require-active
```

The installer creates a `satrot` service user, installs binaries under `/opt/pi-satellite-rotator/bin`, installs a systemd unit, creates `/etc/pi-satellite-rotator/pi-satellite-rotator.env`, and prints the Pi LAN IP address. See [the Raspberry Pi install guide](docs/pi-install.md) for details.

## GPIO motor backend

The default runtime remains safe simulator mode. Real L298N GPIO output is opt-in with `--motor-backend gpio` and requires `--sensor` so the controller has live WT901 feedback before motion. See [the GPIO motor backend guide](docs/gpio-motor-backend.md).

## GPIO smoke test

Before coupling motors to the antenna rotator, use the Pi-side GPIO smoke test to verify L298N wiring and direction with the motors mechanically unloaded:

```bash
sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis both --seconds 0.5
```

The script is dry-run by default and requires both `--execute` and `--confirm-unloaded` before it writes GPIO. See [the GPIO smoke test guide](docs/gpio-smoke-test.md).

### GPIO backend uses Raspberry Pi pinctrl

The daemon GPIO backend uses the Raspberry Pi `pinctrl` command, matching the standalone GPIO smoke test. Legacy `/sys/class/gpio` is not required.

