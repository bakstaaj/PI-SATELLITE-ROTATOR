# Changelog

## v0.1.0 - 2026-06-18

Initial working software baseline:

- EasyComm TCP service on port 4553 with simulator and live WT901 feedback modes.
- WT901BLECL5.0 USB support for combined `0x55 0x61` motion packets.
- WT901 monitoring, accelerometer calibration, and magnetic calibration utility.
- Integrated responsive web controller on port 8080; all operations proxy through EasyComm.
- Move, jog, stop, zero-current-position, and park controls.
- Raspberry Pi Zero 2 W, L298N, KY-003, motor, power, and USB wiring documentation.
- Native protocol and end-to-end web tests.
- Docker-based ARM64 cross-build workflow for 64-bit Raspberry Pi OS.

Motor GPIO actuation and Hall-sensor homing are reserved for the next hardware integration milestone.
