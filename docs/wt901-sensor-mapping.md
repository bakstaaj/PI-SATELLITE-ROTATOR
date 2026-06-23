# WT901 sensor mapping notes

Validated live WT901 mapping for the current bench orientation:

- Azimuth feedback: `yaw`
- Provisional azimuth offset: `270.00`
- Elevation feedback: `roll`
- Elevation inversion: normal / no `--el-invert`
- Conservative elevation offset: `14.60`
- Motor backend while unassembled: `simulator`

The physical WT901 `Y` arrow points in the azimuth / boresight direction. Because WIT roll/pitch/yaw are Euler angles, tilting the boresight upward is represented most strongly by the WIT `roll` angle in this mounting.

Live azimuth testing showed that `--az-offset 90.00` made a north-pointing boresight read approximately 180 degrees, so the provisional correction was changed to `--az-offset 270.00`.

Measured sensor elevation is intentionally allowed to go below zero before true mechanical zero is established. Commanded target elevation remains constrained to `0..180` degrees until the lower limit switch and true-zero workflow are added.

Recommended parked safe service arguments:

```text
--port 4553 --web-port 8080 --feedback-timeout-ms 1000 --sensor /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --sensor-baud 115200 --motor-backend simulator --az-axis yaw --az-offset 270.00 --el-axis roll --el-offset 14.60
```
