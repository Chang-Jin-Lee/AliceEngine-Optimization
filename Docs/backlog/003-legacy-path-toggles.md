# 003 — 최적화 이전 경로를 토글로 되살리기

`Docs/OPTIMIZATION_REPORT.md`에 기록된 최적화 항목을 런타임 플래그로 되돌립니다.

## 이 작업의 원칙

**없던 버그를 새로 만들지 마십시오.** 임의로 `Sleep`을 넣거나 루프를 늘리거나 문서에 없는 성능 저하를 심는 것은 금지입니다.
실제로 있었던 코드 경로를 되살리는 것만 허용합니다. 되살린 항목마다 `Docs/OPTIMIZATION_REPORT.md`의 ID를 코드 주석에 남기십시오.

이유가 두 가지입니다. 첫째, 면접에서 코드를 확인받을 때 문서와 코드가 어긋나면 설명이 무너집니다.
둘째, 실제로 고친 것보다 과장된 개선 폭이 나오면 그 수치 자체가 신뢰를 잃습니다.

이름도 그렇게 붙이십시오. `bug`, `broken`, `slow` 같은 단어를 쓰지 말고 `legacy`를 씁니다.
영상 자막도 "버그"가 아니라 "최적화 이전 경로"로 적습니다.

## 플래그 정의

```
EngineSource/Engine/src/Runtime/Rendering/Metrics/LegacyPathFlags.h
EngineSource/Engine/src/Runtime/Rendering/Metrics/LegacyPathFlags.cpp
```

전역 싱글턴 하나에 bool을 모읍니다. `Runtime/Foundation/Singleton.h`의 기존 방식을 따르십시오.

```cpp
namespace Alice
{
    struct LegacyPathFlags
    {
        bool fullBoneConstantBuffer = false;   // P01
        bool copyPaletteEveryFrame  = false;   // P04
        bool noCameraMatrixCache    = false;   // P07
        bool animateWhenNotPlaying  = false;   // P03
        bool heapAllocWorldMatrix   = false;   // P02
        bool perParticleDrawCall    = false;   // P05
        bool staticMeshThroughSkinning = false; // 렌더독 기록: 정적 타일이 스키닝 경로로 유입
        bool outlineOnByDefault     = false;   // 렌더독 기록: outlineWidth 기본값 0.01
        bool opaqueInTransparentPass = false;  // 렌더독 기록: TransparentForward에서 불투명 재렌더

        void SetAll(bool v);
        bool AnyEnabled() const;
    };
}
```

## 항목별 되살릴 내용

각 항목은 현재 코드에 `if (LegacyPathFlags::Get().플래그)` 분기를 넣어 옛 경로를 함께 둡니다.
옛 코드를 지우고 새로 쓰지 말고, 현재 경로를 남긴 채 분기를 추가하십시오.

### P01 — 본 상수버퍼 전체 업로드

현재 코드는 `boneCount`까지만 채웁니다. 확인된 위치입니다.

- `Runtime/Rendering/DeferredRenderSystem.cpp` 5473행 부근 (`static constexpr std::uint32_t MaxBones = 1023;`, 5480행 `cb->boneCount = (std::min)(boneCount, MaxBones);`)
- `Runtime/Rendering/ForwardRenderSystem.cpp` 1138행 부근 (같은 패턴)
- `Runtime/Rendering/RenderTypes.h` 425행 (`MaxBones = 1023`)

되살릴 동작은 `MaxBones` 1023개 전체를 transpose 또는 항등행렬로 채워 Map하는 것입니다.
드로우당 약 64KB입니다. 이때 `RenderStats::boneCbBytesUploaded`가 실제 Map한 바이트를 반영해야 합니다.

### P04 — 팔레트 전체 복사

`Runtime/Engine/AdvancedAnimSystem.h` 375행 부근에서 매 프레임 팔레트를 전체 복사하던 코드입니다.
현재는 직접 연결 또는 swap으로 바뀌어 있습니다. 복사 경로를 분기로 되살리십시오.

### P07 — 카메라 행렬 캐싱 제거

`Runtime/Rendering/Camera.cpp`의 `GetViewMatrix`, `GetProjectionMatrix`, `GetViewProjectionMatrix`입니다.
현재는 `Camera.h`의 `m_viewDirty`, `m_projDirty` 플래그로 캐싱합니다.
플래그가 켜지면 캐시를 무시하고 호출마다 재계산하도록 합니다.

### P03 — 정지 상태에서도 애니메이션 재계산

`AdvancedAnimSystem` / `SkinnedAnimationSystem::Update`의 early-out을 건너뜁니다.

### P02 — 월드 행렬 힙 할당

`DeferredRenderSystem::BuildWorldMatrix`, `World.cpp:ComputeWorldMatrix_Internal`에서
부모 체인을 `std::vector<XMMATRIX>`로 받던 경로입니다.

### P05 — 파티클당 드로우콜

`Runtime/Rendering/UnityVfxMeshRenderSystem.cpp`에서 파티클당 vector 할당 + Map + Draw를 하던 경로입니다.

### 정적 메시의 스키닝 경로 유입

렌더독 캡처 기록에 남은 항목입니다. 움직이지 않는 배경 타일이 `BLENDINDICES` / `BLENDWEIGHT`가 붙은
Input Layout으로 들어가 스키닝 VS를 타고 있었습니다.

`EngineRender.cpp`의 `RenderBuildSkinnedDrawList`와 `Runtime/Rendering/SkinnedMeshRegistry.cpp`에서
스키닝 대상을 걸러내는 조건을 찾아, 플래그가 켜지면 그 필터를 통과시키도록 하십시오.
정확한 조건식은 코드를 읽고 판단하십시오. 이 문서에 줄 번호를 못 박지 않은 이유는 필터 위치가 리팩토링으로 옮겨졌을 수 있기 때문입니다. 찾은 위치를 커밋 메시지에 적으십시오.

### 아웃라인 기본값

`Runtime/Rendering/RenderTypes.h` 247행이 `outlineWidth { 0.0f }`이고,
`Runtime/Rendering/ForwardRenderSystem.h` 124행의 함수 기본 인자는 아직 `float outlineWidth = 0.01f`입니다.
플래그가 켜지면 기본값을 0.01로 써서 모든 오브젝트가 아웃라인 패스를 한 번 더 타게 합니다.

### TransparentForward에서 불투명 재렌더

`Runtime/Rendering/DeferredRenderSystem.cpp`의 TransparentForward 패스에서 불투명 오브젝트를 걸러내는
조건을 찾아, 플래그가 켜지면 필터를 통과시키도록 합니다. 1073행 부근에 해당 패스의 셰이더 정의가 있습니다.
필터 조건의 실제 위치는 코드를 읽고 확인하십시오.

## 켜는 방법

두 경로를 모두 만듭니다.

- 커맨드라인 `--legacy` 하나로 전부 켜기. 004의 인자 파서가 처리합니다.
- 에디터 패널에서 개별 체크박스. `Editor/Panels/MetricsOverlay.cpp` 안에 접이식 섹션으로 넣으십시오.
- `F10`으로 전체 토글. 002의 입력 경로를 씁니다.

**전체 토글이 한 프레임 안에 반영되어야 합니다.** 재시작이 필요하면 한 테이크에서 전환하는 영상을 못 찍습니다.
셰이더 재컴파일이나 리소스 재생성이 필요한 항목이 있으면, 초기화 시점에 양쪽 리소스를 모두 만들어 두고 포인터만 바꾸십시오.

## 완료 조건

1. `Build.bat`이 성공합니다.
2. 플래그 전부 끈 상태의 계측 값이 003 작업 전과 같습니다. 평균 `presentMs`, `drawCalls`, `psInvocations` 세 값을 작업 전후로 비교해 3% 이내여야 합니다. **현재 경로를 건드리지 않았다는 확인입니다.**
3. `F10`으로 전체를 켜면 재시작 없이 같은 프레임에서 오버레이의 수치가 바뀝니다.
4. 타일이 밀집한 씬에서 전체를 켰을 때 이 세 값이 모두 커집니다.
   - `drawCalls`
   - `psInvocations`
   - `boneCbBytesUploaded`
5. 되살린 항목마다 코드 주석에 `OPTIMIZATION_REPORT` ID 또는 렌더독 기록 참조가 있습니다.
6. `Sleep`, `busy loop`, 인위적 지연이 새로 들어가지 않았습니다. `git diff`로 확인하십시오.

## 실패 시

2번을 못 넘기면 현재 경로를 망가뜨린 것입니다. 되돌리고 어느 항목에서 어긋났는지 보고하십시오.
4번에서 값이 안 커지면 플래그가 실제 코드 경로에 닿지 않은 것입니다. 임계값을 낮추지 말고 원인을 찾으십시오.
