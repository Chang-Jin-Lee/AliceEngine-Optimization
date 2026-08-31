# GameMode 프리로드/로딩 화면 시스템

이 문서는 **최종 빌드(게임 모드)에서의 프리로드 + 로딩 화면 흐름**을 정리합니다.  
목표는 “초기 로딩을 명시적으로 보여주고 이후 씬 전환에서 끊김을 최소화”하는 것입니다.

---

## 1) 핵심 개념

- **Preload.json**에 등록된 리소스를 게임 시작 시 선로딩합니다.
- 로딩 화면(UI)에서 진행률과 상태 문구를 표시합니다.
- 로딩 완료 후 “클릭해서 시작”을 보여주고 입력 시 실제 씬 진입.
- **게임 모드에서만 동작**하며 에디터 모드에서는 프리로드를 건너뜁니다.

---

## 2) Preload.json 위치와 포맷

### 위치 (권장)
- `Assets/Startup/Preload.json`
- 폴백: `Assets/Preload.json`

### 허용 경로 규칙
- `Assets/`, `Resource/`, `Cooked/`만 허용
- `..` 포함 경로는 무시
- 중복 경로는 제거

### 지원 포맷
아래 3가지 중 하나면 모두 인식합니다.

```json
[
  "Resource/Icon/AliceBanner.png",
  "Resource/Fonts/NotoSansKR-Regular.ttf"
]
```

```json
{
  "preload": [
    "Resource/Icon/AliceBanner.png",
    "Resource/Fonts/NotoSansKR-Regular.ttf"
  ]
}
```

```json
{
  "startup": [
    "Assets/Startup/Preload.json",
    "Resource/Fonts/NotoSansKR-Regular.ttf"
  ]
}
```

---

## 3) 에디터 워크플로우

### 3.1 Preload 에디터 열기
- Project 창에서 `Preload.json`을 **더블클릭**하면 전용 에디터 창이 열립니다.

### 3.2 Preload 에디터 기능
- 목록 추가: `Add...` 버튼으로 **다중 선택** 가능
- 드래그앤드롭: Project 창에서 파일 드롭 가능
- 순서 조정: `Up/Down` 버튼
- 삭제: 선택 삭제 / 전체 삭제
- 경로 검증: 허용 prefix만 통과
- 중복 방지 및 파일 존재 경고 표시
- 저장: JSON은 자동 저장, `.tmp → rename` 방식으로 안전 저장

### 3.3 Preload.json 생성
- Project 트리 **우클릭 → Create Preload.json**
- 생성 후 자동으로 Preload 에디터가 열립니다.

---

## 4) 게임 모드 로딩 화면 흐름

**위치**: `Engine/src/Runtime/Engine/EngineInitialize.cpp`

### 전체 흐름 (요약)
1. 게임 모드 시작 시 `InitializeUI()` 완료
2. `InitializePreloadAndLoadingScreen()` 진입
3. Preload.json 로드 → 경로 검증/중복 제거/존재 체크
4. 로딩 UI 월드 구성
5. **시스템 초기화 단계(Audio / RenderSystem / ComputeEffectSystem)** 수행
6. 프리로드 진행 (타입별 실 로딩)
7. 완료 상태 표시 → 클릭 대기 → 실제 씬 진입

### 로딩 UI 구성 요소
- **Banner**: `Resource/Icon/AliceBanner.png`
- **상태 텍스트**: “쉐이더 컴파일중.” (점 애니메이션)
- **프로그레스 바**: `UIGaugeComponent`
- **완료 힌트**: “마우스를 클릭하여 시작”

### 진행률 연출 방식
- 시스템 초기화(오디오/렌더/컴퓨트)와 프리로드를 **가중치 기반**으로 분배해 표시
- 프리로드 리스트가 작아도 **즉시 100%로 튀지 않도록** 최소 시간/스무딩 적용
- 로딩 중 `WM_QUIT` 수신 시 정상 종료 처리

---

## 5) 프리로드 타입별 동작

**PreloadContext**에서 확장자/타입에 따라 실제 로드를 수행합니다.

### 타입별 처리 요약
- `.fbxasset`
  - `LoadFbxInstanceAssetAuto` → `SkinnedMeshRegistry` 등록
  - 애니메이션 프리컴퓨트까지 수행
- `.fbx`
  - `FbxImporter::Import` → 메시 등록
  - **해시 키가 있는 경우 해당 키 우선**
  - 애니메이션 프리컴퓨트 수행
- `effect.json`
  - `UnityVfxMeshRenderSystem::PreloadEffect`
  - 머티리얼/메시/텍스처까지 선로딩
- `.prefab`
  - 프리팹 바이너리(`LoadSharedBinaryAuto`)를 먼저 캐시
  - 프리팹 JSON에서 `UnityVfx.effectPath`를 스캔(루트 + `entities[]`)
  - `effectPath`는 논리 경로로 정규화하고 `effect.json`만 허용
  - 수집된 이펙트 경로를 `PreloadByType()`로 재귀 처리하여 실제 이펙트 프리로드 수행
- 이미지 (`ResourceManager::IsImageLogicalPath`)
  - `ForwardRenderSystem` 또는 `DeferredRenderSystem`의 텍스처 캐시에 등록
- 오디오 (`.wav/.ogg/.mp3/...`)
  - `Sound::LoadAuto`로 메모리 로드
- 그 외
  - `LoadSharedBinaryAuto`로 바이트 캐시 유지

### 캐시 유지 전략
- 바이너리 블롭: `EngineImpl::m_preloadedBlobs`에 보관
- 텍스처: 렌더 시스템 내부 캐시
- 스키닝 메시: `SkinnedMeshRegistry`
- 애니메이션 프리컴퓨트: `SkinnedMeshRegistry`에 저장

### FBX 충돌 대응 (중요)
- 동일 이름의 FBX가 있을 수 있으므로, **.fbxasset의 sourceFbx를 확인해 키를 매칭**합니다.
- 매칭 실패 시 해당 키는 무시하여 **잘못된 프리컴퓨트 적용을 방지**합니다.
- 스크립트/씬에서는 **`Assets/Fbx/xxx.fbxasset` 경로 사용을 권장**합니다.

---

## 6) 안전 장치

- `WM_QUIT` 수신 시 `m_initCanceled` 처리 후 정상 종료
- 로딩 단계 실패는 로그로 구분
- Preload.json 누락/파싱 실패 시에도 **크래시 없이** 진행

---

## 7) 참고 포인트

- 로딩 화면은 **프리로드 단계에서만** 표시됩니다.
- 프리로드 리스트가 작으면 로딩이 빨라 보이므로, 필요하면 목록을 늘려 체감 속도를 보정합니다.
- 프리로드는 기본적으로 **단일 스레드**로 진행됩니다.
- 무거운 FBX/이미지가 있으면 **UI가 잠깐 멈춘 것처럼 보일 수 있음** (기능상 정상).
- `Preload.json`에 이펙트 프리팹(`.prefab`)을 넣으면, 프리팹 본문뿐 아니라 내부 `UnityVfx.effectPath`의 `effect.json`까지 선로딩됩니다.
