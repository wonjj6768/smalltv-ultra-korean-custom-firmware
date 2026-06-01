# Flash Guide / 플래싱 가이드

This firmware supports **SmallTV-Ultra only**.

이 펌웨어는 **SmallTV-Ultra 전용**입니다.

## Release Files / 릴리즈 파일

Download files from GitHub Releases.

GitHub Releases에서 아래 파일을 받습니다.

- `bridge-firmware.bin`: bridge firmware for stock OTA size limits / 순정 OTA 용량 제한 우회용 브릿지 펌웨어
- `firmware.bin`: main firmware / 메인 펌웨어
- `littlefs.bin`: web UI, icons, and data files / 웹 UI, 아이콘, 데이터 파일
- `SHA256SUMS.txt`: checksums / 체크섬

## From Stock Firmware / 순정 펌웨어에서 설치

Use this path when stock firmware refuses large firmware files.

순정 펌웨어가 큰 펌웨어 파일을 받지 못할 때 이 방법을 사용합니다.

1. Upload `bridge-firmware.bin` from the stock OTA page.
2. Connect to the `GeekMagic` AP.
3. Open `http://192.168.4.1/bridgeupdate`.
4. Upload `firmware.bin`.
5. After reboot, open the main web UI.
6. Upload `littlefs.bin` from the update page.

1. 순정 OTA 페이지에서 `bridge-firmware.bin`을 업로드합니다.
2. `GeekMagic` AP에 접속합니다.
3. `http://192.168.4.1/bridgeupdate`를 엽니다.
4. `firmware.bin`을 업로드합니다.
5. 재부팅 후 메인 웹 UI를 엽니다.
6. 업데이트 페이지에서 `littlefs.bin`을 업로드합니다.

## From Custom Firmware / 커스텀 펌웨어에서 업데이트

Open the device web UI.

기기 웹 UI를 엽니다.

- Main update page / 기본 업데이트 페이지: `http://<device-ip>/update.html`
- Legacy update page / 레거시 업데이트 페이지: `http://<device-ip>/legacyupdate`

Upload both files when the web UI or icons changed.

웹 UI나 아이콘이 바뀐 경우 두 파일을 모두 업로드합니다.

- Firmware: `firmware.bin`
- File system: `littlefs.bin`

## Direct Serial Flash / 시리얼 직접 플래싱

Use this when the web update path is unavailable.

웹 업데이트가 불가능할 때 사용합니다.

- `firmware.bin` -> `0x00000000`
- `littlefs.bin` -> `0x00200000`

## First Boot / 첫 부팅

If Wi-Fi is not configured, the device opens an AP.

와이파이가 설정되지 않았으면 기기가 AP 모드로 진입합니다.

- AP SSID: `GeekMagic`
- Password: none / 비밀번호 없음
- Setup page / 설정 페이지: `http://192.168.4.1/`

If only the legacy update page is available, upload `littlefs.bin` from:

레거시 업데이트 화면만 보이면 아래 주소에서 `littlefs.bin`을 업로드합니다.

```text
http://192.168.4.1/legacyupdate
```

## Weather Setup / 날씨 설정

Open the web UI and configure Dashboard settings.

웹 UI에서 Dashboard 설정을 엽니다.

- Enter your own KMA APIHub key / 본인의 기상청 APIHub 인증키를 입력합니다.
- Search a Korean region such as `서울시` or `춘천시` / `서울시`, `춘천시` 같은 지역을 검색합니다.
- Save settings and refresh weather / 저장 후 날씨를 갱신합니다.

The public firmware does not include a private API key.

공개 펌웨어에는 개인 API 키가 포함되어 있지 않습니다.

## Recovery / 복구

- If the web UI is broken but the device is alive, try `http://<device-ip>/legacyupdate`.
- If only the file system is broken, upload `littlefs.bin` again.
- If the device cannot boot normally, use serial download mode and direct flash.

- 웹 UI만 깨졌고 기기가 살아 있으면 `http://<device-ip>/legacyupdate`를 시도합니다.
- 파일 시스템만 깨졌으면 `littlefs.bin`을 다시 업로드합니다.
- 정상 부팅이 안 되면 시리얼 다운로드 모드로 직접 플래싱합니다.

