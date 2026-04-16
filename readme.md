# GeekMagic Open Firmware

Open firmware for ESP8266-based GeekMagic displays such as **HelloCubic Lite** and **SmallTV-Ultra**.

This fork is based on [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware) and remains licensed under **GPL-3.0-or-later**.

![Dashboard Preview](.github/assets/dashboard-preview-host-exact.png)

Current dashboard preview generated from `host-exact`.

## What This Fork Focuses On

- Korean-first clock and weather dashboard
- Cleaner local web UI
- Korean region search for weather location setup
- Refined air quality and forecast presentation
- OTA bridge for stock firmware upgrades
- `host-exact` BMP renderer for local preview checks

## Releases

Prebuilt binaries are attached to GitHub Releases:

- `bridge-firmware.bin`
- `firmware.bin`
- `littlefs.bin`
- `SHA256SUMS.txt`

## Install

From stock firmware:

1. Upload `bridge-firmware.bin` from the stock OTA page.
2. Connect to the `GeekMagic` AP and open `http://192.168.4.1/bridgeupdate`.
3. Upload `firmware.bin`.
4. After reboot, upload `littlefs.bin` from the main firmware update page.

Direct flash:

1. Flash `firmware.bin` to `0x00000000`.
2. Flash `littlefs.bin` to `0x00200000`.
3. If Wi-Fi is not configured, connect to the `GeekMagic` AP and open `http://192.168.4.1/`.

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

Release bundle:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/export_release_artifacts.ps1
```

Build output:

- `.pio/build/esp12e/firmware.bin`
- `.pio/build/esp12e/littlefs.bin`
- `bridge-firmware/.pio/build/esp12e-bridge/firmware.bin`
- `release/firmware.bin`
- `release/littlefs.bin`
- `release/bridge-firmware.bin`
- `release/SHA256SUMS.txt`

## Flash Layout

- `0x00000000` -> `firmware.bin`
- `0x00200000` -> `littlefs.bin`

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
