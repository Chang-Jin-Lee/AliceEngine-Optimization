# Editor 구조 정리

이 문서는 현재 엔진 에디터의 폴더/모듈 구조와 역할을 빠르게 이해하기 위한 요약입니다.

## 1) 최상위 구조 (Engine/src/Editor)

```
Engine/src/Editor
├── Core/
│   ├── EditorCore.h / .cpp          # 에디터 수명주기 + DrawEditorUI 오케스트레이션
│   ├── EditorCommands.h             # Undo/Redo 명령 타입 정의
│   ├── EditorUndoRedo.h             # Undo/Redo API 선언
│   ├── EditorUndoRedo.cpp           # Undo/Redo 스택 관리 + PushCommand
│   ├── EditorUIState.h              # 에디터 전역 UI 상태(g_*)
│   ├── ReflectionUI.*               # RTTR 기반 인스펙터 유틸
│   └── ViewportPicker.*             # 뷰포트 피킹
│
├── UI/
│   ├── EditorDocking.cpp            # DockSpace + 기본 레이아웃 구성
│   ├── EditorMainMenuBar.cpp        # 상단 메뉴바 + 재생/정지/디버그/토글
│   ├── EditorSceneLoadFlow.cpp      # 씬 로드/저장 확인 모달 흐름
│   └── AliceUIFactory.cpp           # AliceUI 기본 위젯 생성 유틸
│
├── Panels/
│   ├── HierarchyPanel.cpp           # Hierarchy 창
│   ├── InspectorPanel.cpp           # Inspector 창
│   ├── ProjectPanel.cpp             # Project 창 (디렉터리 트리)
│   ├── GameViewportPanel.cpp        # Game 창 (뷰포트 + 기즈모 + 드래그/드롭)
│   ├── CameraPanel.cpp              # Camera/Animation 탭
│   └── LightingPanel.cpp            # Lighting 창 (조명/스카이박스 전용)
│
├── Inspector/
│   ├── Inspector_Transform.cpp       # Transform/AnimationStatus 드로어
│   ├── Inspector_Scripting.cpp       # Script 드로어
│   ├── Inspector_Rendering.cpp       # Material/Light/PPV/ComputeEffect 드로어
│   ├── Inspector_Physics.cpp         # 물리/콜라이더/조인트 드로어
│   ├── Inspector_Camera.cpp          # 카메라 컴포넌트 드로어
│   ├── Inspector_Combat.cpp          # 전투/힛박스/트레이스 드로어
│   └── Inspector_Socket.cpp          # 소켓 컴포넌트 드로어
│
├── Project/
│   └── DirectoryTree.cpp             # 프로젝트 디렉터리 트리/컨텍스트 메뉴
│
├── Scene/
│   ├── SceneIO.cpp                   # EnsureSkinnedMeshesRegistered / SaveScene / LoadScene
│   ├── StartupSceneLoader.cpp        # BuildSettings 기반 시작 씬 로드
│   └── AssetInstantiation.cpp        # InstantiateFbxAssetToWorld
│
├── Scripting/
│   ├── ScriptReloadHelpers.h          # 스크립트 빌드/리로드 헬퍼 API
│   └── ScriptReloadHelpers.cpp        # 스크립트 빌드/리로드 구현
│
├── Tools/
│   ├── PvdSettingsWindow.cpp        # PVD 설정 창
│   └── BuildGameWindow.cpp          # 빌드 창 (Release 고정 패키징)
│
└── AssetEditors/
    ├── MaterialAssetEditor.cpp      # .mat 에셋 편집기
    ├── UICurveAssetEditor.cpp       # .uicurve 에셋 편집기
    └── PreloadAssetEditor.cpp       # Preload.json 전용 편집기
```

> **EditorCore는 “오케스트레이터”**이며, 각 창/도구/에셋 편집기는 별도 파일로 분리되어 있습니다.

---

## 2) 실행 흐름 (DrawEditorUI)

`EditorCore::DrawEditorUI()`는 아래 순서로만 동작합니다.

1. **Default PostProcess 업데이트**  
   `deferred.SetDefaultPostProcessSettings(m_defaultPostProcessSettings)`

2. **현재 씬 경로 동기화**  
   `SceneManager`의 현재 씬 경로를 `g_CurrentScenePath`에 반영

3. **Undo/Redo 처리**  
   `HandleGlobalUndoRedo()` (텍스트 입력 중/Play 중엔 동작 안 함)

4. **도킹/메뉴바**  
   `SetupDockSpaceAndDefaultLayout()`  
   `DrawMainMenuBar(...)`

5. **도구 창**  
   `DrawPvdSettingsWindow(...)`  
   `DrawBuildGameWindow()`

6. **패널**  
   `DrawHierarchyWindow(...)`  
   `DrawInspectorWindow(...)`  
   `DrawProjectWindow(...)`  
   `DrawGameViewportWindow(...)`  
   `DrawCameraWindow(...)`  
   `DrawLightingWindow(...)`

7. **에셋 에디터**  
    `DrawMaterialAssetEditorWindow(...)`  
    `DrawUICurveAssetEditorWindow()`  
    `DrawPreloadAssetEditorWindow()`

8. **씬 로드/저장 모달**  
   `HandleSceneLoadFlow(...)`

---

## 3) 전역 UI 상태 (EditorUIState.h)

에디터 전체에서 공유되는 상태들은 `EditorUIState.h`에 정의됩니다.

- 씬 상태:  
  `g_SceneDirty`, `g_CurrentScenePath`, `g_HasCurrentScenePath`
- 툴 창 표시 토글:  
  `g_ShowBuildGameWindow`, `g_ShowPvdSettingsWindow`
- 씬 로드 흐름 상태:  
  `g_RequestSceneLoad`, `g_NextScenePath`, `g_ShowSceneLoadError`, `g_SceneLoadErrorMsg`
- 에셋 에디터 상태:  
  `g_MaterialEditorOpen`, `g_MaterialEditorPath`, `g_MaterialEditorData`  
  `g_UICurveEditorOpen`, `g_UICurveEditorPath`, `g_UICurveEditorData`, `g_UICurveEditorSelected`  
  `g_PreloadEditorOpen`, `g_PreloadEditorPath`, `g_PreloadEditorItems`, `g_PreloadEditorSelected`

---

## 4) Undo / Redo 구조

- `EditorCommands.h` : 커맨드 타입 정의
- `EditorUndoRedo.h` : `ExecuteUndo`, `ExecuteRedo`, `ClearUndoStack` API
- `EditorUndoRedo.cpp` : 실제 Undo/Redo 스택 관리 + `EditorCore::PushCommand`

> **원칙**: 씬 상태 변경 시 `PushCommand(...)`로 기록하며, `g_SceneDirty`를 갱신합니다.

---

## 5) 패널별 핵심 역할

- **Hierarchy**: 엔티티 트리, 드래그&드롭, Rename, Delete 처리
- **Inspector**: 선택 엔티티 컴포넌트 편집 + 프리팹 드롭  
  (실제 컴포넌트 드로어는 `Editor/Inspector/*.cpp`에 분리)
- **Project**: Asset 디렉터리/파일 트리
  - `Preload.json` 더블클릭 시 전용 에디터 오픈
  - 우클릭 메뉴에 `Create Preload.json` 추가
- **Game**: 뷰포트, 기즈모, 픽킹, 스냅/드롭, 에디터 프리캠 단축키
  - `F` 키: 현재 선택 오브젝트 앞으로 카메라 포커스 이동
  - 동작 조건: **Editor 모드 + 재생 중 아님(Stopped)**, 선택 엔티티가 유효할 때만
  - 선택 오브젝트가 삭제된 상태면 선택값을 정리하고 이동하지 않음
- **Camera**: 카메라 설정 + 스키닝 애니메이션 제어 탭
- **Lighting**: 조명 파라미터, 스카이박스, PBR 설정  
  (Post‑Process/Bloom은 **PostProcessVolume**에서만 설정)

> **UE 스타일 적용**: Post‑Process는 **PP Volume만** 사용합니다.  
> Lighting 패널의 Post‑Process/Bloom UI는 제거되었습니다.

---

## 6) 도킹 기본 배치

- 오른쪽 컬럼에서 **Inspector와 Lighting은 같은 도크 노드에 탭**으로 배치됩니다.

---

## 7) 새 창/도구 추가 방법

1. `Editor/` 아래 적절한 폴더에 새 cpp 파일 생성  
   (예: `Panels/NewPanel.cpp`)
2. `EditorCore.h`에 private 함수 선언 추가
3. `EditorCore::DrawEditorUI`에서 함수 호출 추가
4. `CMakeLists.txt`에 소스 등록
5. 필요 시 `EditorUIState.h`에 전역 상태 추가

---

## 8) 현재 남아있는 “큰 덩어리”

현재 `EditorCore.cpp`에 큰 덩어리 로직은 거의 남아있지 않습니다.

- 스크립트 빌드/리로드 로직은 `Editor/Scripting/ScriptReloadHelpers.*`로 분리됨

---

## 9) 개발 규칙 요약

- 창 분리는 “기능 단위” 기준으로 유지
- `EditorCore`는 **오케스트레이터** 역할만 수행
- 전역 상태는 `EditorUIState.h`에만 둔다
- Undo/Redo는 커맨드 기반으로 기록한다

---

이 문서를 기준으로 새로운 패널 추가, 기능 이동, 리팩토링을 진행하면 됩니다.
