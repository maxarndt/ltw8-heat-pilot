# LTW8 Heat Pilot

Firmware for the Waveshare ESP32-S3-POE-ETH-8DI-8DO, built with PlatformIO and
the Arduino framework.

## Current state

The firmware starts in `disabled` mode, keeps all logical outputs off, and
reports its state every two seconds. Logs are available over native USB and,
after DHCP succeeds, over TCP. Physical output control is not implemented yet.

## Build

```sh
pio run
```

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
[status] uptime_ms=1500 mode=disabled state=disabled heater=000 pump=0
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
