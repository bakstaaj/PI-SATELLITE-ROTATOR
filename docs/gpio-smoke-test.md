# GPIO/L298N smoke test

Use `scripts/gpio-smoke-test.sh` on the Raspberry Pi before mechanically coupling the motors to the antenna rotator.

The smoke test directly toggles the L298N GPIO pins. It is separate from the EasyComm daemon and does not require WT901 feedback.

## Safety requirements

Before using `--execute`:

- mechanically unload the motors
- keep hands clear of moving parts
- confirm the L298N motor power supply is appropriate
- be ready to remove motor power
- verify the GPIO wiring matches `docs/wiring.md`

The script is dry-run by default. It will not touch GPIO unless both options are present:

```bash
--execute --confirm-unloaded
```

It stops all configured GPIO outputs on normal exit, Ctrl+C, or termination.

## Dry run

From the repo on any machine:

```bash
./scripts/gpio-smoke-test.sh --axis both
```

## Real Raspberry Pi test

On the Raspberry Pi with motors unloaded:

```bash
sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis both --seconds 0.5
```

To test one axis:

```bash
sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis az --seconds 0.5
sudo ./scripts/gpio-smoke-test.sh --execute --confirm-unloaded --axis el --seconds 0.5
```

If an axis moves opposite of the intended direction, use:

```bash
--az-motor-invert
--el-motor-invert
```

## Default pins

| Axis | Signal | GPIO |
| --- | --- | --- |
| Azimuth | Enable | GPIO12 |
| Azimuth | Forward | GPIO5 |
| Azimuth | Reverse | GPIO6 |
| Elevation | Enable | GPIO13 |
| Elevation | Forward | GPIO16 |
| Elevation | Reverse | GPIO20 |

Override pins with:

```bash
--az-enable-gpio N --az-forward-gpio N --az-reverse-gpio N
--el-enable-gpio N --el-forward-gpio N --el-reverse-gpio N
```
