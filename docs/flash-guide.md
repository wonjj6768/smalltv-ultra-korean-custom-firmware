# 플래싱 가이드

이 문서는 설치와 복구 절차만 다룹니다. 기능 소개, API 키 설정, 라이선스 정보는 [README](../README.md)를 기준으로 관리합니다.

## 준비 파일

[최신 릴리즈](https://github.com/wonjj6768/smalltv-ultra-korean-custom-firmware/releases/latest)에서 아래 파일을 받습니다.

- `bridge-firmware.bin`
- `firmware.bin`
- `littlefs.bin`
- `SHA256SUMS.txt`

## 순정 펌웨어에서 설치

순정 OTA 페이지는 메인 `firmware.bin`을 직접 받지 못합니다. 이 경로에서는 브릿지 펌웨어가 필수입니다.

1. 순정 OTA 페이지에서 `bridge-firmware.bin`을 업로드합니다.
2. 재부팅 후 `GeekMagic` AP에 접속합니다.
3. 브라우저에서 `http://192.168.4.1/bridgeupdate`를 엽니다.
4. `firmware.bin`을 업로드합니다.
5. 재부팅 후 기기 웹 UI를 엽니다.
6. 업데이트 페이지에서 `littlefs.bin`을 업로드합니다.

## 커스텀 펌웨어에서 업데이트

이미 이 펌웨어가 설치되어 있으면 기기 웹 UI에서 업데이트합니다.

- 일반 업데이트: `http://<device-ip>/update.html`
- 레거시 업데이트: `http://<device-ip>/legacyupdate`

웹 UI, 아이콘, 지역 데이터가 바뀐 릴리즈라면 `firmware.bin`과 `littlefs.bin`을 모두 올립니다. 업데이트 중에는 브라우저 탭을 닫지 마세요.

## 시리얼 직접 플래싱

웹 업데이트가 불가능할 때만 사용합니다.

- `firmware.bin` -> `0x00000000`
- `littlefs.bin` -> `0x00200000`

## 첫 부팅

Wi-Fi가 설정되지 않았으면 AP 모드로 시작합니다.

- AP SSID: `GeekMagic`
- 비밀번호: 없음
- 설정 주소: `http://192.168.4.1/`

디스플레이에 `flash littlefs.bin` 문구가 보이거나 웹 UI가 깨져 있으면 `littlefs.bin`을 다시 업로드합니다.

## 복구 기준

- 기기가 부팅되고 IP에 접속되면 먼저 `http://<device-ip>/legacyupdate`를 시도합니다.
- 웹 UI만 깨졌으면 `littlefs.bin`만 다시 올립니다.
- 펌웨어가 정상 부팅하지 않으면 시리얼 다운로드 모드로 직접 플래싱합니다.
