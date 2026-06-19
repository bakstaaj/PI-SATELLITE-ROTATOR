# Hardware integration and safety

See [wiring diagrams](wiring.md) for the complete pin-by-pin connections.

## Confirmed hardware baseline

- Raspberry Pi Zero 2 W running 64-bit Raspberry Pi OS.
- L298N dual H-bridge module driving two MECCANIXITY 12 V, 2 RPM reversible worm-geared motors.
- WT901BLECL5.0 connected through its Micro-USB serial interface at 115200 baud.
- Belt-driven azimuth and elevation axes use the WT901 angles as continuous closed-loop feedback; no position is inferred from motor runtime or gearing.
- One active-low Hall-effect home sensor is fitted to each axis.

## GPIO assignment

| Function | BCM GPIO | Header pin | Notes |
|---|---:|---:|---|
| Azimuth L298N ENA | GPIO12 | 32 | Hardware PWM channel 0; remove the ENA jumper |
| Azimuth L298N IN1 | GPIO5 | 29 | Direction control |
| Azimuth L298N IN2 | GPIO6 | 31 | Direction control |
| Elevation L298N ENB | GPIO13 | 33 | Hardware PWM channel 1; remove the ENB jumper |
| Elevation L298N IN3 | GPIO16 | 36 | Direction control |
| Elevation L298N IN4 | GPIO20 | 38 | Direction control |
| Azimuth home | GPIO23 | 16 | Active-low KY-003 signal, module powered at 3.3 V |
| Elevation home | GPIO24 | 18 | Active-low KY-003 signal, module powered at 3.3 V |

GPIO12 and GPIO13 provide independent hardware PWM channels. This assignment leaves the primary UART, I2C, and SPI pins available for future use. Fit external 10 kOhm pull-down resistors to ENA, ENB, IN1, IN2, IN3, and IN4 so both motors remain disabled while the Pi boots or the controller is stopped.

## Hall-effect home sensor

Use the selected **KY-003 3144E/A3144 digital Hall-effect module**, one per axis. The module is a unipolar, non-latching switch: its signal goes low only when the correct magnet pole approaches the sensitive face and returns high when the magnet is removed.

The original Allegro A3144 is a discontinued 4.5-24 V open-collector device. Modules advertised for 3.3 V may contain a compatible clone and board-level pull-up/indicator components, so do not assume every supplier's KY-003 is electrically identical.

For each axis:

- Power the complete module from Pi 3.3 V, not 5 V. Connect module `S`/signal to the assigned home GPIO and module ground to Pi ground. Verify the module's printed pin labels because clone-board pin order varies.
- Before connecting `S` to the Pi, power the module from 3.3 V and measure `S` with a multimeter. It must remain between 0 and 3.3 V: approximately 3.3 V with no magnet and below 0.4 V with the activating pole present.
- Do not connect the signal from a 5 V-powered KY-003 directly to a Pi GPIO. If a particular module will not switch reliably from 3.3 V, it requires a verified 3.3 V level-shifting circuit or replacement; do not simply move VCC to 5 V.
- The module normally includes its own output pull-up/LED network. Do not add the previously specified external 10 kOhm pull-up until the actual board has been inspected. A 0.1 uF ceramic bypass capacitor across module VCC and GND is still recommended.
- Treat LOW as magnet detected. The A3144 is unipolar, so test and permanently mark the working magnet face before installation.
- Mount the magnet so the sensor remains active across a useful window rather than at one knife-edge point.
- Route signal with a ground conductor and keep it away from motor leads. Use a small sealed enclosure for outdoor mounting.
- Approach home slowly and apply software debounce to reject motor-noise transients. The controller will require several consecutive active samples before accepting home.

At startup, an axis already on its home sensor will first back away until the signal clears, then approach slowly and record the WT901 angle at the repeatable trigger edge. The trigger defines the mechanism's physical zero; the WT901 provides continuous feedback between homing operations.

## Important electrical constraints

- The selected WT901BLECL5.0 connects by USB serial, so no RS-232 level converter is required. Use the Pi Zero 2 W USB-OTG data port through a suitable OTG adapter or powered hub.
- Raspberry Pi GPIO is 3.3 V only. Never expose it to motor voltage, L298N 5 V logic output, or raw RS-232.
- Power motors from a separate suitable supply. Join logic grounds at the intended common point, add local decoupling, and account for motor noise.
- The L298N has a substantial voltage drop and heat loss. Verify motor stall current is safely below the module rating under real cooling conditions.
- Do not power the Pi from the L298N module's 5 V regulator. Join the L298N logic ground and Pi ground, and verify the particular module's 5 V regulator/jumper arrangement before applying 12 V.
- Fit a hardware-accessible emergency stop that removes motor power independently of software.

## Inputs still needed before the motor backend

- Motor voltage, rated current, measured stall current, gearbox ratio, and desired maximum speed.
- WT901 Linux device path, output protocol/rate, axis orientation, and mounting position.
- Whether azimuth has a cable wrap/hard stop and its permitted coordinate interval.
- Magnet placement and the desired physical direction of travel toward each home position.

## Sensor caveat

The WT901 yaw estimate depends on its magnetometer. Steel structures, motor currents, and nearby magnets can distort heading badly. Bench-test heading across the full mechanism under motor load. A shaft encoder or geared absolute encoder may be needed for dependable azimuth feedback; the IMU remains useful for elevation and motion validation.
