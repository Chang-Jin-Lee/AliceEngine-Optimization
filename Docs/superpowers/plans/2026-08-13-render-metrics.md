# Render Metrics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** D3D11 렌더 프레임의 CPU/GPU/Present 시간, 패스별 GPU 시간, 파이프라인 통계, 드로우콜·본 상수버퍼 업로드, VRAM·워킹셋을 비동기 수집하는 001 계측 계층을 구현한다.

**Architecture:** `GpuProfiler`는 4프레임 쿼리 링과 RAII 스코프를 소유하고, `RenderStats`는 같은 깊이의 파이프라인 통계 링 및 CPU 카운터 스냅샷을 소유한다. `Engine::Impl`이 두 서비스를 초기화·종료하고 렌더 패스 경계를 표시하며, 네 렌더 시스템은 주입받은 `RenderStats`의 얇은 Draw 래퍼를 통과한다.

**Tech Stack:** C++20, Direct3D 11 timestamp/disjoint/pipeline-statistics queries, DXGI 1.4 video-memory info, Win32 QPC/process memory, CMake/CTest, Visual Studio 2022.

## Global Constraints

- Windows 10/11과 Direct3D 11 하드웨어 어댑터에서 구현·검증한다.
- GPU 쿼리 회수는 `D3D11_ASYNC_GETDATA_DONOTFLUSH`만 사용하며 CPU/GPU 대기를 만들지 않는다.
- 쿼리와 프레임 저장소는 초기화 때 고정 할당한 4프레임 링버퍼이며 렌더 루프에서 동적 할당하지 않는다.
- disjoint 또는 `Frequency == 0`인 프레임은 GPU 값 전체와 파이프라인 통계 스냅샷에서 제외한다.
- 같은 `GpuScope`는 프레임당 한 번만 열 수 있고 두 번째 요청은 경고 후 무시한다.
- 계측은 기본 활성화하되 `m_metricsEnabled == false`이면 쿼리 발행·폴링·카운터 갱신·메모리 조회를 하지 않는다.
- 측정값은 엔진이 생성한 값만 사용하고 완료 조건의 임계값은 변경하지 않는다.
- 사용자 소유 미추적 FBX/텍스처 파일은 수정·추가·삭제·스테이징하지 않는다.

---

## File Map

- Create `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.h`: GPU 스코프 열거형, timestamp 변환 계약, RAII 마커, 4프레임 쿼리 링 공개 API.
- Create `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.cpp`: D3D11 쿼리 생성·발행·비차단 회수와 disjoint 폐기.
- Create `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.h`: 프레임 스냅샷, CPU 카운터, Draw 래퍼, 파이프라인 통계 공개 API.
- Create `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.cpp`: QPC, 파이프라인 쿼리 링, DXGI VRAM, Win32 working-set 수집.
- Create `EngineSource/Engine/tests/RenderMetricsTests.cpp`: D3D 장치 없이 실행하는 timestamp 변환·카운터 계약 테스트.
- Modify `CMakeLists.txt`: 네 계측 소스/헤더와 CTest 실행 파일, `psapi` 링크를 명시 목록에 추가.
- Modify `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h`: profiler/stats 멤버와 `m_metricsEnabled` 상태 추가.
- Modify `EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp`: render device 직후 계측 초기화, 렌더 시스템에 stats 주입.
- Modify `EngineSource/Engine/src/Runtime/Engine/Engine.cpp`: D3D 장치가 사라지기 전에 계측 종료.
- Modify `EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp`: 프레임/패스 스코프와 프레임 통계 경계 배선.
- Modify `EngineSource/Engine/src/Runtime/Rendering/{ForwardRenderSystem,DeferredRenderSystem,UnityVfxMeshRenderSystem,TrailEffectRenderSystem}.{h,cpp}`: `RenderStats*` 주입, Draw 래퍼 사용, 성공한 본 CB Map 바이트 기록.

---

### Task 1: CPU-only metric contracts and build target

**Files:**
- Create: `EngineSource/Engine/tests/RenderMetricsTests.cpp`
- Modify: `CMakeLists.txt`
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.h`
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.cpp`
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.h`
- Create: `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.cpp`

**Interfaces:**
- Produces: `bool Alice::MetricsDetail::TryTimestampMilliseconds(std::uint64_t begin, std::uint64_t end, std::uint64_t frequency, bool disjoint, double& outMs)`.
- Produces: `Alice::RenderFrameCounters` with `Reset()`, `RecordDraw(bool instanced)`, and `RecordBoneCbUpload(std::uint64_t bytes)`.
- Produces: CTest target `RenderMetricsTests` and test name `render_metrics_contracts`.

- [x] **Step 1: Add a failing timestamp conversion test**

```cpp
#include "Runtime/Rendering/Metrics/GpuProfiler.h"
#include "Runtime/Rendering/Metrics/RenderStats.h"
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
}

int main()
{
    double ms = -1.0;
    Check(Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 1'000'000, false, ms),
        "valid timestamp range must resolve");
    Check(std::abs(ms - 3.0) < 0.000001, "timestamp conversion must return 3 ms");
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 0, false, ms),
        "zero frequency must be rejected");
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 1'000'000, true, ms),
        "disjoint sample must be rejected");
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(3100, 100, 1'000'000, false, ms),
        "reversed timestamp range must be rejected");

    Alice::RenderFrameCounters counters{};
    counters.RecordDraw(false);
    counters.RecordDraw(true);
    counters.RecordBoneCbUpload(65'488);
    Check(counters.drawCalls == 2, "all draw calls must be counted");
    Check(counters.instancedDrawCalls == 1, "instanced draw calls must be counted separately");
    Check(counters.boneCbMapCount == 1, "successful bone CB maps must be counted");
    Check(counters.boneCbBytesUploaded == 65'488, "uploaded bone CB bytes must accumulate");
    counters.Reset();
    Check(counters.drawCalls == 0 && counters.boneCbBytesUploaded == 0,
        "frame counter reset must clear the previous frame");

    return failures == 0 ? 0 : 1;
}
```

- [x] **Step 2: Register and run the failing test target**

Add the four metric files to `ENGINE_SOURCES`/`ENGINE_HEADERS`, create each `.cpp` as an otherwise empty translation unit that includes its matching header, then add:

```cmake
include(CTest)
if(BUILD_TESTING)
    add_executable(RenderMetricsTests
        EngineSource/Engine/tests/RenderMetricsTests.cpp
        ${ALICE_SRC_DIR}/Runtime/Rendering/Metrics/GpuProfiler.cpp
        ${ALICE_SRC_DIR}/Runtime/Rendering/Metrics/RenderStats.cpp
        ${ALICE_SRC_DIR}/Runtime/Foundation/Logger.cpp
    )
    target_include_directories(RenderMetricsTests PRIVATE ${ALICE_SRC_DIR})
    target_compile_definitions(RenderMetricsTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(RenderMetricsTests PRIVATE d3d11 dxgi psapi)
    add_test(NAME render_metrics_contracts COMMAND RenderMetricsTests)
endif()
```

Run:

```powershell
cmake --build build --config Release --target RenderMetricsTests
```

Expected: compilation fails because the declared contracts have no implementation.

- [x] **Step 3: Add the minimal CPU-only contracts**

Declare `TryTimestampMilliseconds` in `GpuProfiler.h`. Define `RenderFrameCounters` in `RenderStats.h` with these exact fields:

```cpp
std::uint64_t drawCalls = 0;
std::uint64_t instancedDrawCalls = 0;
std::uint64_t boneCbMapCount = 0;
std::uint64_t boneCbBytesUploaded = 0;
```

Implement reset by value-initializing `*this`, count every draw in `RecordDraw`, count the instanced subset when requested, and increment map count plus bytes in `RecordBoneCbUpload`.

- [x] **Step 4: Run the CPU-only tests**

Run:

```powershell
cmake --build build --config Release --target RenderMetricsTests
ctest --test-dir build -C Release -R render_metrics_contracts --output-on-failure
```

Expected: build succeeds and `render_metrics_contracts` passes.

---

### Task 2: Four-frame GPU profiler

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.cpp`
- Modify: `EngineSource/Engine/tests/RenderMetricsTests.cpp`

**Interfaces:**
- Consumes: `MetricsDetail::TryTimestampMilliseconds(...)` from Task 1.
- Produces: `enum class GpuScope : std::uint8_t { Frame, MainPass, CameraPreview, ComputeEffects, ParticleOverlay, DebugOverlay, ToneMapAndUI, OverlayEffects, EditorDraw, Count }`.
- Produces: `enum class GpuFrameOutcome : std::uint8_t { Unavailable, Pending, Valid, Discarded }`.
- Produces: `GpuProfiler::{Initialize,Shutdown,SetEnabled,IsEnabled,BeginFrame,BeginScope,EndScope,EndFrame,Resolve,ScopeMs,LastFrameDisjoint,ResolvedFrameSerial,DiscardedFrameCount,FrameOutcome}`; engine integration calls `BeginFrame(std::uint64_t frameSerial)`.
- Produces: `ScopedGpuProfile` and `ALICE_GPU_SCOPE(profiler, scope)`.

- [x] **Step 1: Extend the failing tests for invalid scopes and initial state**

Add assertions that a default `GpuProfiler` is disabled, reports zero milliseconds for every valid scope, has no resolved frame serial, and preserves `outMs` only on successful timestamp conversion.

- [x] **Step 2: Run the tests and confirm the new API is absent**

Run the Task 1 build command. Expected: compile failure naming the missing `GpuProfiler` API.

- [x] **Step 3: Implement fixed query storage and initialization**

Use `static constexpr std::size_t kBufferedFrames = 4` and a `FrameQueries` containing one `ID3D11Query` disjoint query plus begin/end timestamp pairs for every scope. Create every query in `Initialize`; on any failure call `Shutdown()` and return `false`. Retain the immediate context with `ComPtr`, reset all values in `Shutdown`, and do not allocate in frame methods.

- [x] **Step 4: Implement issue rules and RAII closure**

`BeginFrame(frameSerial)` chooses the next free ring slot, stores the engine-provided serial, begins disjoint, emits the Frame begin timestamp, and clears per-scope used/active flags. If all four slots are still pending, it skips that frame without overwriting data. `BeginScope` rejects `Frame`, `Count`, a second use, inactive frames, and a user scope opened while another user scope is active; `EndScope` only emits when its matching begin succeeded. `EndFrame` closes any still-active scope with a warning, emits Frame end, ends disjoint, and marks the slot pending.

`ScopedGpuProfile` stores the profiler pointer only when `BeginScope` succeeds and calls `EndScope` in its destructor. The macro must generate a unique local variable using `__LINE__`.

- [x] **Step 5: Implement non-blocking resolve and disjoint discard**

Poll pending slots oldest-first with `D3D11_ASYNC_GETDATA_DONOTFLUSH`. If disjoint data, any used timestamp, or any end timestamp returns `S_FALSE`, leave that slot pending and return. If the disjoint result is true or frequency zero, increment the discarded count, mark that serial's outcome `Discarded`, mark the last frame disjoint, clear the slot, and publish no scope values. Otherwise convert every used scope, mark that serial's outcome `Valid`, and atomically publish one complete scope array plus its frame serial. Keep the most recent four serial outcomes in fixed storage so `RenderStats` can gate the matching pipeline query.

- [x] **Step 6: Run tests and compile the engine target**

Run:

```powershell
cmake --build build --config Release --target RenderMetricsTests Engine
ctest --test-dir build -C Release -R render_metrics_contracts --output-on-failure
```

Expected: tests pass and `Engine` compiles without new warnings.

---

### Task 3: RenderStats pipeline, CPU, memory, and Draw wrappers

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.cpp`
- Modify: `EngineSource/Engine/tests/RenderMetricsTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RenderFrameCounters` from Task 1.
- Produces: `RenderStatsSnapshot` fields `frameSerial`, counter fields, `iaPrimitives`, `vsInvocations`, `psInvocations`, `cPrimitives`, `cpuFrameMs`, `presentMs`, `vramUsedMB`, `vramBudgetMB`, `workingSetMB`, and `pipelineStatsValid`.
- Produces: `RenderStats::{Initialize,Shutdown,SetEnabled,IsEnabled,BeginFrame,EndFrame,Resolve,Latest,RecordBoneCbUpload,Draw,DrawIndexed,DrawInstanced,DrawIndexedInstanced}` where `BeginFrame(std::uint64_t frameSerial, double presentMs)` uses the engine serial and `Resolve(const GpuProfiler&)` publishes only GPU-valid frame serials.

- [x] **Step 1: Add failing disabled/reset and snapshot tests**

Exercise a default `RenderStats` without a D3D device: disabled recording must not change counters; enabling, beginning a frame, recording two draws and one bone upload, and ending a CPU-only frame must publish the complete non-pipeline snapshot through `Latest()`. Add a deterministic ring-state test proving all terminal GPU outcomes are latched before the four-record outcome cache can be overwritten.

- [x] **Step 2: Run the tests and verify failure**

Compile `RenderMetricsTests` with `ALICE_METRICS_TESTING=1`. Expected: compile failure until the RenderStats API exists.

- [x] **Step 3: Implement fixed pipeline-statistics ring**

Create four `D3D11_QUERY_PIPELINE_STATISTICS` queries during `Initialize`. `BeginFrame(frameSerial, presentMs)` resets the current counters, stores the engine-provided serial, records QPC start and present interval, and begins the current query; if all four slots are pending it skips without overwriting. `EndFrame` records QPC duration, ends the query, captures memory, stores all CPU values in that ring slot, and marks it pending. Without an initialized D3D query it publishes a CPU-only snapshot immediately. `Resolve(const GpuProfiler&)` first latches `Valid`/`Discarded` outcomes for every pending stats slot before the profiler's four-record outcome cache can be reused, then polls only the oldest GPU-valid pipeline query with `DONOTFLUSH`. It maps `IAPrimitives`, `VSInvocations`, `PSInvocations`, and `CPrimitives`, then publishes the entire stored snapshot.

- [x] **Step 4: Implement memory collection without per-frame COM discovery**

During `Initialize`, obtain and retain `IDXGIAdapter3` through `IDXGIDevice::GetAdapter`. During `EndFrame`, call `QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, ...)` and convert `CurrentUsage`/`Budget` to MiB. Use `GetProcessMemoryInfo(GetCurrentProcess(), ...)` for working set. A missing `IDXGIAdapter3` leaves only VRAM fields at zero and does not fail initialization. Link `psapi` for `Launch`, `AlicePlayer`, and `RenderMetricsTests`.

- [x] **Step 5: Implement centralized Draw wrappers**

Every wrapper records exactly one draw when enabled and always forwards the original arguments to `ID3D11DeviceContext`. The two instanced wrappers pass `true` to `RecordDraw`; `Draw` and `DrawIndexed` pass `false`. `RecordBoneCbUpload` updates only after a successful Map.

- [x] **Step 6: Run tests and build**

Run:

```powershell
cmake --build build --config Release --target RenderMetricsTests Engine Launch AlicePlayer
ctest --test-dir build -C Release -R render_metrics_contracts --output-on-failure
```

Expected: test passes, all targets link with `psapi`, and no new warnings appear.

---

### Task 4: Engine lifecycle, pass scopes, and render-system counters

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Engine/Engine.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/ForwardRenderSystem.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/DeferredRenderSystem.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/UnityVfxMeshRenderSystem.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/UnityVfxMeshRenderSystem.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/TrailEffectRenderSystem.h`
- Modify: `EngineSource/Engine/src/Runtime/Rendering/TrailEffectRenderSystem.cpp`

**Interfaces:**
- Consumes: all Task 2 and Task 3 profiler/stat APIs.
- Produces: engine-owned `GpuProfiler m_gpuProfiler`, `RenderStats m_renderStats`, default-true `bool m_metricsEnabled`, and monotonically increasing `std::uint64_t m_renderFrameSerial`.
- Produces: `SetRenderStats(RenderStats*)` on the four listed render systems.

- [x] **Step 1: Add engine-owned services and initialize them after D3D device creation**

Include both metric headers in `EngineImpl.h`. In `InitializeRenderDevice`, after successful device creation, set enabled state on both services and initialize them with `GetDevice()`/`GetImmediateContext()`. Treat query initialization failure as engine initialization failure with a precise log. In `Engine::Shutdown`, call both `Shutdown()` methods before subsystem members and the render device are destroyed.

- [x] **Step 2: Wrap the frame and nine named render passes**

In `RenderFrame`, increment `m_renderFrameSerial`, call `SetEnabled(m_metricsEnabled)` on both services, then pass that serial to both `BeginFrame` calls before `RenderBeginFrame`. Wrap `RenderMainPass`, `RenderCameraPreview`, `RenderComputeEffects`, `RenderParticleOverlayComposite`, `RenderDebugOverlayComposite`, `RenderGameModeToneMappingAndUI`, `RenderOverlayEffects`, and conditional `RenderEditorDraw` with `ALICE_GPU_SCOPE`. End the GPU/stats frames before `RenderEndFrame`, then call `m_gpuProfiler.Resolve()` followed by `m_renderStats.Resolve(m_gpuProfiler)` after Present returns so a disjoint GPU frame cannot publish pipeline statistics.

`GpuScope::Frame` is emitted by `GpuProfiler::BeginFrame`/`EndFrame`; it is not wrapped by the macro. The `MainPass` scope starts before `RenderBeginFrame` so swap-chain target setup and draw-list preparation GPU submissions are included in the named-scope sum.

- [x] **Step 3: Inject RenderStats into the four systems**

Add a forward declaration, `SetRenderStats(RenderStats*)`, and nullable member to each header. Immediately after construction in `InitializeRenderSystems`, pass `&m_renderStats` to forward, deferred, Unity VFX mesh, and trail systems.

- [x] **Step 4: Route every direct Draw call in the four named systems**

Replace each live `m_context->Draw*` call in the four `.cpp` files with the matching `m_renderStats->Draw*` wrapper. Preserve the exact original argument order. For defensive standalone use, if the pointer is null, call the original context method. Do not count commented-out calls.

- [x] **Step 5: Record successful bone constant-buffer uploads**

In forward and deferred `UpdateBonesCB`, after a successful Map and before Unmap, call `RecordBoneCbUpload(sizeof(CBBones))`. Failed Map calls record neither count nor bytes. The expected upload size is `sizeof(CBBones) == 65,488` bytes.

- [x] **Step 6: Build all targets and run contract tests**

Run:

```powershell
cmake --build build --config Release --target RenderMetricsTests Engine Launch AlicePlayer
ctest --test-dir build -C Release -R render_metrics_contracts --output-on-failure
```

Expected: all requested targets build and the contract test passes.

---

### Task 5: Runtime completion gates and cleanup

**Files:**
- Temporarily modify then restore: `EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp`
- Modify: `Docs/backlog/001-render-metrics.md`
- Modify: `Docs/superpowers/plans/2026-08-13-render-metrics.md`

**Interfaces:**
- Consumes: `GpuProfiler::ScopeMs`, `GpuProfiler::LastFrameDisjoint`, and `RenderStats::Latest`.
- Produces: checked completion boxes and a single 001 implementation commit.

- [x] **Step 1: Capture a clean baseline build result**

Run `Build.bat` and save the terminal summary outside the repository working tree. Record warning/error counts from the command output; do not invent counts.

- [x] **Step 2: Add bounded diagnostic logging and run the editor**

For one local verification build only, log a bounded sample window containing Frame GPU ms, the sum of eight pass ms values, draw calls, bone upload bytes, CPU ms, Present ms, VRAM, and working set. Start `build/bin/Release/Launch.exe`, allow at least 240 rendered frames, close it, and retain the generated engine log as evidence outside tracked files.

Expected: Frame GPU ms and drawCalls are non-zero, and `passSum / frameGpuMs` is in `[0.8, 1.2]`.

- [x] **Step 3: Exercise the disjoint discard branch**

For one local test build, force the local copy of resolved `D3D11_QUERY_DATA_TIMESTAMP_DISJOINT::Disjoint` to `TRUE` immediately before the discard condition. Run until at least one pending slot resolves and confirm `DiscardedFrameCount()` increments while the previously published scope values remain unchanged. Revert the forced value before the next build.

- [x] **Step 4: Compare enabled and disabled overhead**

Run two Release editor samples over the same scene and fixed 600-frame observation window, first with `m_metricsEnabled = true`, then with a local uncommitted `false`. Compute the arithmetic mean of engine-produced `presentMs` values. Require `abs(enabledMean - disabledMean) / disabledMean <= 0.03`, then restore the default `true`.

- [x] **Step 5: Remove diagnostics and verify the final diff**

Remove temporary logging and forced test edits. Use `rg` and `git diff --check` to confirm there is no diagnostic log, forced disjoint assignment, `Sleep`, or busy loop in the implementation. Confirm the only unrelated working-tree entries are the pre-existing user FBX/texture assets.

- [x] **Step 6: Run final verification**

Run:

```powershell
Build.bat
ctest --test-dir build -C Release -R render_metrics_contracts --output-on-failure
```

Expected: Build.bat succeeds without a warning-count increase and the test passes.

- [x] **Step 7: Mark 001 complete and commit only scoped files**

Mark the six completion conditions in `Docs/backlog/001-render-metrics.md` as checked, update this plan's completed steps, inspect `git diff --cached`, and commit without the user's untracked assets:

```powershell
git commit -m "[feat] D3D11 렌더 계측 수집 계층 추가"
```

Do not start 002 unless every 001 completion condition passed.
