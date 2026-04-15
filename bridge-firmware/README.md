# OTA Bridge Firmware

Minimal ESP8266 bridge firmware used to get from the stock OTA uploader to this repository's full firmware.

## Purpose

- small enough for the stock OTA size limit
- runs its own access point
- exposes only `/legacyupdate`
- accepts `firmware.bin` only

After the full firmware is installed, LittleFS should be flashed from the main firmware flow.

## Build

```powershell
py -m platformio run -d bridge-firmware
```
