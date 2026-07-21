# LTW8 Heat Pilot

Firmware for the Waveshare ESP32-S3-POE-ETH-8DI-8DO, built with PlatformIO and
the Arduino framework.

## Current state

The firmware starts in `disabled` mode, keeps all physical outputs off, and
reports its state every two seconds. Logs are available over native USB and,
after DHCP succeeds, over TCP.

## Build

```sh
pio run
```

## Tests

Run the hardware-independent control, timing, interlock, output encoding, and
API validation tests on the development machine:

```sh
pio test -e native
```

The tests use simulated timestamps and therefore verify 30, 60, and 90 second
behavior without real waiting or a connected board.

## Upload

Connect the board through its USB-C port and run:

```sh
pio run --target upload
```

If automatic flashing does not start, hold **BOOT**, briefly press **RESET**,
release **RESET**, and then release **BOOT**.

## Serial monitor

```sh
pio device monitor
```

The monitor uses 115200 baud. Expected output:

```text
LTW8 Heat Pilot
Firmware started; all outputs are OFF.
[status] uptime_ms=1500 mode=disabled state=disabled heater_phases=0 pump=0 outputs_healthy=1 manual_timeout_ms=0
```

## Ethernet and network log

The W5500 obtains its address through DHCP and announces the hostname
`heat-pilot`. Connect to the log stream with:

```sh
pio device monitor --port socket://heat-pilot.local:23
```

Only one network log client can be connected at a time. USB logging remains
available in parallel.

## OTA upload

The OTA-capable firmware must be uploaded through USB once. Subsequent updates
can be sent through Ethernet with:

```sh
pio run -e waveshare_esp32_s3_ota --target upload
```

The current development password is `heat-pilot-dev`. Change it in both
`include/Config.h` and `platformio.ini` before permanent deployment. USB upload
remains available as a recovery path.

## REST API

Read the current state:

```sh
curl http://heat-pilot.local/api/v1/status
```

DS18B20-compatible temperature sensors are connected in parallel to GPIO1
(`IO1`) with one pull-up resistor from `IO1` to 3.3 V. The status response lists
their unique addresses and readings in `temperature_sensors`. Sensor positions
such as buffer top, middle, and bottom will be assigned to these addresses after
installation.

The onboard isolated RS485 interface runs as a strictly receive-only Modbus RTU
sniffer on GPIO18 at 9600 baud, 8N1. GPIO21 is held in receive mode and no UART
TX pin is assigned. Sniffer counters and the four most recent frames are exposed
under `modbus_sniffer` in the status response. The sniffer is diagnostic only;
captured data does not yet influence control. When attached in parallel to an
existing bus, termination must match the existing topology. In the current
installation the Heat Pilot replaces a terminated Ohmpilot endpoint, so its
120-ohm termination remains enabled.

Decoded Fronius Smart Meter TS 65A-3 observations are exposed under
`smart_meter`. A negative `grid_power_w` means grid export; the corresponding
positive value is reported as `observed_surplus_w`. These observations are not
connected to automatic output control only while a valid reading not older than
three seconds is available. A timeout immediately stops heating and retains the
pump overrun.

Set the manual outputs (heater phases 0 to 3, pump true or false):

```sh
curl -X PUT http://heat-pilot.local/api/v1/manual-output \
  -H 'Content-Type: application/json' \
  -d '{"heater_phases":2,"pump":true}'
```

The three heater phases map to DO1 through DO3 and are enabled in order. The
pump maps to DO4. The pump is mandatory while a heater phase is active. Sending
zero phases and `pump: false` switches the heater off and starts the 90-second
pump overrun. Any active manual command times out after 60 seconds.
The API is currently intended only for testing on a trusted local network and
does not yet require authentication.

Set simulated measurements (positive surplus means exported power):

```sh
curl -X PUT http://heat-pilot.local/api/v1/simulation \
  -H 'Content-Type: application/json' \
  -d '{"surplus_w":5000}'
```

Temperature is always taken from the physical 1-Wire sensors and cannot be
overridden through the simulation endpoint. Heating is inhibited until all
detected sensors have produced two consecutive valid readings. Missing,
invalid, or stale readings stop the heater immediately and retain the pump
overrun.

The simulation overrides the physical Smart Meter until it is disabled again:

```sh
curl -X PUT http://heat-pilot.local/api/v1/simulation \
  -H 'Content-Type: application/json' \
  -d '{"enabled":false}'
```

Enable automatic mode:

```sh
curl -X PUT http://heat-pilot.local/api/v1/mode \
  -H 'Content-Type: application/json' \
  -d '{"mode":"automatic"}'
```

Control constants are collected in `include/Config.h`: each heater phase is
1500 W, a phase needs 1700 W to switch on and drops below 1300 W, and a new
condition must remain stable for 30 seconds. The target temperature is 80 C;
heating is released again at 76 C. The pump overruns for 90 seconds.
