# Multi-architecture release packaging

The project builds two Docker cross-compiled release packages:

1. `rpi64-aarch64`
   - Target: Raspberry Pi OS 64-bit on aarch64/arm64.
   - Build script: `scripts/build-rpi64.sh`.

2. `rpi-zero-32-armv6`
   - Target: Raspberry Pi Zero W running 32-bit Raspberry Pi OS / Trixie Lite.
   - Build script: `scripts/build-rpi-zero-32.sh`.
   - Toolchain: `arm-linux-gnueabihf-g++` with `-march=armv6 -marm -mfpu=vfp -mfloat-abi=hard`. `-marm` is required because Thumb-1 does not support the hard-float VFP ABI.

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
