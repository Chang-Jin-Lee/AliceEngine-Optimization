# Bench Mode and Camera Take Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 동일 씬·동일 프레임 인덱스의 카메라/시뮬레이션으로 legacy와 current 실행을 반복할 수 있는 촬영·계측 모드를 만든다.

**Architecture:** `CommandLineOptions`가 Win32 인자를 검증하고, `BenchCameraTake`가 사람이 비행한 샘플을 고정 60 Hz 슬롯으로 리샘플링/JSON 저장하며 replay는 프레임 인덱스로만 진행한다. Engine은 벤치 세션 수명·씬·고정 dt·CSV·종료를 소유하고, D3D11 device는 vsync와 Present 직전 백버퍼 PNG 캡처만 담당한다.

**Tech Stack:** C++20, Win32, Direct3D 11, DirectXMath, WIC, nlohmann JSON, CMake/CTest.

## Constraints

- `Bench/take01.json`은 사람이 직접 비행해 만드는 입력 자산이며 구현/테스트 코드가 생성하지 않는다.
- camera replay는 시간 보간이 아니라 frame index로 진행하고 게임 dt도 take의 fixed delta로 고정한다.
- CSV 실행과 PNG 실행은 파서에서 상호 배타로 강제한다.
- 인자 없는 실행은 기존 1600x900, vsync on, 기존 시작 씬을 유지한다.
- 이 백로그의 구현·문서·테스트를 정확히 한 커밋으로 만든다.

---

### Task 1: Command-line contract

**Files:**
- Create: `Runtime/Engine/CommandLineOptions.h/.cpp`
- Modify: `Samples/Sandbox/Main.cpp`, `GameMain.cpp`, `Engine.h/.cpp`, `CMakeLists.txt`
- Modify: `Engine/tests/RenderMetricsTests.cpp`

- [x] Add failing parser tests for every option, defaults, invalid values, record/replay conflict, and CSV/frames conflict.
- [x] Implement `GetCommandLineW` + `CommandLineToArgvW` parsing shared by editor and player.
- [x] Pass immutable options into Engine before window/render initialization.

### Task 2: Camera take record/replay

**Files:**
- Create: `Runtime/Engine/BenchCameraTake.h/.cpp`
- Modify: `EngineImpl.h`, `EngineInitialize.cpp`, `EngineUpdate.cpp`, `EngineRender.cpp`, `Engine.cpp`
- Modify: `RenderMetricsTests.cpp`

- [x] Add failing tests for fixed-slot resampling, quaternion interpolation, JSON round-trip, and frame-index replay.
- [x] Record timestamped camera samples and resample to `fixedDeltaSeconds` only when saving.
- [x] Load replay before the loop, ignore camera input, apply one frame per rendered frame, and fix gameplay dt.
- [x] Stop at take frameCount or duration and log the final camera transform.

### Task 3: Vsync, CSV, and PNG outputs

**Files:**
- Modify: `IRenderDevice.h`, `ID3D11RenderDevice.h`, `D3D11RenderDevice.h/.cpp`
- Modify: `EngineInitialize.cpp`, `EngineRender.cpp`, `EngineImpl.h`, `EngineWindow.cpp`
- Modify: `RenderMetricsTests.cpp`, `CMakeLists.txt`

- [x] Add runtime sync interval and report it accurately in the HUD/CSV condition line.
- [x] Write only unique, coherent, non-disjoint resolved snapshots after warmup.
- [x] Copy the backbuffer before Present and encode WIC PNGs using the frame pattern/stride.
- [x] Create parent directories and fail initialization with actionable errors.

### Task 4: Verification and human handoff

- [x] Run parser/take tests, Build.bat, Release Engine/Launch/AlicePlayer, and CTest.
- [x] Run automated temporary-take replay twice and require matching final positions to 0.001; do not create `Bench/take01.json`.
- [x] Run duration/CSV and PNG smoke checks separately; open one PNG and verify rendered scene + HUD.
- [x] Run no-argument smoke test and independent code review; fix Critical/Important findings.
- [x] Commit `[feat] 벤치 모드와 카메라 테이크 추가`.
- [ ] If `Bench/take01.json` is absent, stop with the exact human recording request from backlog 004.

## Verification evidence

- Release `RenderMetricsTests`, `Launch`, `AlicePlayer` builds pass; CTest `render_metrics_contracts` passes.
- Two replay runs both finish at frame 6, position `(2.500000, 2.000000, -5.000000)`.
- CSV smoke: 651 lines with Release/GPU/driver/run-condition header and coherent metric rows.
- PNG smoke: `000/005/010.png`, each 640x360; visual inspection confirms the rendered scene and metrics HUD.
- Record smoke: 1/60-second take, declared/actual frameCount 13, full logical scene path, no `.tmp` residue.
- Invalid frame-output path exits nonzero without an interactive bench-mode dialog.
- Independent final review: Critical 0, Important 0.
