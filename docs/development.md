# Development Notes / 개발 문서

This document keeps the day-to-day development workflow in one place.

이 문서는 개발 중 반복해서 쓰는 명령과 기준을 정리합니다.

## Build / 빌드

Main firmware:

메인 펌웨어:

```sh
py -m platformio run
```

LittleFS image:

LittleFS 이미지:

```sh
py -m platformio run --target buildfs
```

Bridge firmware:

브릿지 펌웨어:

```sh
py -m platformio run -d tools/bridge-firmware
```

## Web UI / 웹 UI

Web files live in `data/web`.

웹 파일은 `data/web`에 있습니다.

- Edit HTML, CSS, and JS in `data/web`.
- Run `py -m platformio run --target buildfs`.
- Upload the new `littlefs.bin` to the device.

- `data/web`의 HTML, CSS, JS를 수정합니다.
- `py -m platformio run --target buildfs`를 실행합니다.
- 새 `littlefs.bin`을 기기에 업로드합니다.

Gzip assets are generated during the file system build by `scripts/gzip_web_assets.py`.

파일 시스템 빌드 중 `scripts/gzip_web_assets.py`가 gzip 자산을 생성합니다.

## Host Preview / 호스트 미리보기

Use the host renderer when checking the display layout before flashing.

플래싱 전에 디스플레이 레이아웃을 확인할 때 호스트 렌더러를 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear
```

Available presets / 사용 가능한 프리셋:

- `clear`
- `rain`
- `fog`
- `aq-korea`
- `fallback-text`

The host renderer is for display regression checks. Final confirmation should still be done on the real device.

호스트 렌더러는 디스플레이 회귀 확인용입니다. 최종 확인은 실제 기기에서 합니다.

## Weather / 날씨

Weather data is configured from the local web UI.

날씨 데이터는 로컬 웹 UI에서 설정합니다.

- Source: KMA APIHub / 소스: 기상청 APIHub
- API key is user-provided / API 키는 사용자가 직접 입력합니다.
- Region lookup data is stored under `data/web` / 지역 검색 데이터는 `data/web` 아래에 있습니다.

Do not commit a private API key.

개인 API 키를 커밋하지 않습니다.

## Fonts / 폰트

- Clock digit font uses bundled Rajdhani Bold / 시계 숫자는 포함된 Rajdhani Bold를 사용합니다.
- Korean UI text font is generated and committed as `src/display/UiTextFont.cpp` / 한국어 UI 폰트는 `src/display/UiTextFont.cpp`로 생성 후 커밋되어 있습니다.
- If regenerating Korean UI text, use `Noto Sans KR` only / 한국어 UI 폰트를 다시 생성할 때는 `Noto Sans KR`만 사용합니다.

## Useful Device Endpoints / 기기 엔드포인트

- `http://<device-ip>/index.html`: main web UI / 메인 웹 UI
- `http://<device-ip>/update.html`: update page / 업데이트 페이지
- `http://<device-ip>/legacyupdate`: legacy update page / 레거시 업데이트 페이지
- `http://<device-ip>/api/v1/weather/config`: weather config / 날씨 설정
- `http://<device-ip>/api/v1/weather/status`: cached weather state / 캐시된 날씨 상태
- `http://<device-ip>/api/v1/weather/refresh`: weather refresh / 날씨 갱신
- `http://<device-ip>/api/v1/ota/fw`: firmware OTA upload / 펌웨어 OTA 업로드
- `http://<device-ip>/api/v1/ota/fs`: LittleFS OTA upload / LittleFS OTA 업로드

## Release / 릴리즈

Use `docs/release-checklist.md` before publishing a GitHub release.

GitHub 릴리즈 전에는 `docs/release-checklist.md`를 사용합니다.

