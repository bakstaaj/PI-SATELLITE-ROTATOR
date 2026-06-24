# Multi-architecture release packaging

The project builds two Docker cross-compiled release packages:

1. `rpi64-aarch64`
   - Target: Raspberry Pi OS 64-bit on aarch64/arm64.
   - Build script: `scripts/build-rpi64.sh`.

2. `rpi-zero-32-armv6`
   - Target: Raspberry Pi Zero W running 32-bit Raspberry Pi OS / Trixie Lite.
   - Build script: `scripts/build-rpi-zero-32.sh`.
   - Build method: Docker target build inside `balenalib/raspberry-pi-debian:bookworm-build`, which avoids Debian's generic ARMv7/Thumb-2 `armhf` cross-toolchain output.

Build both packages:

```bash
scripts/build-release-packages.sh
```

Outputs:

```text
dist/releases/pi-satellite-rotator-<version>-rpi64-aarch64.tar.gz
dist/releases/pi-satellite-rotator-<version>-rpi64-aarch64.tar.gz.sha256
dist/releases/pi-satellite-rotator-<version>-rpi-zero-32-armv6.tar.gz
dist/releases/pi-satellite-rotator-<version>-rpi-zero-32-armv6.tar.gz.sha256
```

Upload both packages to a GitHub release:

```bash
scripts/upload-release-packages.sh vX.Y.Z
```

Each package contains a standalone `install.sh`. Copy the correct tarball to the target Pi, extract it, then run:

```bash
sudo ./install.sh --start
```

Safety default: keep `PI_SATELLITE_ROTATOR_ARGS` configured with `--motor-backend simulator` until mechanical assembly, limit switches, and zeroing have been validated.

Release installer templating uses Python string replacement so architecture regex values may safely contain `|` alternation.

The Pi Zero W package must not advertise `Tag_CPU_arch: v7` or `Tag_THUMB_ISA_use: Thumb-2`; those attributes indicate an executable that can crash immediately on the original ARMv6 Pi Zero W.
