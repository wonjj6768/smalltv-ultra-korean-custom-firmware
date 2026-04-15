# OTA Artifacts

This folder is used for locally exported OTA artifacts for the GeekMagic firmware.

Generated `.bin` files and checksum files are intentionally not tracked in git. Attach them to a GitHub Release instead.

Files:

- `firmware-latest.bin`: upload this first to the original `/update` page
- `littlefs-latest.bin`: upload this second to `/legacyupdate`
- `SHA256SUMS.txt`: checksums for the exported files

To regenerate after a new build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\export_ota_artifacts.ps1
```
