# 백로그

에이전트(Claude Code, Codex)가 사람 개입 없이 이어서 실행하도록 쓴 작업 명세 모음입니다.
"백로그 진행하라"는 지시를 받으면 아래 순서대로 하나씩 처리합니다.

## 실행 환경 전제

| 항목 | 값 |
|---|---|
| OS | Windows 10/11 (Direct3D 11 빌드·실행에 필수) |
| 툴체인 | Visual Studio 2022, CMake, vcpkg (Build.bat이 부트스트랩) |
| 빌드 | 저장소 루트에서 `Build.bat` |
| 산출물 | `build/bin/` (CMakeLists.txt의 `CMAKE_RUNTIME_OUTPUT_DIRECTORY`) |
| GPU | Direct3D 11 지원 디스크리트 GPU 권장. 타임스탬프 쿼리를 쓰므로 WARP는 측정 의미가 없음 |

리눅스나 WSL 세션에서는 빌드부터 막힙니다. 그 환경이라면 즉시 보고하고 멈추십시오.

## 작업 순서

| 순서 | 문서 | 내용 | 사람 개입 |
|---|---|---|---|
| 1 | [001-render-metrics.md](001-render-metrics.md) | GPU 타임스탬프, 파이프라인 통계, 드로우콜·업로드 카운터 | 없음 |
| 2 | [002-metrics-hud.md](002-metrics-hud.md) | ImGui 계측 오버레이 | 없음 |
| 3 | [003-legacy-path-toggles.md](003-legacy-path-toggles.md) | 최적화 이전 경로를 런타임 토글로 되살리기 | 없음 |
| 4 | [004-bench-mode-camera-take.md](004-bench-mode-camera-take.md) | 커맨드라인 벤치 모드, 카메라 테이크 녹화·재생 | 테이크 1회 녹화 |
| 5 | [005-ab-capture-verify.md](005-ab-capture-verify.md) | A/B 캡처, CSV 검증, 영상 인코딩 | 없음 |

앞 단계의 완료 조건을 못 채운 상태로 다음 단계에 들어가지 마십시오.

## 공통 규칙

**없던 버그를 새로 만들지 마십시오.** 3단계는 `Docs/OPTIMIZATION_REPORT.md`에 기록된 항목을 되살리는 작업입니다. 문서에 없는 성능 저하를 새로 심으면 실제 이력과 어긋나고, 코드를 확인받는 자리에서 설명이 무너집니다. 항목 ID(P01, P04, P07 등)를 코드 주석에 남겨 두십시오.

**완료 조건의 임계값을 고치지 마십시오.** 게이트를 못 넘으면 토글이 실제로 안 걸렸거나 측정이 잘못된 것입니다. 임계값을 낮추지 말고 무엇이 어긋났는지 보고하십시오.

**측정 결과를 손으로 쓰지 마십시오.** CSV와 PNG는 엔진이 만든 것만 씁니다.

**커밋 단위는 문서 하나입니다.** 각 단계를 끝내고 완료 조건을 확인한 뒤 커밋하십시오. 조건을 못 채웠으면 커밋하지 않고 보고합니다.

## 산출물 위치

```
Artifacts/
  bench_legacy.csv          3단계 토글을 켠 상태의 프레임별 계측
  bench_current.csv         현재 코드 상태의 프레임별 계측
  legacy/%06d.png           legacy 실행의 프레임 덤프
  current/%06d.png          current 실행의 프레임 덤프
  legacy.mp4  current.mp4   ffmpeg 인코딩 결과
Bench/
  take01.json               사람이 녹화한 카메라 경로 (입력 자산, 커밋 대상)
```

`Artifacts/`는 `.gitignore`에 넣습니다. `Bench/take01.json`은 커밋합니다.

## 배경

`Docs/OPTIMIZATION_REPORT.md`와 노션의 렌더독 캡처 기록에 최적화 내용이 글로 남아 있습니다.
포트폴리오에서 이 부분이 가장 큰 항목인데 현재는 GIF 한 장이라 개선 폭이 보이지 않습니다.
같은 씬, 같은 카메라 경로에서 이전 경로와 현재 코드를 각각 돌려 프레임 시간과 GPU 시간, 드로우콜을
화면에 띄운 영상 두 개를 만드는 것이 이 백로그의 목표입니다.
