# Signally — 빌드 가이드

## 환경 요구사항

| 항목 | 버전 |
|---|---|
| OS | Windows 10/11 (x64) |
| Visual Studio | 2022 (C++ 데스크톱 개발 워크로드) |
| CMake | 3.22+ |
| Windows SDK | 10.0.22621 이상 |
| WDK | Windows 11 WDK (드라이버 빌드 시) |

## 1. 앱 빌드

```bat
git clone --recurse-submodules https://github.com/your/Signally.git
cd Signally

# ASIO SDK를 third_party\ASIO_SDK\ 에 수동 배치 (Steinberg 사이트에서 다운로드)

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

빌드 결과: `build\Release\Signally.exe`

## 2. 가상 마이크 드라이버 빌드

WDK가 설치된 환경에서:

```
driver\VirtualMicDriver\VirtualMicDriver.vcxproj
→ Visual Studio에서 열어 Release x64 빌드
→ 결과: VirtualMicDriver.sys
```

## 3. 드라이버 설치 (개발/테스트)

```bat
# 테스트 서명 모드 활성화 (관리자 명령 프롬프트, 재부팅 필요)
bcdedit /set testsigning on

# 드라이버 설치
devcon install driver\VirtualMicDriver\VirtualMicDriver.inf Root\VirtualMicDriver

# 확인
devcon status Root\VirtualMicDriver
```

## 4. 드라이버 배포용 서명 (Attestation Signing)

1. EV 코드 서명 인증서 발급
2. https://partner.microsoft.com/dashboard 에서 드라이버 패키지 제출
3. 서명된 .sys 파일을 앱과 함께 배포
4. 앱 설치 시 `DriverInstaller::installDriver()` 자동 호출

## 5. VST3 플러그인 스캔 경로

앱 실행 후 우측 패널 "VST3 Plugins" → "Scan VST3 Folders":
- `C:\Program Files\Common Files\VST3`
- `C:\Program Files (x86)\Common Files\VST3`
- `%APPDATA%\VST3`

## 6. ASIO SDK 설치

1. https://www.steinberg.net/asiosdk 에서 다운로드
2. `third_party\ASIO_SDK\` 에 압축 해제
3. CMake 재실행 시 자동 감지됨

## 오디오 격리 검증 방법

1. OBS Studio 실행 → "오디오 캡처" → "시스템 오디오 캡처" 추가
2. Signally에서 마이크 → VST3 → Virtual Mic Output 연결
3. OBS 오디오 미터 확인 → 파형 없음 ✅
4. Discord에서 "Signally Virtual Microphone" 입력 장치 선택 → 처리된 음성 확인 ✅
