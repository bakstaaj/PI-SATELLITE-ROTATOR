# GPIO motor backend v1

The GPIO motor backend is the first hardware-moving backend for the L298N dual H-bridge. It is disabled by default. The default `simulator` backend never energizes GPIO.

Enable real motor output only with:

```bash
pi-satellite-rotator \
  --sensor /dev/serial/by-id/YOUR_WT901_DEVICE \
  --motor-backend gpio
```

The backend requires WT901 feedback because motor control is closed-loop. The program refuses `--motor-backend gpio` unless `--sensor` is also provided.

## Default GPIO mapping

| Axis | L298N signal | Raspberry Pi GPIO |
| --- | --- | --- |
| Azimuth | ENA | GPIO12 |
| Azimuth | IN1 / forward | GPIO5 |
| Azimuth | IN2 / reverse | GPIO6 |
| Elevation | ENB | GPIO13 |
| Elevation | IN3 / forward | GPIO16 |
| Elevation | IN4 / reverse | GPIO20 |

Override pins with:

```bash
--az-enable-gpio N --az-forward-gpio N --az-reverse-gpio N
--el-enable-gpio N --el-forward-gpio N --el-reverse-gpio N
```

If an axis moves the wrong direction, use `--az-motor-invert` or `--el-motor-invert`.

## Safety behavior

The controller stops all motor outputs on STOP, SIGINT, SIGTERM, target deadband, stale WT901 feedback, sensor failure, or GPIO write fault.

GPIO output is digital on/off in v1. PWM speed control is intentionally left for a later milestone after direction and fail-safe stopping are verified on real hardware.

## Hardware smoke test

Before enabling `--motor-backend gpio` in the daemon, run the standalone GPIO smoke test with the motors mechanically unloaded. See [GPIO smoke test](gpio-smoke-test.md).

