# AliceRenderer 엔진 구조 가이드

## 목차
1. [전체 엔진 구조](#전체-엔진-구조)
2. [핵심 시스템](#핵심-시스템)
3. [ScriptsBuild 시스템](#scriptsbuild-시스템)
4. [빌드 프로세스](#빌드-프로세스)
5. [Animation Root Motion](#animation-root-motion)
6. [VFX Pipeline](#vfx-pipeline)
7. [Rendering 최신 업데이트](#rendering-최신-업데이트)

---

## 전체 엔진 구조

### ️ 소스 계층 구조 (Engine/src 기준)

```
Engine/src
├── Runtime/                 # 런타임 공용 모듈
│   ├── Foundation/          # Logger, Delegate, ThreadSafety, StringUtils 등
│   ├── ECS/                 # World, GameObject, ComponentRegistry
│   │   └── Components/      # Transform, ID, ComponentStorage
│   ├── Engine/              # 엔진 루프/윈도우/타이머
│   ├── Input/               # InputSystem
│   ├── Resources/           # ResourceManager, Scene, SceneFile, Prefab
│   │   └── Serialization/   # RTTR/JSON 직렬화
│   ├── Rendering/           # D3D11, RenderSystems, ShaderCode
│   │   ├── Components/      # Camera, Light, Mesh, Material, PostProcess 등
│   │   ├── Data/            # Material, Vertex 등 렌더링 데이터
│   │   └── D3D11/           # RenderDevice/Backend
│   ├── Physics/             # PhysX 래퍼 + PhysicsSystem
│   │   ├── Components/      # Phy_* 컴포넌트
│   │   └── Module/          # PhysX 모듈 구현부
│   ├── Audio/               # AudioSystem, SoundManager
│   │   └── Components/      # AudioSource/Listener 등
│   ├── UI/                  # AliceUI + UIRenderer
│   │   └── Components/      # UI* 컴포넌트
│   ├── Scripting/           # ScriptSystem, ScriptHotReload
│   │   └── Components/      # ScriptComponent
│   ├── Importing/           # FBX/Asset Import
│   └── Gameplay/            # 실제 게임 로직
│       ├── Animation/
│       ├── Combat/
│       └── Sockets/
│
├── Editor/                  # 에디터 전용
│   ├── Core/                # EditorCore, ViewportPicker
│   └── Tools/               # Blueprint 등
│
├── Samples/                 # 샘플/테스트
│   ├── Sandbox/
│   └── Scenes/
│
└── ThirdParty/              # 외부 라이브러리
    └── json/
```

### 프로젝트 구성

```
AliceRenderer
├── Engine (정적 라이브러리)
├── Launch (에디터 실행 파일)
├── AlicePlayer (게임 실행 파일)
└── AliceScripts.dll (ScriptsBuild 결과)
```

---

## 핵심 시스템

### 1. **Engine (메인 루프/초기화)**

**위치**: `Engine/src/Runtime/Engine/` (`Engine.cpp`, `EngineInitialize.cpp`, `EngineUpdate.cpp`, `EngineRender.cpp`, `EnginePhysics.cpp`)

**역할**:
- 윈도우 생성 및 메시지 루프
- Runtime 모듈 초기화/종료
- Update/Render/Physics 흐름 제어
- 에디터 모드/게임 모드 전환

**주요 보유 객체** (요약):
- `World`, `SceneManager`, `ScriptSystem`, `InputSystem`, `ResourceManager`
- `ForwardRenderSystem` / `DeferredRenderSystem`
- `PhysicsSystem` (ECS 브릿지), `PhysicsModule`(PhysX)
- `UIRenderer`, `AudioSystem`, `SkinnedMeshRegistry`

**안전한 씬 전환**:
- 스크립트/게임플레이는 **즉시 전환 금지**
- `SceneManager::SwitchTo()` / `LoadSceneFileRequest()`로 **요청**
- `Engine::Update()` 안전 지점에서 `CommitPendingSceneChange()`로 커밋

**게임 모드 로딩/프리로드**:
- 최종 빌드(게임 모드)에서는 초기 로딩 화면 + 프리로드가 선행됩니다.
- `InitializePreloadAndLoadingScreen()`이 Preload.json을 읽고 타입별 선로딩을 수행합니다.
- 완료 후 “클릭하여 시작”을 표시하고 입력 시 씬 진입합니다.
- 상세 문서: `엔진설명서/PRELOAD_SYSTEM.md`

---

### 2. **World (ECS 컨테이너)**

**위치**: `Engine/src/Runtime/ECS/World.h`, `World.cpp`

**역할**:
- Entity/Component 관리 (Sparse Set 기반)
- `GameObject` 래퍼 제공

**컴포넌트 배치 규칙**:
- 코어 컴포넌트: `Runtime/ECS/Components`
  - `TransformComponent`, `IDComponent`, `ComponentStorage`
- 기능 컴포넌트: 각 모듈의 `Components` 폴더
  - Rendering/Physics/Audio/UI/Scripting/Gameplay 등

---

### 3. **Scene/Resource 시스템**

**SceneManager 위치**: `Engine/src/Runtime/Resources/Scene.h/.cpp`

**Scene 흐름**:
- 코드 씬: `SceneFactory` + `REGISTER_SCENE()` 등록
- 파일 씬: `.scene` JSON (RTTR 기반 직렬화)
- 요청 기반 전환 → 엔진 안전 지점에서 커밋

**ResourceManager 위치**: `Engine/src/Runtime/Resources/ResourceManager.h/.cpp`

**역할**:
- 논리 경로(`Resource/`, `Assets/`, `Cooked/`) 해석
- 텍스처/텍스트/JSON/기타 리소스 로드

---

### 4. **ScriptSystem (스크립트 라이프사이클)**

**위치**: `Engine/src/Runtime/Scripting/`

**구성 요소**:
- `IScript` / `ScriptAPI` / `ScriptSystem`
- `ScriptFactory` / `ScriptHotReload`

**라이프사이클**:
```
Awake → Start → Update → LateUpdate → FixedUpdate → OnDestroy
```

**특징**:
- Script 요청(씬 전환)은 `SceneManager`에 **지연 요청**
- Runtime에서 DLL 핫리로드 지원

---

### 5. **Rendering**

**위치**: `Engine/src/Runtime/Rendering/`

**구성**:
- `D3D11RenderDevice` (Backend)
- `ForwardRenderSystem` / `DeferredRenderSystem`
- `EffectSystem`, `ComputeEffectSystem`, `DebugDrawSystem`
- 렌더링 컴포넌트: `Runtime/Rendering/Components`
- 렌더링 데이터: `Runtime/Rendering/Data`

**스키닝/애니메이션**:
- `Runtime/Gameplay/Animation/` + `SkinnedMeshRegistry`
- 프리로드 단계에서 애니메이션 프리컴퓨트를 등록해 런타임에서 재사용합니다.

**IBL/스카이박스 (PBR 환경광 제어)**:
- **머티리얼별 환경광 스케일**  
  `MaterialComponent`에 `envDiffuseStrength`, `envSpecularStrength`가 있어 IBL Diffuse/Specular 기여도를 스케일함.  
  기본값은 1.0, 0이면 해당 항목의 환경광 영향 없음.  
  (Forward/Deferred 공통, Linear space에서 IBL term에 곱)
- **스카이박스 해상도 선택 (HDR/MDR)**  
  LightingPanel에서 스카이박스 해상도(HDR/MDR)를 선택할 수 있음.  
  내부적으로 `EnvHDR.dds` / `EnvMDR.dds` suffix를 사용.  
  현재 선택은 `EngineSettings.json`에 저장되어 최종 빌드에도 적용됨.
- **스카이박스 Off 동작**  
  Off 시 Skybox SRV + IBL SRV를 제거해 마지막 스카이박스가 남지 않도록 처리.
- **커스텀 스카이박스 경로**  
  LightingPanel의 `Browse...`로 `Resource/Skybox/...` 폴더를 선택 가능.  
  선택한 폴더/프리픽스는 설정으로 저장됨.

**그림자 강도 제어**:
- 전역 그림자 강도: LightingPanel에서 `Shadow Strength`로 조절, `EngineSettings.json`에 저장됨.
- 머티리얼 그림자 강도: `MaterialComponent.shadowStrength` (UI: **Shadow Intensity**). 전역 값과 곱해져 적용됨.
- ToonPBREditable 전용: `Toon Shadow Strength (Editable)`로 별도 보정 가능.
- ToonPBREditable Ramp Intensity: `MaterialComponent.toonPbrRampIntensity`로 **툰의 검은 밴드만 완화**.
- UI는 `0~1` 정규화 값이며 셰이더에서 더 강한 범위로 매핑되어 적용됨.
- 그림자 선명도는 `ShadowSettings.mapSizePx`(해상도)와 `shadowPcfRadius`(PCF 반경), `m_shadowResolutionScale`로 조절.

---

### 6. **Physics**

**위치**: `Engine/src/Runtime/Physics/`

**구성**:
- `PhysicsModule` (PhysX 래퍼)
- `PhysicsSystem` (ECS 브릿지)
- 물리 컴포넌트: `Runtime/Physics/Components`

---

### 7. **UI (AliceUI)**

**위치**: `Engine/src/Runtime/UI/`

**특징**:
- `UIRenderer`가 ECS(World) 기반 UI 컴포넌트를 렌더링
- UI 관련 컴포넌트는 `Runtime/UI/Components`에 위치

---

### 8. **Audio**

**위치**: `Engine/src/Runtime/Audio/`

**구성**:
- `AudioSystem`, `SoundManager`
- `Runtime/Audio/Components` (AudioSource/Listener 등)

---

### 9. **Gameplay & Importing**

- `Runtime/Gameplay`: 전투, 애니메이션, 소켓 등 게임 로직
- `Runtime/Importing`: FBX, 에셋 임포트
- FBX 임포트는 **동일 파일명 충돌을 회피**하기 위해 필요 시 해시 키를 부여합니다.

---

## Animation Root Motion

**정리 문서**: `엔진설명서/ROOT_MOTION.md`

**핵심 요약**:
- Root Motion은 **Extract → Consume → (선택) Apply**로 처리
- 모드: `NoExtraction`, `Ignore`, `FromEverything`
- RootLock: `AnimFirstFrame`(기본), `Zero`
- 기본 축 필터: XZ 이동 + Yaw 회전

**관련 파일**:
- `Engine/src/Runtime/Gameplay/Animation/AdvancedAnimationComponent.h`
- `Engine/src/Runtime/Gameplay/Animation/AdvancedAnimator.h`
- `Engine/src/Runtime/Engine/AdvancedAnimSystem.cpp`

---

## ScriptsBuild 시스템

### 개요

`ScriptsBuild`는 `Assets/Scripts`를 별도 DLL(`AliceScripts.dll`)로 빌드하는 프로젝트입니다.

**목적**:
- 핫 리로드 지원
- 엔진/스크립트 빌드 분리
- RTTR 등록 공유

### 주요 포인트

- 스크립트 공용 PCH: `Engine/src/Runtime/Scripting/ScriptPCH.h`
- `IScript.h`에서 `TransformComponent` 기본 include
- Debug PDB 이름을 타임스탬프 기반으로 변경해 잠금 회피
- Release는 `/DEBUG:NONE` 옵션으로 PDB 비활성화

### 핫 리로드 흐름 (에디터)
1. `Reload Scripts` 클릭
2. `cmake -S ScriptsBuild -B ScriptsBuild/build`
3. `cmake --build ScriptsBuild/build --target AliceScripts`
4. DLL 복사 → `ScriptHotReload_Unload` → `ScriptHotReload_Reload`

---

## 빌드 프로세스

### 초기 빌드

**권장**: 루트의 `Build.bat` 실행
- 내부적으로 `Engine/Setup.bat` + `Engine/build_msvc.cmd` 수행

### 수동 빌드 예시

```
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug

cmake -S ScriptsBuild -B ScriptsBuild/build -G "Visual Studio 17 2022"
cmake --build ScriptsBuild/build --config Debug --target AliceScripts
```

### 에디터 Build Game 버튼 (최종 패키징)

- 에디터가 **Debug/Release 어떤 구성으로 실행 중이든** 최종 게임 빌드는 **Release로 고정**됩니다.
  - `cmake --build build --config Release --target AlicePlayer`
- ScriptsBuild도 **Release로 빌드**하여 최종 실행 파일 옆에 배치합니다.
  - `ScriptsBuild/build/Release/AliceScripts.dll` → `build/bin/Release/dll/`
- 패키징 결과는 `Export/Bin`에 정리됩니다.
  - `AlicePlayer.exe`, `dll/`, `BuildSettings.json`, `EngineSettings.json`
  - `Cooked/`, `Metas/` 디렉터리 포함

관련 문서:
- `엔진설명서/BUILD_OUTPUTS_AND_ICONS.md`

---

## VFX Pipeline

**핵심 포인트**:
- Unity ParticleSystem 프리팹 → `effect.json` export
- 엔진에서 `UnityVfxComponent`가 `ComputeEffectSystem`으로 매핑 실행

관련 문서:
- `엔진설명서/VFX_PIPELINE.md`

---

## Rendering 최신 업데이트

렌더링 기능의 최신 기준은 아래 문서를 우선 참고합니다.

- `엔진설명서/RENDERING_LATEST_AND_OUTLINE.md`

이 문서에 포함된 핵심 내용:

- Deferred Edge Detection 아웃라인 구현 구조
  - `OutlineData` GBuffer 확장
  - LightPass Sobel 합성 방식
  - 머티리얼별 `outlineColor` / `outlineWidth` 제어 경로
- HalfCut(카메라 화면 분할) 포스트프로세스
  - `PostProcessVolume` Override 기반 제어
  - Forward/Deferred 공통 톤매핑 단계 적용
  - 데모 씬: `Assets/Scenes/Camera/CameraHalfCut.scene`
- HalfCut 확장(절단면 수학 FX + FOV 커브 + VFX 동기화)
  - `splitFxIntensity/Width/Speed/TimeSec` 추가
  - `UICurveAsset` 기반 FOV 비선형 제어
  - VFX 중점 스폰 + 종료 구간 fade-out
  - 구현/트러블슈팅 상세: `엔진설명서/RENDERING_LATEST_AND_OUTLINE.md` 11장
- LightingPanel 최신 정책
  - 톤매핑 직접 슬라이더 제거
  - PostProcessVolume(Unbound) 기반 조정 안내
- 그림자/툰/IBL/스카이박스 관련 최신 UI 및 동작 기준

---

## 요약

- `Engine/src/Runtime`에 런타임 모듈이 모듈별로 정리됨
- 컴포넌트는 **모듈별 Components 폴더**에 분산 배치
- Scene 전환은 **요청 → 안전 지점 커밋** 구조
- ScriptsBuild는 PCH + 핫리로드 기반으로 빠른 반복을 지원

