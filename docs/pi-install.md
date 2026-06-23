# Raspberry Pi install and service operation

This project can be installed as a systemd service on 64-bit Raspberry Pi OS.

The service runs as the dedicated `satrot` user, installs binaries under `/opt/pi-satellite-rotator/bin`, stores runtime state under `/var/lib/pi-satellite-rotator`, and reads command-line options from `/etc/pi-satellite-rotator/pi-satellite-rotator.env`.

## Build on the development laptop

From MSYS2 in the repository root:

```bash
./scripts/test.sh
./scripts/build-rpi.sh
```

The ARM64 artifacts are written to:

```text
dist/pi-satellite-rotator
dist/witmotion-tool
```

## Install on the Raspberry Pi

Copy the repository or at least the `dist/`, `scripts/`, `packaging/`, and `config/` directories to the Pi, then run:

```bash
sudo ./scripts/install-pi.sh --start
```

The default environment starts in safe simulator mode:

```text
PI_SATELLITE_ROTATOR_ARGS=--port 4553 --web-port 8080 --feedback-timeout-ms 1000
```

To enable WT901 USB feedback, edit:

```text
/etc/pi-satellite-rotator/pi-satellite-rotator.env
```

Use a stable `/dev/serial/by-id/...` path when possible.

## Validate the installed service

```bash
./scripts/validate-pi-install.sh --require-active
```

Without `--require-active`, the validator accepts an installed but stopped service.

## Service commands

```bash
sudo systemctl status pi-satellite-rotator.service
sudo systemctl restart pi-satellite-rotator.service
sudo journalctl -u pi-satellite-rotator.service -f
```

## Network note

The web UI listens on port 8080 and has no authentication. Use it only on a trusted private LAN and do not expose it through router port forwarding.
