# EGOSIS 엔진 리팩토링 ②~⑥ 설계 문서

- 날짜: 2026-07-06
- 대상 저장소: `D:\Github\EGOSIS_Refactoring` (dev 브랜치)
- 선행 작업: ① 애셋 복사·QA 가능화, Play 버튼 ScriptReloadFailed 수정, DLL 최소화 (완료)

## 배경과 목표

| # | 문제 | 목표 |
|---|------|------|
| ② | 없는 애셋을 매 프레임 디스크에서 재시도(fallback)해 프레임드랍 + 로그 스팸 | 실패 캐시로 재시도 제거, 프레임 안정화 |
| ③ | 에디터 Play마다 스크립트 빌드 실행 | 변경 없으면 빌드/리로드 생략, Play 즉시 시작 |
| ④ | 스크립트 DLL 언로드 시 살아있는 인스턴스/콜백의 힙·vtable 유실로 누수·크래시 위험 | Unity 도메인 리로드 방식의 단일 관문(ScriptDomain)으로 안전 불변식 확립 |
| ⑤ | 애셋 관리(로딩 성능·쿠킹·임포트 UX)가 느리고 불편 | 비동기 로딩 + LRU 캐시, 증분 쿠킹, 탐색기 드래그&드롭 임포트 |
| ⑥ | 최종 빌드 로딩 화면이 인위적 대기 + 가짜 "쉐이더 컴파일중..." 문구 | 실제 진행률 기반, 백그라운드 로딩으로 빠르고 정직한 로딩 |

구현 순서: **③ → ④ → ②+⑤(로딩 성능) → ⑥ → ⑤(쿠킹/드롭/UX)**.
각 단계는 독립적으로 빌드·스모크 테스트를 통과한 뒤 다음 단계로 진행한다.

---

## ③ 에디터 Play 빌드 스킵

### 현재 동작
`EditorMainMenuBar.cpp`의 Play 버튼 → `ReloadScripts_FromButton()`(Editor/Scripting/ScriptReloadHelpers.cpp)이 매번 cmake 빌드를 실행한다. (configure는 CMakeCache 존재 시 스킵하도록 이미 개선됨)

### 설계
`ReloadScripts_FromButton` 앞단에 두 단계 스킵 판정을 추가한다.

1. **빌드 스킵**: `Assets/Scripts/**`(*.cpp/*.h/*.hpp)와 `EngineSource/ScriptsBuild/CMakeLists.txt`의 최신 수정시각(mtime)이 중간 산출물 `ScriptsBuild/build/bin/<Config>/AliceScripts.dll`의 mtime보다 오래됐으면 cmake 빌드를 건너뛴다.
2. **리로드 스킵**: 빌드를 건너뛰었고, 현재 로드된 DLL(`build/bin/<Config>/dll/AliceScripts.dll`)이 중간 산출물과 크기·mtime이 같으면 언로드/리로드 전체를 건너뛰고 즉시 성공을 반환한다. → 스크립트 상태 스냅샷/복원도 발생하지 않아 Play가 즉시 시작된다.

콘솔 창(`ExecuteCommandWithConsole`)은 실제로 빌드할 때만 뜬다.

### 에러 처리
- 중간 산출물이 없으면 무조건 빌드한다.
- mtime 비교는 `std::error_code` 버전 API로 예외 없이 처리하고, 판정이 불확실하면 빌드하는 쪽(보수적)으로 폴백한다.

### 테스트
- 스크립트 무변경 상태에서 Play → 로그에 "스킵" 메시지, 콘솔 창 안 뜸, 즉시 시작.
- 스크립트 파일 하나 touch 후 Play → 빌드+리로드 수행.
- 산출물 삭제 후 Play → 전체 빌드 수행.

---

## ④ ScriptDomain — 핫리로드 안전 관문

### 확인된 위험 경로
1. `BuildGameWindow.cpp:782`: 살아있는 인스턴스 정리 없이 `ScriptHotReload_Unload()` 직접 호출.
2. `UIButtonComponent` 등의 `std::function`에 스크립트(DLL 코드)를 캡처한 람다가 남은 채 언로드되면 댕글링.
3. 언로드 진입점이 여러 곳이라 "언로드 전 전부 정리"가 구조적으로 보장되지 않음.

### 설계 (Unity 도메인 리로드 방식 + 복사본 로드)

**신규 모듈**: `Runtime/Scripting/ScriptDomain.h/.cpp`

```
ScriptDomain
├─ 인스턴스 레지스트리: IScript 생성자/소멸자에서 전역 셋에 등록/해제 (스레드 안전)
├─ Reload(world):  스냅샷(JSON) → 인스턴스 전부 파괴 → DLL-출처 콜백 클리어
│                  → Unload → 복사본 로드 → 복원
├─ Unload(world):  위와 동일하되 복원 없이 종료 (게임 빌드 직전 등)
└─ 불변식 검사:    FreeLibrary 직전 살아있는 인스턴스 수가 0이 아니면
                   언로드를 중단하고 남은 인스턴스 이름을 에러 로그로 출력
```

- **복사본 로드**: `dll/AliceScripts.dll`을 로드하지 않고 `dll/AliceScripts_live.dll`로 복사한 뒤 그 복사본을 `LoadLibrary` 한다. → 원본 파일은 절대 잠기지 않으므로 빌드/복사 충돌이 원천 차단된다. (Unreal의 버전드 DLL에서 차용하되, 이전 DLL은 정상 언로드하므로 메모리 계단 증가 없음)
- **콜백 클리어**: 리로드/언로드 시 UI 콜백(`UIButtonComponent::onClick` 등 std::function 보유처)을 일괄 초기화한다. 스크립트가 `Awake`/`OnEnable`에서 재바인딩하는 것을 규약으로 한다. 구현 단계에서 `std::function` 보유처 전수 조사(UIButtonComponent, World, EditorComponentRegistry, AdvancedAnimationComponent, Delegate)를 수행해 클리어 대상을 확정한다.
- **기존 API 정리**: `ScriptHotReload_Load/Reload/Unload`는 ScriptDomain 내부 구현으로 이동하고, 외부(에디터 포함)에는 ScriptDomain만 노출한다. `BuildGameWindow`는 `ScriptDomain::Unload(world)`로 교체.
- `ReloadScripts_FromButton`의 스냅샷/복원 로직(SnapshotAndDestroyScripts/RestoreScripts)은 ScriptDomain으로 이관하고, 헬퍼는 "빌드 판정 + ScriptDomain::Reload 호출"만 담당한다.

### 에러 처리
- 복원 중 `ScriptFactory::Create` 실패(스크립트가 새 DLL에서 사라짐): 해당 컴포넌트는 이름만 유지하고 인스턴스 없이 남긴다(현행 동작 유지). 경고 로그 1회.
- 불변식 위반(인스턴스 잔존): 언로드 중단이 원칙. 어떤 시스템이 잡고 있는지 로그로 즉시 파악 가능하게 한다.

### 테스트
- Reload 20회 반복 후 프로세스 메모리(Working Set) 증가 없음 확인 (수동, 작업관리자/스크립트).
- Play → Stop → Build Game 경로에서 크래시 없음.
- 리로드 후 스크립트 프로퍼티 값 복원 확인.
- 인스턴스를 의도적으로 잔존시키는 테스트 코드로 불변식 로그 동작 확인.

---

## ②+⑤ 애셋 로딩 성능

### 확인된 문제
- `ForwardRenderSystem::GetOrCreateTexture`(및 Deferred/UnityVfx/UIRenderer의 동등 코드): 실패를 캐시하지 않아 없는 텍스처를 매 드로우마다 재시도 + 경고 반복.
- `ResourceManager`: `weak_ptr` 캐시라 마지막 사용자가 놓으면 즉시 해제 → 재사용 시 디스크 재로드. negative cache 없음. 게임 모드 청크 Resolve가 요청마다 `filesystem::exists` 2회.

### 설계

**1) Negative cache (ResourceManager)**
- `m_missingPaths: unordered_set<string>` (mutex 보호). `LoadSharedBinaryAuto`/`LoadBinary` 실패 시 등록, 등록된 경로는 즉시 nullptr 반환(디스크 접근 없음).
- 무효화: `ClearNegativeCache()` — 에디터의 임포트/드롭/새로고침 시 호출. 게임 모드는 파일이 불변이므로 무효화 불필요.
- 실패 로그는 경로당 최초 1회만 출력한다.

**2) LRU 강참조 캐시 (ResourceManager)**
- 기존 `m_blobCache(weak_ptr)`는 유지하고, 별도의 shared_ptr LRU 링(기본 상한 256MB, EngineSettings로 조정 가능)을 추가해 최근 사용 blob의 수명을 연장한다.
- 청크 Resolve 결과(논리 경로 → 실존 청크 파일 경로)를 캐시해 exists 프로브를 1회로 줄인다.

**3) 렌더 시스템 실패 캐시**
- `GetOrCreateTexture` 계열: 실패도 `m_textureCache`에 nullptr로 기록해 재시도를 차단. 경고는 삽입 시 1회. 에디터에서 애셋이 새로 생기면 `ClearNegativeCache()`와 함께 텍스처 캐시의 nullptr 항목만 제거한다.
- 적용 대상: ForwardRenderSystem, DeferredRenderSystem, UnityVfxMeshRenderSystem, UIRenderer, SkinnedMeshSystem(emissive 경고 반복 억제).

**4) AsyncLoader (Foundation 신규)**
- 워커 스레드 N개(기본 2)의 작업 큐. API:
  - `Request(logicalPath, [](shared_ptr<const Blob>){...})` — 완료 콜백은 메인 스레드 펌프(`AsyncLoader::PumpCompleted()`, 엔진 Update 초입)에서 실행.
  - 텍스처처럼 GPU 리소스 생성이 필요한 경우: `ID3D11Device`는 스레드 세이프이므로 워커에서 `CreateTexture2D`/`CreateShaderResourceView`까지 수행하고 완성된 SRV를 콜백으로 전달한다. `ID3D11DeviceContext`는 절대 워커에서 사용하지 않는다.
- 소비처: ⑥ 로딩 화면 프리로드, 에디터 씬 로드 시 텍스처 지연 로딩(선택적 적용).

### 테스트
- 존재하지 않는 텍스처를 참조하는 씬에서 FPS가 정상 씬과 동일한지 확인, 경고 로그 각 1회.
- 같은 애셋 반복 로드/해제 시나리오에서 디스크 접근이 1회인지(로그/프로파일) 확인.
- AsyncLoader 단위: 100개 파일 동시 요청 → 전부 콜백 도착, 메인 스레드 외 콜백 실행 없음.

---

## ⑥ 최종 빌드 로딩 화면

### 확인된 문제 (`EngineInitialize.cpp` InitializePreloadAndLoadingScreen)
- 모든 초기화·프리로드가 메인 스레드 동기 실행이며, `AdvanceTo(target, minSeconds)`가 단계마다 **인위적 최소 대기**(시스템당 0.12~0.2초, 애셋당 0.015~0.08초 + 게이지 따라잡기 대기)를 강제한다.
- 상태 문구가 작업 내용과 무관하게 "쉐이더 컴파일중..." 고정.

### 설계
1. **인위적 대기 제거**: `AdvanceTo`의 `minSeconds`와 스텝 슬립을 삭제. 게이지 스무딩(보간)은 유지하되 작업 진행을 막지 않는다. 완료 시 게이지가 잔여 구간을 빠르게 채우고 끝난다.
2. **백그라운드 프리로드**: preloadList 처리를 AsyncLoader(② ⑤)로 위임. 메인 스레드는 UI 렌더 루프만 돌린다.
   - 진행 공유: `atomic<size_t> loadedCount` + 현재 항목명(mutex 보호 string).
   - 오디오/렌더 시스템 초기화는 D3D 컨텍스트·전역 상태 의존이 있으므로 메인 스레드에 남기되, 사이사이 UI 프레임을 계속 렌더한다(현행 구조 유지).
3. **정직한 상태 문구**: "오디오 초기화", "렌더 시스템 초기화", "셰이더 준비", "애셋 로딩 (N/M) 파일명", "완료 — 클릭하여 시작". 애니메이션 점(...)은 유지.
4. **유지되는 것**: 청크 무결성 검증 실패 화면(빨간 게이지), 완료 후 클릭 게이트, 배너/힌트 레이아웃.

### 테스트
- 게임 빌드(AlicePlayer) 산출 후 로딩 총 시간을 개선 전/후 측정해 기록.
- 로딩 중 게이지가 60fps로 부드럽게 갱신되는지(대형 애셋 로드 중 UI 멈춤 없음) 확인.
- 무결성 실패 시나리오(청크 삭제) 화면 동작 확인.

---

## ⑤ 쿠킹 증분화 · 드래그&드롭 임포트 · 에디터 UX

### 1) 증분 쿠킹
- 쿠킹 시 `Cooked/CookManifest.json`(소스 상대경로 → {size, mtime, contentHash})을 기록.
- `CookDirectoryRecursive`/`CookResourceToChunkStore`에서 매니페스트와 비교해 **미변경 파일은 재암호화/재청크를 건너뛴다**. 산출물이 이미 존재하는지도 함께 확인.
- 소스에서 삭제된 파일의 청크는 매니페스트 비교로 제거(고아 청크 정리).
- 매니페스트 없음/파손 시 전체 쿠킹으로 폴백.

### 2) 탐색기 드래그&드롭 임포트
- `EngineWindow`(Win32)에서 에디터 모드일 때 `DragAcceptFiles(hwnd, TRUE)` + `WM_DROPFILES` 처리.
- 드롭 이벤트 → 에디터 임포트 큐에 파일 목록 전달(메인 루프에서 처리).
- 확장자 규칙 테이블:

| 확장자 | 대상 폴더 | 후처리 |
|--------|-----------|--------|
| .fbx | `Resource/fbx/<파일명>/` | 기존 Load FBX 임포트 파이프라인 실행(.fbxasset 생성) |
| .png .jpg .dds .tga | `Resource/Textures/<파일명>/` | 없음 (negative cache 무효화) |
| .wav .mp3 .ogg | `Resource/Sound/` | 없음 |
| .ttf .otf | `Resource/Fonts/` | 없음 |
| 그 외 | 드롭 무시 + 상태바 경고 | — |

- 이름 충돌 시 `이름_1` 접미사. 복사 완료 후 프로젝트 패널 자동 새로고침 + `ClearNegativeCache()`.
- 다중 파일/폴더 드롭 지원(폴더는 재귀 복사, 규칙은 파일별 적용).

### 3) 에디터 UX (targeted)
- 프로젝트 패널: 이름 부분일치 검색 필터, 새로고침 버튼(F5).
- 전면 개편은 범위 외.

### 테스트
- 동일 소스로 게임 빌드 2회 → 2회차 쿠킹 시간이 대폭 감소(스킵 로그 확인), 산출물 해시 동일.
- 탐색기에서 png/fbx 드롭 → 올바른 폴더 복사 + 패널 표시 + 씬에서 사용 가능.
- 미지원 확장자 드롭 → 경고만, 크래시 없음.

---

## 공통 검증 절차 (각 단계 완료 시)
1. `cmake --build build --config Release --target Launch` 성공.
2. 에디터 25초 스모크 실행 → 로그 `[Error]` 0건.
3. 해당 단계의 기능 테스트(위 각 절) 수행.
4. 단계별 커밋(기능 단위).

## 범위 외 (명시)
- D3D11-AliceTutorial의 텍스처 로드/애니메이션 리타기팅 신규 기능(미완성, 추후 통합).
- 프로젝트 패널 전면 개편, 애셋 핸들 기반 AssetManager 전면 재설계(접근안 B — 기각).
- 암호화 방식(XOR) 변경.
