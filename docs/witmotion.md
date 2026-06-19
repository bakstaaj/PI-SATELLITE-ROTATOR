# WT901BLECL5.0 USB setup and calibration

The controller uses the sensor's Micro-USB data connection as a 115200-baud serial port. On Raspberry Pi OS it will normally appear as `/dev/ttyUSB0`; confirm the actual path after connecting it:

```bash
ls -l /dev/serial/by-id/ /dev/ttyUSB*
```

Prefer the stable `/dev/serial/by-id/...` name in service configuration when one is available.

## Inspect live data

Keep motor power disabled during initial sensor setup.

```bash
witmotion-tool monitor --device /dev/ttyUSB0 --seconds 20
```

The tool validates the WitMotion checksum and prints roll, pitch, yaw, acceleration, and magnetic-field readings as CSV. Use this output to determine the sensor's physical axis mapping and whether either controller axis must be inverted.

If monitoring reports no frames, make sure the sensor's physical switch is ON even if its red charging LED is lit. Then inspect the unparsed stream:

```bash
witmotion-tool raw --device /dev/ttyUSB0 --baud 115200 --seconds 5
witmotion-tool monitor --device /dev/ttyUSB0 --baud 9600 --seconds 5
```

Zero raw bytes means the sensor MCU is not transmitting: verify its switch, USB device path, and cable. The WT901BLECL5.0 USB interface may emit 20-byte combined packets beginning `55 61`; these contain acceleration, angular velocity, and roll/pitch/yaw in one packet. Older standard streams use separate packets beginning `55 51`, `55 52`, `55 53`, and `55 54`. The controller supports both formats. If bytes arrive only at 9600 baud, use `--sensor-baud 9600` for the service and record that device-specific setting.

## Accelerometer calibration

Remove the sensor from the steel rotator structure if practical. Place it level and completely stationary, then run:

```bash
witmotion-tool calibrate-accel --device /dev/ttyUSB0
```

Afterward, run `monitor` again. With the sensor stationary and level, X/Y acceleration should be near zero and Z should be near 1 g.

## Magnetic-field calibration

Perform this away from motors, steel, magnets, and high-current wiring. Slowly rotate the sensor through a complete turn around each of its three axes during the calibration interval:

```bash
witmotion-tool calibrate-magnetic --device /dev/ttyUSB0 --seconds 60
```

The command exits calibration mode and saves the result. Repeat the `monitor` test after mounting the sensor and again with motors running. If yaw changes materially when motor current is applied, the sensor must be moved farther from the motors/current wiring or magnetic heading will not be dependable.

## Run the EasyComm service with live feedback

Bench motion testing confirmed the mounted sensor mapping: **yaw is azimuth** and **roll is elevation**. These are now the service defaults:

```bash
pi-satellite-rotator \
  --sensor /dev/serial/by-id/usb-WitMotion_SENSOR-if00 \
  --az-axis yaw \
  --el-axis roll
```

Available mapping adjustments are `--az-axis`, `--el-axis`, `--az-offset`, `--el-offset`, `--az-invert`, and `--el-invert`. The service refuses elevation feedback outside 0-180 degrees instead of silently folding or clamping it. The installed Hall home sensors will capture the azimuth and elevation offsets at each homing cycle; the current un-homed sensor readings are not compiled in as permanent offsets.

The `55 61` combined USB packet does not include raw magnetometer values, so `witmotion-tool monitor` displays zeros in the `mag_x`, `mag_y`, and `mag_z` columns for this model. Yaw is still the WT901's internally fused heading output.

The calibration commands are based on WitMotion's standard protocol implementation: unlock `FF AA 69 88 B5`, accelerometer calibration `FF AA 01 01 00`, magnetic calibration `FF AA 01 07 00`, normal mode `FF AA 01 00 00`, and save `FF AA 00 00 00`.

Reference implementation: [WitMotion WitStandardProtocol_JY901](https://github.com/WITMOTION/WitStandardProtocol_JY901).
