# LTW8 Heat Pilot

Firmware for the Waveshare ESP32-S3-POE-ETH-8DI-8DO, built with PlatformIO and
the Arduino framework.

## Current state

The initial firmware starts in `disabled` mode, keeps all logical outputs off,
and reports its state over native USB every two seconds. Physical output
control is intentionally not part of this first milestone.

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
