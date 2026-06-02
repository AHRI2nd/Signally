# third_party 의존성 배치 안내

이 디렉토리는 외부 SDK들이 위치하는 곳입니다. 라이선스/재배포 제약 때문에
저장소에는 포함되지 않으며, 빌드 전에 아래 절차로 직접 준비해야 합니다.

## 1. VST3 SDK (git submodule, MIT)

`.gitmodules`에 정의되어 있습니다. 최초 클론 후 한 번만 등록하면 됩니다.

```bat
:: 기존 저장소에서 서브모듈을 처음 가져올 때
git submodule update --init --recursive

:: .gitmodules만 있고 아직 등록되지 않은 경우 (한 번만)
git submodule add https://github.com/steinbergmedia/vst3sdk.git third_party/vst3sdk
git submodule update --init --recursive
```

결과 경로: `third_party/vst3sdk/`

## 2. ASIO SDK (Steinberg, 수동 배치 — 재배포 금지)

1. https://www.steinberg.net/asiosdk 에서 다운로드
2. 압축을 풀어 다음 구조가 되도록 배치:

```
third_party/ASIO_SDK/
├── common/
│   ├── asio.h
│   └── ...
├── host/
└── ...
```

`CMakeLists.txt`가 `third_party/ASIO_SDK/common/asio.h` 존재 여부로 자동 감지하며,
없으면 ASIO 지원을 비활성화한 채 빌드됩니다.

> ASIO SDK는 `.gitignore`에 의해 추적되지 않습니다 (재배포 불가 라이선스).
