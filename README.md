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
available in parallel. Network log output is buffered and written by a separate
task, so a slow or disconnected log client cannot block the control loop.

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

Open `http://heat-pilot.local/` for the lightweight, mobile-friendly web
interface. It shows the current energy, battery, temperature, heater, and pump
state and allows switching between disabled and automatic mode. Manual output
and simulation controls deliberately remain API-only. Status polling permits
only one in-flight browser request and aborts it after 1.5 seconds.

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
under `modbus_sniffer` in the status response. Raw frames are diagnostic; only
CRC-valid, recognized Smart Meter and battery responses influence control. When
attached in parallel to an existing bus, termination must match the existing
topology. In the current
installation the Heat Pilot replaces a terminated Ohmpilot endpoint, so its
120-ohm termination remains enabled.

RTU frames are separated using their function-specific length and CRC. The
inter-frame timing gap remains as a fallback for corrupt or unknown frames, so
request and response can still be separated after temporary application stalls.

Decoded Fronius Smart Meter TS 65A-3 observations are exposed under
`smart_meter`. A negative `grid_power_w` means grid export; the corresponding
positive value is reported as `observed_surplus_w`. These observations are not
connected to automatic output control only while a valid reading not older than
three seconds is available. A timeout immediately stops heating and retains the
pump overrun.

The BYD HVS+ status block is decoded from Modbus unit 21. Battery state of
charge, power, voltage, and capacity are exposed under `battery`. Automatic
heating requires battery data not older than 12 seconds. Up to 500 W of battery
discharge is tolerated transiently for at most 15 seconds and no more than 2 Wh;
exceeding either limit removes one heater phase. A value above 500 W removes one
phase immediately. Another phase can only be removed after a new battery sample.

The `modbus_sniffer` status includes CRC and overflow counters as well as
`smart_meter_timeouts` and `battery_timeouts`. These counters are intended for
OTLP export.

Metrics are exported every 15 seconds as OTLP/HTTP JSON to the endpoint in
`config::telemetry`. NTP timestamps come from `pool.ntp.org`; control timers
continue to use the monotonic `millis()` clock. Export runs in a separate task,
so an unavailable collector cannot delay control or Modbus processing. The
status response exposes exporter counters and runtime diagnostics under
`telemetry` and `diagnostics`.

Runtime diagnostics separately track maximum durations for network,
application, HTTP, and telemetry processing. Loop iterations above 100 ms are
counted as stalls. The five-second ESP32 task watchdog resets a blocked main
loop; the reset reason and active stage are retained and reported after reboot.
Output initialization now happens immediately at startup without waiting for a
USB serial connection.

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

Control constants are collected in `include/Config.h`: each heater phase uses
the Ohmpilot-derived estimate of 1625 W. A phase needs 1700 W to switch on and
drops below 1300 W, and a new
condition must remain stable for 30 seconds. The target temperature is 80 C;
heating is released again at 76 C. The pump overruns for 90 seconds.
