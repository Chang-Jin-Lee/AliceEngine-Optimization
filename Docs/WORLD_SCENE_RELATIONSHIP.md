# World와 Scene의 관계 분석 (최신 구조)

## 구조 개요

```mermaid
graph TB
    subgraph "Engine"
        Engine[Engine]
    end

    subgraph "Runtime 소유 객체"
        World[World<br/>ECS 컨테이너]
        SceneMgr[SceneManager<br/>씬 관리자]
        ScriptSys[ScriptSystem]
        RenderSys[RenderSystems]
    end

    subgraph "SceneManager 관리 객체"
        IScene[IScene]
        SampleScene[SampleScene]
    end

    Engine -->|소유| World
    Engine -->|소유| SceneMgr
    Engine -->|소유| ScriptSys
    Engine -->|소유| RenderSys

    SceneMgr -->|참조| World
    SceneMgr -->|소유| IScene
    IScene <|-- SampleScene

    IScene -.->|참조로 사용| World
    ScriptSys -.->|참조로 사용| World
    RenderSys -.->|참조로 사용| World
```

---

## 핵심 개념

### 1. World (ECS 컨테이너)
**위치**: `Engine/src/Runtime/ECS/World.h`

- World는 **순수 데이터 컨테이너**
- 씬 개념을 알지 못함
- Sparse Set 기반으로 컴포넌트를 연속 저장

---

### 2. Scene (IScene)
**위치**: `Engine/src/Runtime/Resources/Scene.h`

- 씬 로직 단위
- World를 **참조로 받아서 사용**
- OnEnter/OnExit/Update로 생명주기 관리

---

### 3. SceneManager
**위치**: `Engine/src/Runtime/Resources/Scene.h`

- World를 **참조**로 사용
- 현재 씬을 **소유**
- 전환은 요청 → 안전 지점 커밋

**핵심 API**:
```cpp
bool SwitchToImmediate(const char* name);        // 안전 지점에서만 사용
bool SwitchTo(const char* name);                 // 요청 등록
bool LoadSceneFileRequest(const path& file);     // 요청 등록
bool CommitPendingSceneChange(World& world);     // 엔진이 커밋
```

---

## 관계 정리

### World가 Scene을 참조하는가?
**아니요.** World는 Scene을 전혀 모릅니다.

### Scene이 World를 참조하는가?
**예.** Scene은 World를 참조로 받아 사용합니다.

### 실제 관계

```
Engine
  ├─ World (소유)
  ├─ SceneManager (소유)
  │     ├─ World& (참조)
  │     └─ IScene (소유)
  │           └─ World& (참조)
  └─ ScriptSystem / RenderSystems (World 참조)
```

---

## 실제 사용 예시

```cpp
void SampleScene::OnEnter(World& world, ResourceManager& /*resources*/)
{
    m_cubeEntity = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(m_cubeEntity);
    transform.SetPosition(0.0f, 0.0f, 0.0f);
}
```

---

## 씬 전환 시나리오 (요약)

```cpp
// Script/Gameplay
sceneManager->SwitchTo("SampleScene"); // 요청만 등록

// Engine Update 안전 지점
sceneManager->CommitPendingSceneChange(world);  // 실제 전환
```

**중요**: 즉시 전환은 `SwitchToImmediate()`로만 수행되며
이는 엔진 초기화/프레임 경계 안전 지점에서만 호출되어야 합니다.

---

## 결론

- **World는 데이터 컨테이너, Scene은 로직 컨테이너**
- 씬 전환은 **요청/커밋 구조**로 안전하게 처리
- Engine이 World와 SceneManager를 소유하고 전체 흐름을 제어
