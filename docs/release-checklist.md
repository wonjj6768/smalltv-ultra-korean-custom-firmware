# 릴리즈 체크리스트

GitHub 공개 릴리즈를 만들기 전 확인용 문서입니다. 설치 설명은 [README](../README.md)와 [플래싱 가이드](flash-guide.md)에서만 관리합니다.

## 1. 릴리즈 전 확인

- 작업트리가 깨끗한지 확인합니다.
- README 미리보기 이미지가 현재 화면과 맞는지 확인합니다.
- 개인 기상청 API 키가 커밋되지 않았는지 확인합니다.
- 디스플레이, 웹 UI, 업데이트 경로를 실제 기기에서 한 번 확인합니다.

```sh
git status --short
```

## 2. 로컬 빌드

```sh
py -m platformio run
py -m platformio run --target buildfs
py -m platformio run -d tools/bridge-firmware
```

## 3. 미리보기 이미지

디스플레이 레이아웃이 바뀐 경우에만 갱신합니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset clear -Output artifacts/output/dashboard-preview-clear.bmp
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset rain -Output artifacts/output/dashboard-preview-rain.bmp
powershell -ExecutionPolicy Bypass -File tools/host-exact/build-and-run.ps1 -Preset aq-korea -Output artifacts/output/dashboard-preview-air.bmp
```

BMP를 PNG로 변환해서 아래 파일을 교체합니다.

- `.github/assets/dashboard-preview-clear.png`
- `.github/assets/dashboard-preview-rain.png`
- `.github/assets/dashboard-preview-air.png`

## 4. 커밋과 푸시

```sh
git add README.md docs .github/assets
git commit -m "Prepare release"
git push origin HEAD:main
```

문서만 바뀐 경우 새 바이너리 릴리즈는 만들지 않아도 됩니다.

## 5. 태그 릴리즈

태그를 푸시하면 GitHub Actions가 릴리즈 파일을 빌드하고 업로드합니다.

```sh
git tag vYYYY.MM.DD HEAD
git push origin refs/tags/vYYYY.MM.DD
```

생성되는 릴리즈 파일:

- `bridge-firmware.bin`
- `firmware.bin`
- `littlefs.bin`
- `SHA256SUMS.txt`

## 6. 릴리즈 검증

```sh
gh run list --repo wonjj6768/smalltv-ultra-korean-custom-firmware --limit 4
gh release view vYYYY.MM.DD --repo wonjj6768/smalltv-ultra-korean-custom-firmware --json tagName,url,assets,targetCommitish
git ls-remote origin refs/heads/main refs/tags/vYYYY.MM.DD
```

실기기 확인:

- 웹 UI 접속
- `firmware.bin` 업데이트
- `littlefs.bin` 업데이트
- 기상청 API 키 저장과 검증
- 지역 저장 후 디스플레이 날씨 갱신
