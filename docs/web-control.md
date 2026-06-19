# Web control panel

The rotator executable starts two listeners by default:

- EasyComm TCP: port 4553
- Web control: HTTP port 8080

Start in simulator mode:

```bash
./pi-satellite-rotator
```

Or start with live WT901 feedback:

```bash
./pi-satellite-rotator --sensor /dev/ttyUSB0
```

From another computer on the same network, open:

```text
http://PI_ADDRESS:8080/
```

## Controls

- **Move** sends `AZaaa.a ELbbb.b` through EasyComm.
- **Jog** calculates a new target from the latest EasyComm position and sends a normal move command.
- **Stop** sends `SA SE`.
- **Zero here** sends `ZERO`; the current feedback becomes 0 degrees azimuth and 0 degrees elevation for this process lifetime. Zeroing is rejected while motion is commanded or before the first live feedback sample; press **Stop** first.
- **Park 0/0** sends `PARK`, which requests azimuth 0 and elevation 0.
- Position cards poll `AZ EL` through EasyComm every 750 milliseconds.

In simulator mode, move and park targets are reached immediately. With live WT901 feedback but no motor backend, commands are accepted but the displayed position changes only when the sensor physically moves. This is intentional and allows the complete browser-to-EasyComm path to be tested before motors are energized.

## Options

```text
--port PORT       EasyComm port, default 4553
--web-port PORT   HTTP port, default 8080
--no-web          disable the HTTP server
```

## Network safety

The web interface binds to all network interfaces and currently has no authentication or TLS. Use it only on a trusted private network, restrict port 8080 with the Pi firewall, and do not expose it through router port forwarding. The physical emergency stop remains required whenever motor power is enabled.
