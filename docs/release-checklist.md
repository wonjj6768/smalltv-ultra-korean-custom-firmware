# Release Checklist / 릴리즈 체크리스트

This document is the release checklist for this repository.

이 문서는 이 저장소를 릴리즈할 때 따라가는 체크리스트입니다.

## Before Release / 릴리즈 전 확인

- Working tree is clean / 작업트리가 깨끗한지 확인합니다.
- README preview images are current / README 미리보기 이미지가 최신인지 확인합니다.
- No private API key is committed / 개인 API 키가 커밋되지 않았는지 확인합니다.
- Release files are built from the current commit / 릴리즈 파일은 현재 커밋에서 빌드합니다.

```sh
git status --short
```

## Build / 빌드

```sh
py -m platformio run
py -m platformio run --target buildfs
py -m platformio run -d tools/bridge-firmware
```

Expected output files / 생성되는 파일:

- `.pio/build/esp12e/firmware.bin`
- `.pio/build/esp12e/littlefs.bin`
- `tools/bridge-firmware/.pio/build/esp12e-bridge/firmware.bin`

## Prepare Release Files / 릴리즈 파일 정리

Release artifacts should be placed in `artifacts/release`.

릴리즈 파일은 `artifacts/release`에 정리합니다.

```powershell
$releaseDir = Join-Path (Get-Location) 'artifacts\release'
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

Copy-Item '.pio\build\esp12e\firmware.bin' "$releaseDir\firmware.bin" -Force
Copy-Item '.pio\build\esp12e\littlefs.bin' "$releaseDir\littlefs.bin" -Force
Copy-Item 'tools\bridge-firmware\.pio\build\esp12e-bridge\firmware.bin' "$releaseDir\bridge-firmware.bin" -Force
```

Generate checksums / 체크섬 생성:

```powershell
$files = @('bridge-firmware.bin', 'firmware.bin', 'littlefs.bin')
$lines = foreach ($file in $files) {
  $path = Join-Path $releaseDir $file
  $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
  "$hash  $file"
}
Set-Content -LiteralPath (Join-Path $releaseDir 'SHA256SUMS.txt') -Value $lines -Encoding ascii
```

Release files / 릴리즈 파일:

- `bridge-firmware.bin`
- `firmware.bin`
- `littlefs.bin`
- `SHA256SUMS.txt`

## Preview Images / 미리보기 이미지

If the display layout changed, regenerate preview images.

디스플레이 레이아웃이 바뀌면 미리보기 이미지를 다시 생성합니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear -Output artifacts/output/dashboard-preview-clear.bmp
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset rain -Output artifacts/output/dashboard-preview-rain.bmp
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset aq-korea -Output artifacts/output/dashboard-preview-air.bmp
```

Convert the BMP files to PNG and replace:

BMP를 PNG로 변환해서 아래 파일을 교체합니다.

- `.github/assets/dashboard-preview-clear.png`
- `.github/assets/dashboard-preview-rain.png`
- `.github/assets/dashboard-preview-air.png`

## Commit and Push / 커밋 및 푸시

```sh
git add README.md docs .github/assets
git commit -m "Prepare release"
git push origin HEAD:main
```

If only documentation changed, do not rebuild or re-upload binary release assets.

문서만 바뀐 경우 바이너리 릴리즈 파일은 다시 만들거나 업로드하지 않아도 됩니다.

## Tag and GitHub Release / 태그 및 GitHub 릴리즈

Use a date-based tag.

날짜 기반 태그를 사용합니다.

```sh
git tag -f vYYYY.MM.DD HEAD
git push --force origin refs/tags/vYYYY.MM.DD
```

Upload release assets:

릴리즈 파일 업로드:

```sh
gh release upload vYYYY.MM.DD artifacts/release/bridge-firmware.bin artifacts/release/firmware.bin artifacts/release/littlefs.bin artifacts/release/SHA256SUMS.txt --repo wonjj6768/smalltv-ultra-korean-custom-firmware --clobber
```

## Verify / 검증

```sh
gh run list --repo wonjj6768/smalltv-ultra-korean-custom-firmware --limit 4
gh release view vYYYY.MM.DD --repo wonjj6768/smalltv-ultra-korean-custom-firmware --json tagName,url,assets,targetCommitish
git ls-remote origin refs/heads/main refs/tags/vYYYY.MM.DD
```

Device smoke test / 기기 확인:

- Web UI opens / 웹 UI가 열리는지 확인합니다.
- Firmware upload works / 펌웨어 업로드가 되는지 확인합니다.
- LittleFS upload works / LittleFS 업로드가 되는지 확인합니다.
- Weather settings save and refresh / 날씨 설정 저장과 갱신이 되는지 확인합니다.

