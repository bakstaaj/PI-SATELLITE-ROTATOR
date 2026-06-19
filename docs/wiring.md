# Wiring diagrams

These diagrams use the Raspberry Pi header's **physical pin numbers** and BCM GPIO names together. Disconnect all power while wiring. Verify every module label with a meter before connecting it to the Pi because clone-board layouts can vary.

## Complete system

```mermaid
flowchart LR
    PSU12["Fused 12 V motor supply"]
    ESTOP["Emergency-stop switch"]
    L298["L298N module"]
    AZM["Azimuth 12 V motor"]
    ELM["Elevation 12 V motor"]
    PSU5["Regulated 5 V Pi supply"]
    PI["Raspberry Pi Zero 2 W"]
    AZH["Azimuth KY-003 home sensor"]
    ELH["Elevation KY-003 home sensor"]
    OTG["USB-OTG adapter or powered hub"]
    WIT["WT901BLECL5.0 Micro-USB"]

    PSU12 -->|"+12 V"| ESTOP
    ESTOP -->|"switched +12 V"| L298
    PSU12 -->|"0 V / ground"| L298
    L298 -->|"OUT1 / OUT2"| AZM
    L298 -->|"OUT3 / OUT4"| ELM

    PSU5 -->|"Pi power input"| PI
    PI <-->|"control GPIO + common ground"| L298
    PI -->|"3.3 V + ground"| AZH
    AZH -->|"active-low GPIO23"| PI
    PI -->|"3.3 V + ground"| ELH
    ELH -->|"active-low GPIO24"| PI
    PI <-->|"USB data port"| OTG
    OTG <-->|"USB data + sensor charging"| WIT
```

Use separate regulated supplies for the Pi and motors. The grounds must be common for the L298N control signals, but **do not connect the L298N module's 5 V terminal to the Pi's 5 V rail**.

## L298N and motors

Remove the L298N module's **ENA and ENB jumper caps** before attaching the PWM wires.

```mermaid
flowchart LR
    subgraph PI["Raspberry Pi Zero 2 W header"]
        P32["Pin 32 - GPIO12 PWM"]
        P29["Pin 29 - GPIO5"]
        P31["Pin 31 - GPIO6"]
        P33["Pin 33 - GPIO13 PWM"]
        P36["Pin 36 - GPIO16"]
        P38["Pin 38 - GPIO20"]
        PG["Pin 34 or 39 - GND"]
    end

    subgraph DRIVER["L298N module"]
        ENA["ENA"]
        IN1["IN1"]
        IN2["IN2"]
        ENB["ENB"]
        IN3["IN3"]
        IN4["IN4"]
        DG["GND"]
        VM["+12 V / VS"]
        O12["OUT1 / OUT2"]
        O34["OUT3 / OUT4"]
        V5["5 V terminal - leave disconnected from Pi"]
    end

    AZ["Azimuth motor"]
    EL["Elevation motor"]
    SUPPLY["Emergency-stop switched +12 V"]
    RETURN["12 V supply negative"]

    P32 --> ENA
    P29 --> IN1
    P31 --> IN2
    P33 --> ENB
    P36 --> IN3
    P38 --> IN4
    PG --- DG
    SUPPLY --> VM
    RETURN --- DG
    O12 --> AZ
    O34 --> EL
```

| Pi connection | Physical pin | L298N connection | Purpose |
|---|---:|---|---|
| GPIO12 | 32 | ENA | Azimuth PWM/speed |
| GPIO5 | 29 | IN1 | Azimuth direction |
| GPIO6 | 31 | IN2 | Azimuth direction |
| GPIO13 | 33 | ENB | Elevation PWM/speed |
| GPIO16 | 36 | IN3 | Elevation direction |
| GPIO20 | 38 | IN4 | Elevation direction |
| Ground | 34 or 39 | GND | Common logic/power reference |

Add a 10 kOhm resistor from each of ENA, ENB, IN1, IN2, IN3, and IN4 to ground at the driver end. These pull-downs keep both motors disabled while the Pi boots or is disconnected.

Motor polarity only determines which software direction is positive. If an axis moves backward during the low-power bench test, either swap that motor's two output wires or select the corresponding software inversion setting later—never change wiring while powered.

## KY-003 Hall home sensors

Power both modules from 3.3 V. Pin order varies between KY-003 clones, so follow the module's printed `S`, `+`, and `-` labels rather than assuming their physical order.

```mermaid
flowchart LR
    VCC["Pi pin 1 - 3.3 V"]
    GND["Pi pin 6 or 9 - GND"]
    AZS["Pi pin 16 - GPIO23"]
    ELS["Pi pin 18 - GPIO24"]

    subgraph AZH["Azimuth KY-003"]
        AZP["+"]
        AZN["-"]
        AZOUT["S"]
    end

    subgraph ELH["Elevation KY-003"]
        ELP["+"]
        ELN["-"]
        ELOUT["S"]
    end

    VCC --> AZP
    VCC --> ELP
    GND --- AZN
    GND --- ELN
    AZOUT --> AZS
    ELOUT --> ELS
```

Before connecting either `S` wire to the Pi:

1. Power the KY-003 from 3.3 V.
2. Measure `S` to ground with no magnet; it should be approximately 3.3 V.
3. Present the activating magnet pole; `S` should fall below 0.4 V.
4. Confirm `S` never exceeds 3.3 V.
5. Mark the activating magnet face and its matching sensor face.

Add a 0.1 uF ceramic capacitor between `+` and `-` at each module. Route each sensor cable as signal/ground/3.3 V together and keep it away from motor wiring. The firmware treats LOW as home detected and applies debounce.

## WT901BLECL5.0 USB connection

```mermaid
flowchart LR
    PWR["Regulated 5 V supply"] --> PIPWR["Pi PWR IN Micro-USB"]
    PIDATA["Pi USB data Micro-USB port"] <-->|"USB-OTG adapter"| HUB["USB hub - optional"]
    HUB <-->|"USB-A to Micro-USB data cable"| WIT["WT901BLECL5.0"]
```

- Use the Pi connector labeled **USB** for data and the connector labeled **PWR IN** for Pi power.
- A direct OTG adapter is suitable if its power budget is adequate. A quality externally powered USB hub is preferable when adding more USB devices; use one designed not to back-feed its upstream port.
- Use a real USB data cable, not a charge-only cable.
- Do not connect the WT901 to Pi GPIO UART pins. The controller communicates only through its USB serial device, normally `/dev/ttyUSB0` or a stable `/dev/serial/by-id/...` path.

## Noise suppression and first-power checks

- Fit an appropriately rated fuse close to the 12 V supply and place the emergency stop in series with motor positive power before the L298N.
- Twist each motor's two wires together and keep them separate from USB and Hall-sensor cables.
- Fit a 0.1 uF ceramic suppression capacitor directly across each motor's terminals.
- Keep the WT901, Hall cables, and USB cable away from motor leads and the L298N heatsink.
- Before attaching motors, verify GPIO control voltages and confirm ENA/ENB remain low during boot.
- For the first powered motion test, use a current-limited motor supply, low PWM, and immediate access to the emergency stop.
