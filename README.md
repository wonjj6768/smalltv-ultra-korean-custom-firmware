# smalltv-ultra-korean-custom-firmware

**SmallTV-Ultra 전용 한국어 커스텀 펌웨어**입니다.

원본 프로젝트 [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)를 기반으로, 한국어 시계/날씨 표시와 국내 사용 흐름에 맞춰 정리했습니다.

## 미리보기

![맑음 화면](.github/assets/dashboard-preview-clear.png)
![비 화면](.github/assets/dashboard-preview-rain.png)
![대기질 화면](.github/assets/dashboard-preview-air.png)

## 주요 기능

- SmallTV-Ultra 화면에 맞춘 한국어 시계/날씨 대시보드
- 기상청 APIHub 기반 현재 실황, 초단기예보, 오늘 최고기온 표시
- 온도, 강수량, 습도, 미세먼지, 오존 표시
- 로컬 웹 UI에서 Wi-Fi, 시간, 지역, 밝기, 업데이트 설정
- 순정 펌웨어에서 넘어오기 위한 브릿지 펌웨어 제공

## 지원 기기

- **SmallTV-Ultra만 지원합니다.**

## 릴리즈 파일

[GitHub Releases](https://github.com/wonjj6768/smalltv-ultra-korean-custom-firmware/releases/latest)에서 받습니다.

- `bridge-firmware.bin`: 순정 펌웨어 OTA 제한을 넘기기 위한 중간 펌웨어
- `firmware.bin`: 메인 펌웨어
- `littlefs.bin`: 웹 UI, 아이콘, 지역 데이터
- `SHA256SUMS.txt`: 릴리즈 파일 체크섬

## 설치 요약

순정 펌웨어 OTA 페이지는 메인 `firmware.bin`을 직접 받지 못합니다. **순정 펌웨어에서 설치할 때는 반드시 `bridge-firmware.bin`을 먼저 올린 뒤 메인 펌웨어로 넘어가야 합니다.**

1. 순정 OTA 페이지에서 `bridge-firmware.bin` 업로드
2. 기기가 만든 `GeekMagic` AP에 접속
3. `http://192.168.4.1/bridgeupdate`에서 `firmware.bin` 업로드
4. 재부팅 후 기기 웹 UI에서 `littlefs.bin` 업로드

이미 이 커스텀 펌웨어가 설치된 기기는 웹 UI의 업데이트 페이지에서 `firmware.bin`과 `littlefs.bin`을 업데이트하면 됩니다. 업데이트 중에는 브라우저 탭을 닫지 마세요.

자세한 설치/복구 절차는 [플래싱 가이드](docs/flash-guide.md)를 보세요.

## 초기 설정

Wi-Fi가 설정되지 않았으면 기기가 AP 모드로 시작합니다.

- AP SSID: `GeekMagic`
- 비밀번호: 없음
- 설정 주소: `http://192.168.4.1/`

웹 UI에서 Wi-Fi, 시간, Dashboard, 지역, 기상청 API 키를 설정합니다.

## 기상청 APIHub 설정

공개 펌웨어에는 개인 API 키가 들어있지 않습니다. 날씨를 사용하려면 본인의 기상청 APIHub 키를 입력해야 합니다.

1. [기상청 APIHub](https://apihub.kma.go.kr/)에서 `authKey`를 발급받습니다.
2. APIHub에서 아래 항목을 **API 활용신청**합니다.
3. 기기 웹 UI의 `Dashboard` 설정에 발급받은 키를 입력합니다.
4. `Validate`로 키를 확인한 뒤 지역을 저장합니다.

필요한 APIHub 신청 항목:

- `4. 동네예보(초단기실황·초단기예보·단기예보) 조회`
- `4.1 초단기실황조회`
- `4.2 초단기예보조회`
- `4.3 단기예보조회`

사용하는 서비스 경로:

- `VilageFcstInfoService_2.0/getUltraSrtNcst`
- `VilageFcstInfoService_2.0/getUltraSrtFcst`
- `VilageFcstInfoService_2.0/getVilageFcst`

`지상관측`, `종관기상관측(ASOS)`, `방재기상관측(AWS)`는 이 펌웨어에 필요하지 않습니다.

## 문서

- [플래싱 가이드](docs/flash-guide.md): 설치, 업데이트, 복구 절차
- [개발 메모](docs/development.md): 빌드, 웹 UI, 호스트 렌더러
- [릴리즈 체크리스트](docs/release-checklist.md): 공개 릴리즈 전 확인 순서

## 개발

로컬 빌드, 웹 UI 수정, 호스트 렌더러 사용법은 [개발 메모](docs/development.md)에만 정리합니다.

## 면책

이 펌웨어는 SmallTV-Ultra용 비공식 커스텀 펌웨어입니다. GeekMagic, Times-Z, 원본 펌웨어 개발자, 기상청 또는 날씨 데이터 제공기관과 공식적인 관련이 없습니다.

커스텀 펌웨어 설치는 실패할 수 있으며, 경우에 따라 시리얼 복구가 필요할 수 있습니다. 반드시 SmallTV-Ultra 기기인지 확인한 뒤 본인 책임하에 사용하세요.

날씨 정보는 API 상태, 지역 설정, 제공기관 데이터에 따라 지연되거나 실제와 다를 수 있습니다.

## 라이선스와 자산

- 소스 코드: `GPL-3.0-or-later`
- 원본 프로젝트: [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
- 숫자 폰트 `Rajdhani-Bold.ttf`: `SIL Open Font License 1.1`
- 한국어 UI 폰트 `src/display/UiTextFont.cpp`: `Noto Sans KR` 기반 생성 파일
- 웹 UI CSS `pico.min.css`: `MIT`
- 날씨 아이콘: [`basmilius/weather-icons`](https://github.com/basmilius/weather-icons), 자세한 내용은 `data/weather-icons/LICENSE.txt`
