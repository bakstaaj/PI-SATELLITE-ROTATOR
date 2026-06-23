# Web WT901 sensor controls

The web UI includes a **Sensor diagnostics** panel for WT901 validation and calibration.

## Controls

- **Sensor Test** calls `/api/sensor/test` and returns the same JSON status as EasyComm `STATUS`.
- **Level/Accel Cal** queues WT901 accelerometer calibration. Keep the sensor level and motionless.
- **Mag Cal Start** starts WT901 magnetic calibration mode. Rotate the sensor slowly around all axes.
- **Mag Finish/Save** exits magnetic calibration mode and saves the calibration.

## Safety behavior

Calibration commands are handled by the daemon's existing WT901 serial reader thread. The web server does not open a second competing serial session.

Calibration requests are rejected unless WT901 external feedback is enabled. Requests are also rejected while motion is commanded. The daemon calls stop before sending calibration commands to the sensor.

## EasyComm commands

```text
SENSOR TEST
SENSOR CALIBRATE ACCEL
SENSOR CALIBRATE MAGNETIC START
SENSOR CALIBRATE MAGNETIC FINISH
```

These commands are intended for diagnostics and setup. They do not intentionally command rotator motion.

## Calibration status

During Level/Accel and magnetic calibration command windows, STATUS includes `sensor_maintenance` and `sensor_maintenance_reason`. A temporary stale WT901 frame age during this maintenance window is not treated as a controller fault.

## Sensor stream versus mapped feedback

`SENSOR TEST` reports both WT901 stream health and mapped rotator feedback health. `sensor_stream_received` and `sensor_stream_age_ms` indicate whether frames are arriving from the WT901. Mapped feedback may still be invalid if the selected axis, inversion, or offset produces an elevation outside the allowed 0-180 degree rotator range.
