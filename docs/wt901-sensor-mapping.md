# WT901 sensor mapping notes

Validated live WT901 mapping for the current bench orientation:

- Azimuth feedback: `yaw`
- Elevation feedback: `roll`
- Elevation inversion: normal / no `--el-invert`
- Conservative elevation offset: `14.60`
- Provisional azimuth offset: `270.00`

The physical WT901 `Y` arrow points in the azimuth / boresight direction. Because WIT roll/pitch/yaw are Euler angles, tilting the boresight upward is represented most strongly by the WIT `roll` angle in this mounting.

Measured sensor elevation is intentionally allowed to go below zero. Commanded target elevation remains constrained to `0..180` degrees until the mechanical zero and limit-switch workflow is added.

Live azimuth testing showed that `--az-offset 90.00` made a north-pointing boresight read approximately 180 degrees, so the provisional correction was changed to `--az-offset 270.00`.
