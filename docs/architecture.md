# Controller architecture

The controller is split so protocol and safety logic can be tested without energized motors.

1. The TCP service accepts newline-delimited EasyComm commands on port 4553.
2. The EasyComm parser turns commands into bounded azimuth/elevation targets.
3. A motion controller will compare target angles with filtered sensor angles and command each motor.
4. Hardware adapters will own GPIO/PWM, limit switches, and the WT901 serial stream.
5. A safety supervisor will stop both axes on stale sensor data, travel-limit violation, process shutdown, or control timeout.

The current implementation includes steps 1 and 2, simulator mode, the WT901 binary protocol decoder, USB serial transport, calibration utility, live angle feedback in the EasyComm service, status/fault reporting, and stale-feedback protection. GPIO motor output remains disabled until the mounted sensor axis mapping is measured and verified.

## Planned control loop

The motion loop should run independently of TCP clients at a fixed rate. Each axis will use signed angular error, a deadband, ramped PWM, and braking/coasting behavior selected during bench testing. Azimuth is a bounded 0-359 degree axis, not an assumed continuously rotating axis; shortest-path movement must account for cable and hard-stop constraints.

The controller never derives antenna position from motor runtime, belt ratio, or motor revolutions. Hall modules establish the two physical home references; the WT901 supplies all continuous azimuth/elevation feedback. At home, the controller will capture the sensor-to-mechanism offsets and use them until the next homing cycle.

## EasyComm subset

- `AZ123.4 EL45.6` sets both targets.
- `AZ123.4` or `EL45.6` sets one target.
- `AZ EL`, `AZ`, or `EL` queries the current position. The service returns `AZ123.4 EL45.6`.
- `STATUS` returns a non-standard JSON status object for the integrated web UI and diagnostics.
- `SA`, `SE`, `SA SE`, or `STOP` stops motion.
- `ZERO` captures the current feedback position as azimuth 0 and elevation 0.
- `PARK` requests azimuth 0 and elevation 0.

Commands may be terminated by LF or CRLF. Responses use CRLF. The TCP service handles concurrent persistent clients, allowing tracking software and the web control proxy to remain connected at the same time.

The integrated HTTP server listens on port 8080. Its API does not access `RotatorController` directly: every status or control operation opens a TCP connection to `127.0.0.1:4553` and sends the corresponding EasyComm command. This keeps browser testing on the same protocol path used by external tracking software.
