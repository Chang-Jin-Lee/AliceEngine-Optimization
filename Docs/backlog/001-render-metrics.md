# 001 — 렌더 계측 수집

프레임 시간, GPU 시간, 드로우콜, 오버드로우, 업로드량, 메모리를 프레임마다 모으는 계층을 만듭니다.
화면 표시는 002에서 하고 여기서는 수집과 조회 API까지만 합니다.

## 왜 필요한가

Direct3D 11 즉시 컨텍스트에서 `DrawIndexed` 앞뒤를 `QueryPerformanceCounter`로 감싸면 아무 의미가 없습니다.
커맨드는 큐에 쌓이기만 하고 GPU 실행은 그 뒤입니다. GPU가 실제로 쓴 시간은 타임스탬프 쿼리로만 알 수 있습니다.
지금 엔진에는 `GameTimer`(`Runtime/Engine/TimeSystem.h`)의 프레임 델타밖에 없습니다.
이 값은 Present에서 Present까지의 벽시계 시간이라 GPU 대기와 드라이버 대기가 섞여 있습니다.

세 값을 분리해서 재야 합니다.

- CPU 프레임 시간 — 메인 스레드가 실제로 일한 구간
- GPU 프레임 시간 — GPU가 그 프레임을 그린 시간
- Present 간격 — 위 둘 중 큰 쪽에 수렴하는 총 프레임 시간. 지금의 `GameTimer` 값

## 새로 만들 파일

```
EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.h
EngineSource/Engine/src/Runtime/Rendering/Metrics/GpuProfiler.cpp
EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.h
EngineSource/Engine/src/Runtime/Rendering/Metrics/RenderStats.cpp
```

`CMakeLists.txt`가 소스를 glob으로 모으는지 확인하고, 명시 목록이면 추가하십시오.

## GpuProfiler

`D3D11_QUERY_TIMESTAMP_DISJOINT` 하나와 구간별 `D3D11_QUERY_TIMESTAMP` 쌍을 한 세트로 묶고,
세트를 **4프레임 링버퍼**로 돌립니다. 같은 프레임에서 `GetData`를 호출하면 GPU를 기다려서 측정 자체가 프레임을 망칩니다.

```cpp
namespace Alice
{
    enum class GpuScope : int
    {
        Frame = 0,          // RenderBeginFrame ~ RenderEndFrame 전체
        MainPass,           // RenderMainPass
        CameraPreview,      // RenderCameraPreview
        ComputeEffects,     // RenderComputeEffects
        ParticleOverlay,    // RenderParticleOverlayComposite
        DebugOverlay,       // RenderDebugOverlayComposite
        ToneMapAndUI,       // RenderGameModeToneMappingAndUI
        OverlayEffects,     // RenderOverlayEffects
        EditorDraw,         // RenderEditorDraw
        Count
    };

    class GpuProfiler
    {
    public:
        bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx);
        void Shutdown();

        void BeginFrame();                  // disjoint Begin + Frame 시작 타임스탬프
        void BeginScope(GpuScope s);
        void EndScope(GpuScope s);
        void EndFrame();                    // Frame 종료 타임스탬프 + disjoint End
        void Resolve();                     // 3~4프레임 전 세트를 폴링해 회수

        double ScopeMs(GpuScope s) const;   // 회수된 최신 값
        bool   LastFrameDisjoint() const;   // true면 그 프레임 값은 버려야 함
    };
}
```

회수 규칙입니다.

- `GetData`는 반드시 `D3D11_ASYNC_GETDATA_DONOTFLUSH`로 폴링하고, `S_FALSE`면 이번 프레임에는 건너뜁니다.
- `D3D11_QUERY_DATA_TIMESTAMP_DISJOINT::Disjoint`가 참이면 GPU 클럭이 바뀐 프레임입니다. **그 프레임 값 전체를 폐기**하고 CSV에도 쓰지 않습니다. 안 버리면 그래프에 가짜 스파이크가 찍힙니다.
- ms 환산은 `(end - begin) / dj.Frequency * 1000.0`이고 `Frequency == 0`이면 폐기합니다.
- 중첩 스코프는 지원하지 않습니다. 같은 스코프를 한 프레임에 두 번 열면 로그로 경고하고 두 번째를 무시하십시오.

## 스코프를 넣을 위치

`EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp`의 `Engine::Impl::RenderFrame()`이
이미 패스별 헬퍼로 쪼개져 있습니다(43행). 각 헬퍼 호출을 스코프로 감싸면 됩니다.

```cpp
void Engine::Impl::RenderFrame()
{
    if (!m_renderDevice) return;
    m_gpuProfiler.BeginFrame();
    m_renderStats.BeginFrame();
    ...
    { ALICE_GPU_SCOPE(GpuScope::MainPass);       RenderMainPass(); }
    { ALICE_GPU_SCOPE(GpuScope::CameraPreview);  RenderCameraPreview(); }
    RenderUnbindDepthOnly();
    { ALICE_GPU_SCOPE(GpuScope::ComputeEffects); RenderComputeEffects(); }
    ...
    m_gpuProfiler.EndFrame();
    m_renderStats.EndFrame();
    RenderEndFrame();
    m_gpuProfiler.Resolve();
}
```

`ALICE_GPU_SCOPE`는 RAII 매크로로 만드십시오. 조기 return이 있는 헬퍼에서 EndScope가 빠지는 것을 막습니다.

## RenderStats

한 프레임 분 카운터입니다. 렌더 시스템이 값을 올리고 프레임 끝에 스냅샷을 만듭니다.

| 필드 | 수집 방법 | 이 영상에서 보여줄 것 |
|---|---|---|
| `drawCalls` | 드로우 호출 지점마다 `++` | 같은 타일이 몇 번 올라가는가 |
| `instancedDrawCalls` | 인스턴스드 드로우만 별도 집계 | 배칭이 실제로 도는지 |
| `boneCbMapCount` | `UpdateBonesCB` 호출 수 | 드로우마다 Map하고 있는가 |
| `boneCbBytesUploaded` | 실제 Map한 바이트 누적 | 64KB × 드로우 수가 그대로 보임 |
| `iaPrimitives` | 파이프라인 통계 | 제출한 삼각형 수 |
| `vsInvocations` | 파이프라인 통계 | 정점 셰이더 호출. 스키닝 경로 오염이 튐 |
| `psInvocations` | 파이프라인 통계 | 픽셀 셰이더 호출 = 오버드로우 |
| `cPrimitives` | 파이프라인 통계 | 래스터라이즈까지 살아남은 삼각형 |
| `cpuFrameMs` | `QueryPerformanceCounter` | 메인 스레드 작업 시간 |
| `presentMs` | `GameTimer::DeltaTime()` × 1000 | 총 프레임 시간 |
| `vramUsedMB`, `vramBudgetMB` | `IDXGIAdapter3::QueryVideoMemoryInfo` | GPU 메모리 |
| `workingSetMB` | `GetProcessMemoryInfo` (psapi) | 프로세스 메모리 |

파이프라인 통계는 `D3D11_QUERY_PIPELINE_STATISTICS`이고 GpuProfiler와 같은 링버퍼·폐기 규칙을 씁니다.

드로우콜과 본 상수버퍼 카운터를 넣을 지점입니다.

- `Runtime/Rendering/DeferredRenderSystem.cpp` — `UpdateBonesCB` 부근(5473행 근처에 `MaxBones` 상수 있음)과 각 드로우 호출
- `Runtime/Rendering/ForwardRenderSystem.cpp` — 같은 함수(1138행 근처)
- `Runtime/Rendering/UnityVfxMeshRenderSystem.cpp` — 파티클당 드로우가 나가는 지점
- `Runtime/Rendering/TrailEffectRenderSystem.cpp`

카운터 증가 코드를 드로우 호출마다 손으로 넣는 대신, 드로우를 얇은 래퍼 함수 하나로 통과시키는 편이 누락이 적습니다. 래퍼를 넣기 어려운 지점만 직접 증가시키고, 어디에 넣었는지 커밋 메시지에 남기십시오.

## 오버헤드

계측 자체가 프레임을 먹으면 안 됩니다.

- 링버퍼 폴링은 `DONOTFLUSH`로만 하고 대기하지 않습니다.
- 스코프 개수는 `GpuScope::Count`로 고정하고 프레임마다 할당하지 않습니다. 링버퍼는 Initialize에서 한 번만 만듭니다.
- `Metrics` 전체를 `bool m_metricsEnabled`로 끌 수 있게 하고 기본값은 켜 둡니다. 끈 상태에서 오버헤드가 0이어야 합니다.

## 완료 조건

1. `Build.bat`이 성공하고 경고가 새로 늘지 않습니다.
2. 에디터를 띄운 뒤 `GpuProfiler::ScopeMs(GpuScope::Frame)`이 0보다 큰 값을 반환합니다. 임시 로그로 확인하고 로그는 지웁니다.
3. 패스별 스코프 ms 합이 `Frame` 스코프 ms의 0.8배에서 1.2배 사이입니다. 벗어나면 스코프가 빠졌거나 겹친 것입니다.
4. `drawCalls`가 0이 아니고, 씬에 오브젝트를 추가하면 값이 늘어납니다.
5. 계측을 끈 상태와 켠 상태의 평균 `presentMs` 차이가 3% 이내입니다.
6. `Disjoint`가 참인 프레임을 폐기하는 경로에 도달하는지 로그로 한 번 확인합니다. GPU 클럭이 안정적이면 안 나올 수 있으므로, 강제로 참을 넣어 폐기 분기가 도는 것만 확인하고 되돌립니다.

## 실패 시

패스별 합이 안 맞거나 `Frame` 값이 0이면 다음 단계로 넘어가지 말고, 어느 스코프가 어긋났는지 수치와 함께 보고하십시오.
