# smalltv-ultra-korean-custom-firmware

Korean-first custom firmware for the ESP8266-based **SmallTV-Ultra**.

ESP8266 기반 **SmallTV-Ultra**를 위한 한국어 중심 커스텀 펌웨어입니다.

Repository / 저장소: [wonjj6768/smalltv-ultra-korean-custom-firmware](https://github.com/wonjj6768/smalltv-ultra-korean-custom-firmware)

Based on / 원본 기반: [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)

## Preview / 미리보기

![Clear Dashboard](.github/assets/dashboard-preview-clear.png)
![Rain Dashboard](.github/assets/dashboard-preview-rain.png)
![Air Quality Dashboard](.github/assets/dashboard-preview-air.png)

## Features / 주요 기능

- Korean clock and weather dashboard tuned for SmallTV-Ultra / SmallTV-Ultra 화면에 맞춘 한국어 시계/날씨 대시보드
- KMA APIHub weather support with Korean region lookup / 기상청 APIHub 기반 날씨와 한국 지역 검색 설정
- Hourly forecast row with temperature, precipitation amount, and humidity / 시간별 예보에 온도, 강수량, 습도 표시
- Local web UI for Wi-Fi, time, weather, update, GIF, logs, and reset / 와이파이, 시간, 날씨, 업데이트, GIF, 로그, 초기화를 위한 로컬 웹 UI

## Supported Device / 지원 기기

This firmware is intended for **SmallTV-Ultra only**.

이 펌웨어는 **SmallTV-Ultra 전용**으로 정리했습니다.

## Release Files / 릴리즈 파일

Download the files from [GitHub Releases](https://github.com/wonjj6768/smalltv-ultra-korean-custom-firmware/releases).

[GitHub Releases](https://github.com/wonjj6768/smalltv-ultra-korean-custom-firmware/releases)에서 아래 파일을 받습니다.

- `bridge-firmware.bin`: OTA bridge for stock firmware upload limits / 순정 펌웨어 OTA 용량 제한을 넘기기 위한 중간 펌웨어
- `firmware.bin`: main firmware / 실제 메인 펌웨어
- `littlefs.bin`: web UI, icons, and data files / 웹 UI, 아이콘, 데이터 파일
- `SHA256SUMS.txt`: release file checksums / 릴리즈 파일 체크섬

## Install From Stock Firmware / 순정 펌웨어에서 설치

Use this path when the stock OTA page refuses large firmware files.

순정 OTA 페이지가 큰 펌웨어 파일을 받지 못할 때 이 방법을 사용합니다.

1. Upload `bridge-firmware.bin` from the stock OTA page.
2. Connect to the `GeekMagic` AP created by the bridge firmware.
3. Open `http://192.168.4.1/bridgeupdate`.
4. Upload `firmware.bin`.
5. After reboot, open the main web UI and upload `littlefs.bin` from the update page.

1. 순정 OTA 페이지에서 `bridge-firmware.bin`을 업로드합니다.
2. 브릿지 펌웨어가 만든 `GeekMagic` AP에 접속합니다.
3. `http://192.168.4.1/bridgeupdate`를 엽니다.
4. `firmware.bin`을 업로드합니다.
5. 재부팅 후 메인 웹 UI의 업데이트 페이지에서 `littlefs.bin`을 업로드합니다.

## Direct Flash / 직접 플래싱

Use this only when flashing by serial/download mode.

시리얼 다운로드 모드로 직접 플래싱할 때만 사용합니다.

1. Flash `firmware.bin` to `0x00000000`.
2. Flash `littlefs.bin` to `0x00200000`.
3. If Wi-Fi is not configured, connect to the `GeekMagic` AP and open `http://192.168.4.1/`.

1. `firmware.bin`을 `0x00000000`에 플래싱합니다.
2. `littlefs.bin`을 `0x00200000`에 플래싱합니다.
3. 와이파이가 설정되지 않은 경우 `GeekMagic` AP에 접속한 뒤 `http://192.168.4.1/`을 엽니다.

## First Setup / 초기 설정

Open the device web UI after installation.

설치 후 기기 웹 UI를 엽니다.

- Configure Wi-Fi / 와이파이를 설정합니다.
- Configure time/NTP if needed / 필요한 경우 시간/NTP를 설정합니다.
- Open Dashboard settings and enter your own KMA APIHub key / Dashboard 설정에서 본인의 기상청 APIHub 인증키를 입력합니다.
- Search and apply a Korean weather region such as `춘천시` or `서울시` / `춘천시`, `서울시` 같은 한국 지역명을 검색해서 적용합니다.

The public firmware does not include a private KMA API key.

공개 펌웨어에는 개인 기상청 API 인증키가 포함되어 있지 않습니다.

## Documentation / 문서

- [Flash Guide](docs/flash-guide.md) / 플래싱 및 복구 절차
- [Development Notes](docs/development.md) / 개발, 빌드, 호스트 미리보기
- [Release Checklist](docs/release-checklist.md) / 릴리즈 체크리스트

## Build / 빌드

Requires PlatformIO.

PlatformIO가 필요합니다.

```sh
pio run
pio run -t buildfs
pio run -d tools/bridge-firmware
```

## Project Tools / 프로젝트 도구

- `tools/bridge-firmware`: small OTA bridge firmware / 순정 OTA 제한 우회용 중간 펌웨어
- `tools/host-exact`: local display preview renderer / 로컬 디스플레이 프리뷰 렌더러

## Third-Party Assets / 서드파티 자산

- Firmware source code is released under `GPL-3.0-or-later` / 펌웨어 소스 코드는 `GPL-3.0-or-later`로 배포됩니다.
- `tools/fontgen/assets/fonts/rajdhani/Rajdhani-Bold.ttf` is bundled under the `SIL Open Font License 1.1` / `Rajdhani-Bold.ttf`는 `SIL Open Font License 1.1`로 포함되어 있습니다.
- `src/display/UiTextFont.cpp` is generated and committed. If regenerated, use `Noto Sans KR` / `UiTextFont.cpp`는 생성 후 커밋된 파일입니다. 재생성 시 `Noto Sans KR`만 사용합니다.
- `data/web/css/pico.min.css` is `MIT` licensed / `pico.min.css`는 `MIT` 라이선스입니다.
- `data/weather-icons/*.bmp` are generated from [`basmilius/weather-icons`](https://github.com/basmilius/weather-icons). See `data/weather-icons/LICENSE.txt` / 날씨 아이콘은 `basmilius/weather-icons`에서 생성했으며 `data/weather-icons/LICENSE.txt`를 참고하세요.

## License and Upstream / 라이선스와 원본

- License / 라이선스: `GPL-3.0-or-later`
- License file / 라이선스 파일: `LICENSE`
- Upstream / 원본: [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
