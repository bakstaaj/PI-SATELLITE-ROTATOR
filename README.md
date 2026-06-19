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
