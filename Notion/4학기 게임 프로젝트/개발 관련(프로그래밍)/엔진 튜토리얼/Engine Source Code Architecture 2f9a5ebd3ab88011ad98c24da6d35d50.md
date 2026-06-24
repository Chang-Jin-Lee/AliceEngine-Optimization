# Engine Source Code Architecture

# 🏗️ Engine Source Code Architecture

> 2026.01.31 Refactoring Status: Applied
> 
> 
> 기존의 혼재된 폴더 구조를 **모듈 중심(Module-centric)** 아키텍처로 변경했습니다.
> 
> 핵심 목표는 **"순환 참조(Circular Dependency) 방지"**와 **"직관적인 파일 위치 파악"**입니다.
> 

---

## 1. 📂 Top-Level Hierarchy (대분류)

엔진의 소스 코드는 빌드 포함 여부와 역할에 따라 4가지 계층으로 나뉩니다.

| **폴더명** | **역할** | **비고** |
| --- | --- | --- |
| **`Runtime/`** | 게임 실행에 필수적인 엔진 코어 및 시스템 | **Shipping 빌드 포함** |
| **`Editor/`** | 에디터 구동을 위한 도구, 패널, 뷰포트 코드 | **Shipping 빌드 제외** |
| **`Samples/`** | 샌드박스(Sandbox), 테스트 씬(Scenes) | 개발용 |
| **`ThirdParty/`** | 외부 라이브러리 (Json, ImGui, FMOD 등) |  |

---

## 2. ⚠️ 핵심 원칙: Component 분산 배치

> [중요] src/Components 통합 폴더는 삭제되었습니다.
> 

모든 컴포넌트는 **자신이 속한 기능(Module) 폴더** 내부의 `/Components` 하위 폴더로 이동합니다. 이는 모듈 간의 의존성을 명확히 하고 순환 참조를 방지하기 위함입니다.

- ❌ `src/Components/PointLightComponent.h` (삭제됨)
- ✅ **`src/Runtime/Rendering/Components/PointLightComponent.h`** (이동됨)
- ✅ **`src/Runtime/Physics/Components/Phy_RigidBodyComponent.h`** (이동됨)

---

## 3. 📁 Runtime 폴더 상세 구조 (Module List)

`Runtime` 하위 폴더는 **도메인(기능)** 단위로 격리됩니다.

### 🔹 Core Foundation & System

- **`Foundation/`**: 엔진 전역에서 쓰이는 유틸리티. (Logger, Math, String, Thread, Delegate)
- **`ECS/`**: Entity, World, System 등 ECS 아키텍처의 **기반 클래스**.
- **`Engine/`**: 엔진 초기화, 메인 루프(GameLoop), 진입점(EntryPoint).
- **`Input/`**: 키보드/마우스 입력 처리 시스템.
- **`Resources/`**: 리소스 매니저, 씬(Scene) 로드/저장, 직렬화(Serialization).

### 🔹 Functional Modules (기능 모듈)

- **`Rendering/`**: DX11, 셰이더, 렌더링 파이프라인.
    - 📂 `*Components/` 포함: Camera, Light, Mesh, Particle 등*
    - 📂 `*Data/` 포함: Vertex.h, Material.h 등 데이터 구조*
- **`Physics/`**: PhysX 래퍼 및 물리 시뮬레이션.
    - 📂 `*Components/` 포함: Rigidbody, Collider, Joint 등*
- **`Audio/`**: 사운드 시스템 (FMOD 래핑).
    - 📂 `*Components/` 포함: AudioSource, AudioListener*
- **`UI/`**: (구 AliceUI) UI 시스템 및 위젯 처리.
    - 📂 `*Components/` 포함: Button, Image, Text 등*
- **`Scripting/`**: 스크립트 엔진 연동.
    - 📂 `*Components/` 포함: ScriptComponent*
- **`Importing/`**: 외부 에셋 로더 (FbxLoader 등).

### 🔹 Gameplay Layer (콘텐츠)

- **`Gameplay/`**: 엔진 기능이 아닌, **실제 게임 콘텐츠 로직**.
    - 포함: 캐릭터(Character), 전투(Combat), FSM, 아이템 등.
    - 📂 `*Components/` 포함: Health, Attack, Skill, Movement 등*
    - *Note: 이 폴더는 엔진의 모든 기능을 참조할 수 있지만, 엔진 코어는 이 폴더를 참조해서는 안 됩니다.*

---

## 4. ❓ FAQ: 파일 위치 가이드

| **찾으려는 파일 유형** | **위치 (Path)** |
| --- | --- |
| **TransformComponent** | `Runtime/ECS` (모든 객체의 기본이므로 가장 하위 레이어) |
| **Vertex, Material 구조체** | `Runtime/Rendering/Data` (그래픽스 데이터로 분류) |
| **플레이어 HP/MP/공격** | `Runtime/Gameplay/Components` (게임 로직) |
| **FBX 로더/임포터** | `Runtime/Importing` |
| **블루프린트 에디터** | `Editor/Tools` |

---

## 5. ⚙️ Include 경로 설정 가이드 (C++)

프로젝트 속성의 **Additional Include Directories**는 `$(ProjectDir)src` 하나만 등록합니다. 코드에서는 아래와 같이 **풀 경로(Full Path)**를 명시하여 소속을 드러냅니다.

C++

# 

`// Good Examples 👍
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Gameplay/Character/Player.h"

// Bad Examples 👎
#include "../../../Rendering/Components/CameraComponent.h"  // 상대 경로 지양
#include "CameraComponent.h"                                // 모호한 경로 지양`