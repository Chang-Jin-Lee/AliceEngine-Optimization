# Legacy Path Toggles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `OPTIMIZATION_REPORT`에 근거가 있는 최적화 이전 렌더·애니메이션 경로를 런타임 플래그로 복원해 같은 실행에서 current/legacy를 비교한다.

**Architecture:** `LegacyPathFlags`가 기본 OFF인 단일 전역 상태를 소유하고 F10·HUD 체크박스·향후 `--legacy`가 같은 `SetAll` 계약을 사용한다. 각 핫패스는 현재 경로를 그대로 둔 채 작은 legacy 분기만 추가하며, 측정 가능한 항목은 기존 `RenderStats` 래퍼를 통과한다.

**Tech Stack:** C++20, Direct3D 11, DirectXMath, ImGui, CMake/CTest.

## Global Constraints

- 문서나 Git 역사에 없는 `Sleep`, busy loop, 중복 반복은 추가하지 않는다.
- 플래그는 모두 기본 false이고 OFF 경로의 기존 분기·데이터 흐름을 바꾸지 않는다.
- 각 legacy 분기 주석에 `OPTIMIZATION_REPORT P01`~`P07` 또는 RenderDoc 기록을 남긴다.
- F10 전체 토글은 `InputSystem::IsKeyPressed`에서 한 프레임 안에 적용한다.
- 이 백로그의 구현·문서·테스트를 정확히 한 커밋으로 만든다.

---

### Task 1: Flag state and control surface

**Files:**
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/LegacyPathFlags.h`
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/LegacyPathFlags.cpp`
- Modify: `EngineSource/Engine/tests/RenderMetricsTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp`
- Modify: `EngineSource/Engine/src/Editor/Panels/MetricsOverlay.cpp`

**Interfaces:**
- Produces: `LegacyPathFlags::Get()`, `SetAll(bool)`, `AnyEnabled()`.
- Consumes: F10 edge input and ImGui checkbox state.

- [x] Add failing contracts for default OFF, every field changed by `SetAll`, and `AnyEnabled`.
- [x] Build `RenderMetricsTests` and confirm the missing type fails compilation.
- [x] Implement the fixed flag singleton and add it to Engine/test targets.
- [x] Add F10 all-toggle and a collapsible checkbox section; make the status badge read `AnyEnabled()`.
- [x] Rebuild tests and require all contracts to pass.

### Task 2: CPU and bone upload legacy branches

**Files:**
- Modify: `DeferredRenderSystem.cpp`, `ForwardRenderSystem.cpp`
- Modify: `Camera.cpp`
- Modify: `World.cpp`
- Modify: `AdvancedAnimSystem.h`

**Interfaces:**
- Consumes: `fullBoneConstantBuffer`, `noCameraMatrixCache`, `heapAllocWorldMatrix`, `copyPaletteEveryFrame`, `animateWhenNotPlaying`.
- Produces: actual legacy Map byte counts and unchanged OFF branches.

- [x] Add pure helper contracts for legacy bone upload count and animation gating.
- [x] Reintroduce 1023-entry bone fill/Map accounting for P01 while keeping `boneCount` shader metadata correct.
- [x] Reintroduce vector parent-chain allocation for P02 in both world-matrix sites.
- [x] Bypass animation unchanged/stopped early-outs for P03 and copy instead of swap for P04.
- [x] Recompute camera matrices per call for P07.
- [x] Build and run CTest.

### Task 3: GPU path legacy branches

**Files:**
- Modify: `UnityVfxMeshRenderSystem.cpp`
- Modify: `EngineRender.cpp`, `SkinnedMeshRegistry.cpp` where the current static filter lives
- Modify: `RenderTypes.h` and material-to-command construction sites
- Modify: `DeferredRenderSystem.cpp` TransparentForward filtering

**Interfaces:**
- Consumes: `perParticleDrawCall`, `staticMeshThroughSkinning`, `outlineOnByDefault`, `opaqueInTransparentPass`.
- Produces: extra real Draw calls/PS invocations/bone uploads through existing render stats wrappers.

- [x] Locate and document the exact optimized/current filter at each site.
- [x] Restore per-particle vector/Map/Draw only from the historical VFX implementation.
- [x] Allow recorded static tile contamination only under its flag.
- [x] Substitute outline width 0.01 only when the effective default is zero and legacy is enabled.
- [x] Relax the TransparentForward opaque filter only under its flag.
- [x] Build and run CTest; scan the diff for artificial delay code.

### Task 4: Runtime gates and commit

**Files:**
- Modify: `Docs/backlog/003-legacy-path-toggles.md`
- Modify: this plan

- [x] Capture an all-OFF baseline and require presentMs/drawCalls/psInvocations within 3% of 002.
- [x] Exercise the dense-map workload with a deterministic 400-tile acceptance fixture, toggle all flags, and require drawCalls, psInvocations, and boneCbBytesUploaded all increase.
- [x] Verify every branch changes without restart and the HUD badge/checks follow the same state.
- [x] Run `Build.bat`, Release targets, CTest, `git diff --check`, and diagnostic/artificial-delay scans.
- [x] Request independent review; fix all Critical/Important findings.
- [x] Mark 003 complete and commit `[feat] 최적화 이전 렌더 경로 토글 추가`.

### Runtime evidence

- 002 baseline, 120 resolved frames: present 45.750682 ms, draw 23.000, PS 10,909,999.275.
- 003 all OFF, 120 resolved frames: present 45.558216 ms, draw 23.000, PS 10,941,703.855; deltas 0.42%, 0%, 0.29%.
- Dense fixture OFF -> ON: draw 25 -> 747, PS 12,068,858 -> 13,216,657, bone upload 68 -> 261,904 bytes.
- The acceptance probe logged the toggle before the next render frame and was removed after capture.
