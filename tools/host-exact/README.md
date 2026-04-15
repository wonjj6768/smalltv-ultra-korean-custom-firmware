# host-exact

`host-exact` is the pixel-match host renderer for the dashboard.

## What it does

- Compiles the real firmware display sources
- Compiles the real `Arduino_GFX.cpp`, `Arduino_G.cpp`, and `Arduino_Canvas_Indexed.cpp`
- Uses a host framebuffer backend only for output storage and BMP export
- Keeps LittleFS assets pointed at the real `data/` tree

This means fallback ASCII text, `getTextBounds()`, wrapping, indexed canvas flushing, and icon drawing all run through the same `Arduino_GFX` core logic as the device build.

## What is still host-only

- `Arduino_HWSPI` is a no-op transport stub
- `Arduino_ST7789` is a host framebuffer surface instead of a real SPI panel
- `ConfigManager` and `WeatherClient` remain host adapters so presets can seed deterministic scenes

## Presets

- `clear`
- `rain`
- `fog`
- `aq-korea`
- `fallback-text`

## Usage

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear
```

Output BMPs are written to `/output/display-manager-host-exact-<preset>.bmp`.
