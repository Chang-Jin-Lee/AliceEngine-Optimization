# AliceRenderer 엔진 아키텍처 구조도

## 전체 시스템 계층 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                    Engine (Runtime/Engine)                      │
│  - 윈도우/메시지 루프, Update/Render/Physics 제어               │
│  - 에디터 모드/게임 모드 전환                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Runtime Modules                         │
│                                                                 │
│  ECS         Resources        Scripting        Rendering        │
│  Physics     Audio            UI               Gameplay         │
│  Input       Importing        Foundation                        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                          Editor Modules                         │
│  - Editor/Core (ImGui 기반 편집기)                              │
│  - Editor/Tools (Blueprint 등)                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## ECS (Entity Component System) 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                              World                              │
│  - EntityId 관리 (SlotMap + Generation)                         │
│  - Sparse Set 기반 컴포넌트 저장                                 │
│                                                                 │
│  ComponentStorage<TransformComponent> (Runtime/ECS/Components)  │
│  ComponentStorage<CameraComponent>     (Runtime/Rendering/...)  │
│  ComponentStorage<Phy_RigidBodyComponent> (Runtime/Physics/...)  │
│  ...                                                             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                           GameObject                            │
│  - World + EntityId 래퍼 (Unity 스타일 API)                     │
│  - Get/Add/RemoveComponent<T>()                                 │
└─────────────────────────────────────────────────────────────────┘
```

**컴포넌트 위치 규칙**
- Core: `Runtime/ECS/Components`
- 기능별: 각 모듈의 `Components` 폴더
  - Rendering/Physics/Audio/UI/Scripting/Gameplay 등

---

## 게임 루프 흐름 (요약)

```
Engine::Run()
  ├─ 메시지 루프 (Win32)
  ├─ Update
  │   ├─ Timer/Input 갱신
  │   ├─ SceneManager Update
  │   ├─ ScriptSystem Tick (Awake/Start/Update/Late/Fixed)
  │   ├─ 안전 지점: CommitPendingSceneChange
  │   ├─ PhysicsSystem 업데이트/시뮬레이션
  │   ├─ Animation/Gameplay 시스템 업데이트
  │   └─ World 지연 파괴 처리
  └─ Render
      ├─ Forward/Deferred Render
      ├─ DebugDraw, Effect/Compute
      ├─ UI(RenderWorld/RenderScreen)
      └─ Present
```

---

## 주요 모듈 요약

### Runtime/Resources
- `SceneManager`, `SceneFile`, `ResourceManager`, `Prefab`
- 씬 전환은 **요청 → 안전 지점 커밋**

### Runtime/Scripting
- `IScript`, `ScriptSystem`, `ScriptFactory`, `ScriptHotReload`
- Script 빌드: `ScriptsBuild`에서 별도 DLL 생성

### Runtime/Rendering
- D3D11 백엔드 + Forward/Deferred 렌더링
- Render 컴포넌트는 `Runtime/Rendering/Components`
- 최신 렌더링 상세 문서:
  - `엔진설명서/RENDERING_LATEST_AND_OUTLINE.md`
  - `엔진설명서/LIGHTING_ENHANCEMENTS.md`

### Runtime/Physics
- PhysX 래퍼 + ECS 브릿지 `PhysicsSystem`
- 물리 컴포넌트는 `Runtime/Physics/Components`

### Runtime/UI
- `UIRenderer`가 ECS 기반 UI 컴포넌트를 렌더링
- UI 컴포넌트는 `Runtime/UI/Components`

---

## 파일 구조 (요약)

```
Engine/src
├── Runtime/
│   ├── Foundation/
│   ├── ECS/
│   ├── Engine/
│   ├── Input/
│   ├── Resources/
│   ├── Rendering/
│   ├── Physics/
│   ├── Audio/
│   ├── UI/
│   ├── Scripting/
│   ├── Importing/
│   └── Gameplay/
├── Editor/
│   ├── Core/
│   └── Tools/
├── Samples/
│   ├── Sandbox/
│   └── Scenes/
└── ThirdParty/
    └── json/
```

---

## 주요 의존성 관계

```
Engine
  ├─ World (소유)   ← Runtime/ECS
  ├─ SceneManager (소유) ← Runtime/Resources
  ├─ ScriptSystem (소유) ← Runtime/Scripting
  ├─ RenderSystems (소유) ← Runtime/Rendering
  ├─ PhysicsSystem (소유) ← Runtime/Physics
  ├─ UIRenderer (소유) ← Runtime/UI
  └─ EditorCore (소유, 에디터 모드)
```

---

## 설계 패턴 요약

1. **ECS (Entity Component System)**
   - Sparse Set 기반 컴포넌트 저장

2. **PIMPL (Pointer to Implementation)**
   - `Engine` 내부 구현 분리 (`EngineImpl.h`)

3. **Factory / Registrar**
   - `SceneFactory`, `ScriptFactory`

4. **Delegate / 이벤트**
   - Scene 로드 후 콜백 등

5. **요청/커밋 패턴**
   - 씬 전환은 요청 후 안전 지점에서 커밋
