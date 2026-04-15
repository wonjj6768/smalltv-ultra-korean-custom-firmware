# GeekMagic Open Firmware

Open firmware for ESP8266-based GeekMagic displays such as **HelloCubic Lite** and **SmallTV-Ultra**.

This fork is based on [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware) and remains licensed under **GPL-3.0-or-later**.

## What This Fork Focuses On

- Korean-first clock and weather dashboard
- Cleaner local web UI
- Korean region search for weather location setup
- Refined air quality and forecast presentation
- OTA bridge for stock firmware upgrades
- `host-exact` BMP renderer for local preview checks

## Build

Firmware:

```powershell
py -m platformio run
```

LittleFS:

```powershell
py -m platformio run -t buildfs
```

OTA bridge firmware:

```powershell
py -m platformio run -d bridge-firmware
```

Build output:

- `.pio/build/esp12e/firmware.bin`
- `.pio/build/esp12e/littlefs.bin`
- `bridge-firmware/.pio/build/esp12e-bridge/firmware.bin`

## Flash Layout

- `0x00000000` -> `firmware.bin`
- `0x00200000` -> `littlefs.bin`

## Upgrade From Stock Firmware

1. Upload `bridge-firmware/.pio/build/esp12e-bridge/firmware.bin` through the stock OTA page.
2. Open `http://192.168.4.1/bridgeupdate` and upload main `firmware.bin`.
3. After the main firmware boots, upload `littlefs.bin` from the main firmware update page.

## Web UI

The device serves its local web UI from `data/web`.

This fork includes:

- local dashboard settings
- Wi-Fi and update pages
- Korean weather region search

## Host-Exact Preview

`host-exact` renders the dashboard to BMP files without flashing the device.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear
```

Presets:

- `clear`
- `rain`
- `fog`
- `aq-korea`
- `fallback-text`

## License

See `LICENSE`.

## Upstream

- [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
