# Metrics Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 001의 렌더 계측을 에디터와 게임 실행 양쪽에서 읽을 수 있는 고정형 ImGui 오버레이로 표시한다.

**Architecture:** `MetricsHistory`는 240개 고정 링과 min/avg/max/1% low 계산만 담당하고 CPU 테스트로 검증한다. `MetricsOverlay`는 최신 `RenderStatsSnapshot`과 `GpuProfiler`를 받아 표시하며, `Engine::Impl`은 모든 실행 모드에서 ImGui 프레임 수명만 관리하고 에디터 도킹 UI는 기존 분기 안에 유지한다.

**Tech Stack:** C++20, ImGui DX11/Win32 backend, DirectXTK input, CMake/CTest.

## Global Constraints

- F9 입력은 `InputSystem::IsKeyPressed(DirectX::Keyboard::Keys::F9)`만 사용한다.
- 오버레이는 우상단 340px, 배경 알파 0.55, 1920x1080 면적 15% 이하이다.
- 히스토리는 240프레임 고정 저장소이며 프레임 루프에서 할당하지 않는다.
- disjoint/미회수 프레임은 마지막 유효값을 유지한다.
- legacy 체크박스는 003에서 추가하며 002는 상태 배지만 자리만 제공한다.

---

### Task 1: Fixed metrics history contracts

**Files:**
- Create: `EngineSource/Engine/src/Editor/Panels/MetricsOverlay.h`
- Create: `EngineSource/Engine/src/Editor/Panels/MetricsOverlay.cpp`
- Modify: `EngineSource/Engine/tests/RenderMetricsTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `MetricsHistory::{Push,Values,Count,Summary}` and `MetricsSummary{minimum,average,maximum,onePercentLow}`.
- Produces: `MetricsOverlay::{SetVisible,ToggleVisible,IsVisible,Update,Render}`.

- [x] Add failing tests for ring wrap, stable ordering, summary values, and 1% low as the mean of the slowest ceil(1%) samples.
- [x] Build `RenderMetricsTests` and confirm missing overlay contracts fail compilation.
- [x] Implement the fixed 240-value ring and allocation-free summary calculation.
- [x] Rebuild and run CTest until all contracts pass.

### Task 2: Overlay rendering and engine lifetime

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp`

**Interfaces:**
- Consumes: `RenderStats::Latest`, `GpuProfiler::ScopeMs`, `GpuProfiler::ResolvedFrameSerial`, `GpuProfiler::DiscardedFrameCount`.
- Produces: default-on F9 overlay in editor and game mode.

- [x] Add `MetricsOverlay` to explicit source/header lists and test target (linking `imgui`).
- [x] Initialize `EditorCore`'s ImGui context in every mode, retaining logo/docking/editor drawing behind `m_editorMode`.
- [x] Begin an ImGui frame every rendered engine frame and submit ImGui draw data after tone mapping/overlay construction in every mode.
- [x] Render the exact fixed layout, graph, summary, pass table, counters, memory, and status badges.
- [x] Build `RenderMetricsTests Engine Launch AlicePlayer` and run CTest.

### Task 3: Runtime gates and 002 commit

**Files:**
- Modify: `Docs/backlog/002-metrics-hud.md`
- Create: `Docs/backlog/assets/002-overlay.png`
- Modify: this plan

- [x] Run Release editor and game, toggle F9 in each, and verify metrics continue updating.
- [x] Capture a 1920x1080 screenshot, inspect it directly, and require overlay area <= 15%.
- [x] Collect 600-frame overlay-on/off presentMs samples and require <= 3% mean difference.
- [x] Remove temporary diagnostics and verify no `GetAsyncKeyState`, waits, or capture-only code remains.
- [x] Run `Build.bat`, Release targets, and CTest from the final tree.
- [x] Mark 002 and this plan complete and commit `[feat] 렌더 계측 오버레이 추가`.
