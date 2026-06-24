# Azimuth-only north zero workflow

The web UI includes a **Set North** control for bench alignment.

Use it when the boresight is physically pointed north but the WT901 azimuth reading is not zero. The control sends `ZERO AZ` through the web API and EasyComm path. This updates only the azimuth feedback zero reference and leaves elevation calibration untouched.

Related controls:

- **Set North**: sets current azimuth to 0/North only.
- **Zero All**: sets current azimuth and current elevation to 0.
- **Park 0/0**: commands a target of azimuth 0 and elevation 0.

Keep the motor backend in `simulator` until the mechanical assembly and limit switches are installed.
