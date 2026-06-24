# EGOSIS 엔진 최적화 보고서

작성일: 2026-06-18 · 대상: `Engine/src` 전체 · 목적: 코드 단순화 + 성능 + 빌드 안정성

> 원칙: 엔진 내부 코드는 **짧고 단순하게**, 변경 후에도 **빌드 가능**해야 하며 **개발 오류가 없어야** 함.
> 각 항목은 `[영향][난이도][위험]`으로 분류. 위험 "중간" 이상은 적용 전 검증(회귀 테스트) 필요.

---

## 0. 한눈에 보기

| 분류 | 항목 수 | 핵심 효과 |
|---|---|---|
| Tier 0 — 버그/정확성 | 3 | 즉시 수정, 동작 정상화 |
| Tier 1 — 무위험 삭제(단순화) | 9 | 코드량 감소, 빌드 영향 0 |
| Tier 2 — 핫패스 성능 | 9 | 프레임당 할당·재계산 제거 |
| Tier 3 — 구조/메모리/직렬화 | 7 | 장기 안정성·정확성 |
| Tier 4 — 빌드 타임 | 5 | 증분 빌드 단축 |

**가장 먼저 권장**: Tier 0 전부(1줄짜리 버그) → Tier 1 삭제(안전) → Tier 2의 `UpdateBonesCB`·애니메이션 early-out.

---

## Tier 0 — 버그 / 정확성 (즉시, 저위험)

| ID | 영향/난이도/위험 | 위치 | 문제 → 수정 |
|---|---|---|---|
| **B01** | High / 낮음 / 낮음 | `World.cpp:CreateCamera` | `hasCamera` 의미가 반전되어 **첫 카메라가 primary=false**가 됨. → `bool hasCamera = !GetComponents<CameraComponent>().empty();` |
| **B02** | High / 낮음 / 낮음 | `AudioSystem.cpp:Update` (105) | `for (auto [id, src] : ...)`가 컴포넌트를 **값 복사** → `requestPlay/Stop` 리셋이 원본에 반영 안 됨(재생 반복). 매 프레임 string 2개 복사. → `auto&& [id, src]` (SoundBox 루프 188도 동일) |
| **B03** | Med / 낮음 / 낮음 | `EngineSettings.json` | `skybox.customDir`에 머신 절대경로 하드코딩(타 PC에서 깨짐). → `Resource/...` 상대(논리) 경로로 변경 |

---

## Tier 1 — 무위험 삭제로 단순화 (빌드 영향 0, 참조 0건 확인됨)

엔진을 짧게 만드는 가장 안전한 작업. 모두 인스턴스화·include 참조가 없는 죽은/중복 코드.

| ID | 영향/위험 | 대상 | 비고 |
|---|---|---|---|
| **D01** | High / 낮음 | `Rendering/SwordSlashRenderSystem.cpp/.h` 전체 삭제 | 어디서도 생성/Render 안 됨. `TrailEffectRenderSystem`과 기능 중복. 매 프레임 `dynamic_cast` 전수 RTTI까지 있음 |
| **D02** | High / 낮음 | `Rendering/EffectRenderSystem.cpp`(소문자 `trailEffectRenderSystem`) 삭제 | `TrailEffectRenderSystem`과 1:1 중복, 인스턴스화 0건 |
| **D03** | Med / 낮음 | `Editor/4.txt` 삭제 | `tree` 출력 깨진 쓰레기 파일 |
| **D04** | Med / 낮음 | `Editor/Inspector/*.cpp.extracted` 7개 삭제 (≈3,794줄) | 리팩토링 중간 산출물, CMake 참조 0건 |
| **D05** | Med / 낮음 | `ComponentStorage.h`의 `m_generations`, `ComponentHandle`, `ShrinkSparse()` 삭제 | 어디서도 읽지 않음(엔티티 generation은 `World`가 관리) |
| **D06** | Low / 낮음 | `SoundManager`의 Boss 그룹 API 3종 + 전역 맵 3개 삭제 | 호출처 0건(게임 전용 기능이 엔진에 유입) |
| **D07** | Low / 낮음 | `AdvancedAnimSystem.h`의 `USE_REF_PALETTE_FORCE`(항상 false) 분기 삭제 | 죽은 분기 |
| **D08** | Low / 낮음 | `FbxModel.cpp`/`FbxImporter.cpp` 죽은 주석·미사용 include 정리 | BFS 주석, 디버그 로그 27줄, `<Windows.h>`/`<ranges>`/`<queue>` 등 |
| **D09** | Low / 낮음 | `EngineUpdate.cpp`의 주석 처리된 AttackDriver 코드, `ComputeEffectSystem`의 deprecated 빈 함수 3종 정리 | |

> `ScriptPCH.h`는 이름과 달리 PCH로 연결돼 있지 않고 참조도 없음. **삭제하거나** Tier 4의 PCH 도입과 함께 실제 PCH로 연결(둘 중 택1).

---

## Tier 2 — 핫패스 성능 (프레임당 비용 제거)

| ID | 영향/난이도/위험 | 위치 | 문제 → 수정 |
|---|---|---|---|
| **P01** | High / 낮음 / 중간 | `Deferred/ForwardRenderSystem::UpdateBonesCB` | 드로우마다 항상 `MaxBones=1023`개(≈64KB) 전체를 transpose/Identity로 채워 Map. → `boneCount`까지만 채우기(셰이더가 범위 밖을 안 읽는지 1회 확인) |
| **P02** | High / 중간 / 중간 | `DeferredRenderSystem::BuildWorldMatrix`, `World.cpp:ComputeWorldMatrix_Internal` | 오브젝트마다 `std::vector<XMMATRIX>` 힙 할당. → 부모체인은 얕으므로 `std::array<,16>` 또는 멤버 스크래치, 또는 자식→부모 누적곱 1줄 |
| **P03** | High / 중간 / 중간 | `AdvancedAnimSystem`/`SkinnedAnimationSystem::Update` | `playing=false`여도 매 프레임 포즈·팔레트 전부 재계산. → 입력(클립/시간/blend) 미변경 시 early-out |
| **P04** | High / 낮음 / 낮음 | `AdvancedAnimSystem.h:375` | 매 프레임 `animComp->palette = rt.palette`(본 수×64B) 전체 복사. → `data()` 직접 연결 또는 `swap`(읽기 의존성 1회 확인) |
| **P05** | High / 중간 / 중간 | `UnityVfxMeshRenderSystem::Render` | 파티클당 vector 신규 할당 + per-particle Map+Draw(드로우콜 폭증). → 스크래치 재사용 + 인스턴싱/단일 버퍼 누적 |
| **P06** | Med / 낮음 / 낮음 | `EngineRender.cpp:RenderMainPass`/`RenderCameraPreview` | 매 프레임 `unordered_set<EntityId> cameraIDs` 재생성(에디터는 프레임당 2회). → 멤버 스크래치 `.clear()` 재사용 |
| **P07** | Med / 중간 / 중간 | `Camera.cpp:GetView/Projection/ViewProjectionMatrix` | 캐싱 없이 호출마다 행렬 재계산(프레임당 수십 회). → dirty 플래그 캐싱 |
| **P08** | High / 중간 / 중간 | `World.cpp:MarkTransformDirty` / `DestroyEntity` | dirty 표시마다 **전체 Transform 선형 스캔**으로 자식 탐색(깊은 트리 O(N²)). → 기존 `m_children` 캐시 활용 |
| **P09** | High / 중간 / 중간 | `Editor/Project/DirectoryTree.cpp:DrawDirectoryNode` | 열린 폴더마다 **매 프레임 `fs::directory_iterator` 디스크 스캔**. → 트리 캐시 + 수동/주기 Refresh |

> 추가 동급 항목(스크래치 벡터 재사용으로 동일 패턴): `ComputeEffectSystem::Execute`, `TrailEffectRenderSystem::Render`, `WeaponTraceSystem`의 prev/curr 버퍼(`swap`화), `DeferredRenderSystem`의 인스턴싱 배치 벡터·매 프레임 `std::sort`.

---

## Tier 3 — 구조 / 메모리 / 직렬화 (중위험, 검증 후 적용)

| ID | 영향/난이도/위험 | 위치 | 문제 → 수정 |
|---|---|---|---|
| **S01** | Med / 중간 / 중간 | `AdvancedAnimSystem`·`SkinnedAnimationSystem`·`AudioSystem`의 `m_runtime` 맵 | 엔티티 파괴 시 erase 없음 → **씬 재로드마다 무한 증가**(메모리 누수). → 살아있는 엔티티와 대조해 정리 또는 파괴 훅 |
| **S02** | Med / 중간 / 중간 | `PhysicsSystem.cpp:1238` Game→Physics 동기화 | 물리 없는 엔티티 포함 전체 Transform 순회 + 엔티티당 3회 GetComponent. → `m_entityToActor` 맵을 1차로 순회 |
| **S03** | Med / 중간 / 낮음 | `SocketWorldUpdateSystem`·`SocketAttachmentSystem` | 매 프레임 set/vector 신규 할당 + 이름 선형 매칭 + 자식 BFS(O(n²)). → 이름→인덱스 맵, 버퍼 재사용, 소켓 child 캐시 |
| **S04** | Med / 낮음 / 중간 | `ScriptSystem.cpp` Call*Update 4종 | 호출마다 전체 엔티티 ID 스냅샷 `vector` 할당(FixedUpdate는 더 잦음). → 멤버 버퍼 재사용 |
| **S05** | High / 중간 / 중간 | `JsonRttr.h:248` 정수/실수 변환 | 모든 정수를 `to_int64`/`get<double>` 경유 → 큰 `uint64`·2^53 초과 정수 손실. → 부호/정수 타입 분기 직접 get |
| **S06** | High / 낮음 / 낮~중 | `JsonRttr.h:253,344`, `*Serialization.h` | NaN/Inf float이 `null`로 저장돼 필드 누락; 미처리 타입에 JSON 문자열 주입; 음수에 `get<uint64_t>()`. → `isfinite` 가드, 폴백 시 `false` 반환, 음수 클램프 |
| **S07** | Med / 중간 / 중간 | `ResourceManager.h:163` blob 캐시 `weak_ptr` | 호출자가 놓으면 즉시 캐시 미스 → 재읽기/재복호화. `m_pathToHash` 무한 증가. → 핫 blob 강참조 LRU, 종료 시 정리 |

---

## Tier 4 — 빌드 타임 단축

| ID | 영향/난이도/위험 | 위치 | 문제 → 수정 |
|---|---|---|---|
| **C01** | High / 중간 / 낮음 | `CMakeLists.txt` (Engine 타깃) | PCH 미사용 → `<Windows.h>`/RTTR/json/DirectXMath를 매 TU 재파싱. → `target_precompile_headers(Engine PRIVATE ...)` 추가 |
| **C02** | High / 중간 / 중간 | `Resources/SceneFile.cpp` (1,916줄, include 78개) | 컴포넌트 하나 추가해도 거대 TU 전체 재컴파일. → `WriteEntity`/`ReadEntity` 분리 |
| **C03** | Med / 낮음 / 낮음 | `Serialization/JsonRttr.h`·`ReflectionSerializer.h` | 헤더에서 풀 `json.hpp`+rttr 포함이 광범위 전파. → `json_fwd.hpp`+전방선언, 미사용 include 제거 |
| **C04** | Med~High / 중간 / 중간 | `FbxModel.cpp` | 스키닝 웨이트 빌드 ~130줄이 **3곳 복붙**(분기까지 발생). → private 헬퍼 1개로 추출 |
| **C05** | Med / 낮음 / 낮음 | 직렬화 헬퍼 중복 | `ParseGuidOrZero` 3중, `Float3<->Json` 2중, `SceneFile::Save`/`SaveToJsonString` 수집 로직 중복. → 공용 헬퍼 통합 |

---

## 권장 적용 순서

1. **Tier 0** (3건, 1줄 수준) — 즉시. 명백한 버그 정상화.
2. **Tier 1** (D01~D09) — 안전 삭제로 코드량 즉시 감소(빌드 영향 0).
3. **Tier 2 우선 P01·P03·P04** — 캐릭터 다수 씬에서 CPU 절감 최대.
4. **Tier 2 나머지 + Tier 3 S01** — 프레임 할당 제거, 누수 차단.
5. **Tier 4 C01·C03** — 빌드 단축(저위험부터).
6. **Tier 3 S05·S06 / Tier 4 C02·C04** — 직렬화 정확성·중복 제거(회귀 테스트 동반).

## 검증 시 주의 (위험 "중간" 항목)
- **P01**: 셰이더가 `boneCount` 범위만 읽는지 확인 후 적용.
- **P04 / S01 / C04**: 팔레트·Runtime·스키닝 로직을 다른 시스템(소켓·직렬화)이 읽는지 확인.
- **S05/S06/E**: 변경 후 기존 씬·프리팹 로드 회귀 테스트 필수.
- 삭제(Tier 1)는 참조 0건이 확인되었으나, 실제 적용 시 한 번 더 전체 빌드로 확인 권장.
