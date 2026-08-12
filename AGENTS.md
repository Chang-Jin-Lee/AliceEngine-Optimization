# 에이전트 안내

## 백로그를 진행하라는 지시를 받았다면

`Docs/backlog/README.md`를 먼저 읽으십시오. 실행 환경 전제와 작업 순서, 공통 규칙이 거기에 있습니다.
번호 순서대로 하나씩 처리하고, 앞 단계의 완료 조건을 못 채운 상태로 다음 단계에 들어가지 마십시오.

## 이 저장소

Direct3D 11 기반 자체 3D 게임 엔진과 에디터입니다. 팀 게임 EGOSIS를 이 엔진으로 만들었고,
게임을 시연한 뒤 엔진을 리팩토링한 저장소입니다.

| 경로 | 내용 |
|---|---|
| `EngineSource/Engine/src/Runtime` | 엔진 런타임. ECS, Rendering, Physics, UI, Scripting, Resources, Importing, Audio, Input, Foundation, Gameplay, Engine |
| `EngineSource/Engine/src/Editor` | 에디터. 패널, 인스펙터, 애셋 에디터 |
| `EngineSource/Engine/src/Samples/Sandbox` | 실행 진입점 (`Main.cpp`의 `wWinMain`) |
| `Assets` | 씬, 프리팹, FBX, 스크립트 |
| `Docs` | 설계 문서와 최적화 보고서 |
| `Docs/backlog` | 에이전트 실행용 작업 명세 |

## 빌드와 실행

```
Build.bat
```

vcpkg 부트스트랩과 서브모듈 갱신까지 처리합니다. 산출물은 `build/bin/`입니다.
**Windows에서만 빌드·실행됩니다.** 리눅스나 WSL 세션이면 즉시 보고하고 멈추십시오.

## 작업 규칙

문서에 기록되지 않은 성능 저하나 버그를 코드에 심지 마십시오.
`Docs/OPTIMIZATION_REPORT.md`에 최적화 항목이 ID(B01, D01, P01 …)로 정리돼 있습니다.
이전 경로를 재현하는 작업은 그 ID를 코드 주석에 남기십시오.

측정값을 손으로 쓰지 마십시오. 엔진이 만든 CSV와 PNG만 근거로 씁니다.

커밋 메시지는 기존 관례를 따릅니다. `[docs]`, `[fix]`, `[feat]`, `[refactor]` 접두어를 씁니다.
