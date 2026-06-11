# 개발 메모

이 문서는 개발할 때 반복해서 쓰는 명령과 코드 위치만 정리합니다. 설치 절차는 [플래싱 가이드](flash-guide.md), 공개 안내는 [README](../README.md)를 기준으로 합니다.

## 빌드

```sh
py -m platformio run
py -m platformio run --target buildfs
py -m platformio run -d tools/bridge-firmware
```

생성 파일:

- `.pio/build/esp12e/firmware.bin`
- `.pio/build/esp12e/littlefs.bin`
- `tools/bridge-firmware/.pio/build/esp12e-bridge/firmware.bin`

## 웹 UI

- 소스 위치: `data/web`
- LittleFS 빌드 시 `scripts/gzip_web_assets.py`가 gzip 파일을 생성합니다.
- 웹 UI를 수정했으면 `py -m platformio run --target buildfs` 후 `littlefs.bin`을 기기에 올립니다.

## 날씨

날씨는 기상청 APIHub만 사용합니다.

- 현재 칸: 초단기실황 `getUltraSrtNcst`
- 이후 예보 칸: 초단기예보 `getUltraSrtFcst`
- 오늘 최고기온: 단기예보 `getVilageFcst`의 `TMX`
- API 키는 웹 UI에서 사용자가 직접 입력합니다.
- 개인 API 키를 커밋하지 않습니다.

관련 코드:

- `src/weather/`
- `src/config/`
- `data/web/`

## 디스플레이

- 디스플레이 레이아웃: `src/display/DisplayManager.cpp`
- 숫자 폰트: `src/display/ClockDigitFont.cpp`
- 한국어 UI 폰트: `src/display/UiTextFont.cpp`
- 날씨 아이콘: `data/weather-icons`

호스트 렌더러:

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear
```

프리셋:

- `clear`
- `rain`
- `fog`
- `aq-korea`
- `fallback-text`

## 폰트

- 시계 숫자는 포함된 `Rajdhani-Bold.ttf`를 사용합니다.
- 한국어 UI 폰트는 생성된 `src/display/UiTextFont.cpp`를 커밋해 둡니다.
- 한국어 폰트를 재생성할 때는 `Noto Sans KR`만 사용합니다.

## 주요 기기 엔드포인트

- `http://<device-ip>/index.html`
- `http://<device-ip>/update.html`
- `http://<device-ip>/legacyupdate`
- `http://<device-ip>/api/v1/weather/config`
- `http://<device-ip>/api/v1/weather/status`
- `http://<device-ip>/api/v1/weather/refresh`
- `http://<device-ip>/api/v1/display/brightness`
- `http://<device-ip>/api/v1/ota/fw`
- `http://<device-ip>/api/v1/ota/fs`
