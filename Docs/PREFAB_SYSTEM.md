# Prefab System (최신)

이 문서는 EGOSIS의 Prefab 저장/로드 방식과 런타임 사용법을 간단히 정리한 문서입니다.

## 핵심 요약
- 프리팹은 JSON 기반 파일입니다. (`.prefab`)
- 단일 엔티티 **+ 자식 계층 전체** 저장/복원 지원 (v2 포맷).
- 에디터/게임 모드 모두에서 **동일한 API**로 인스턴스 가능.
- 최종 빌드에서는 `Assets/`에 있는 프리팹이 **Metas/Chunks**로 패킹되므로,
  `Prefab::InstantiateFromFileAuto()`가 그대로 동작합니다.

---

## 프리팹 포맷 버전

### v1 (단일 엔티티)
```json
{
  "version": 1,
  "name": "MyPrefab",
  "Transform": { ... },
  "UnityVfx": { ... }
}
```

### v2 (엔티티 계층 포함)
```json
{
  "version": 2,
  "rootGuid": "1234567890",
  "entities": [
    { "guid": "1234567890", "_parentGuid": "0", ... },
    { "guid": "1234567891", "_parentGuid": "1234567890", ... }
  ]
}
```

`rootGuid`는 루트 엔티티를 가리키며 `_parentGuid`로 부모-자식 관계를 복원합니다.

---

## 런타임 API

### 1) 파일 경로 직접 로드
```cpp
EntityId e = Prefab::InstantiateFromFile(world, "D:/Project/Assets/Prefabs/My.prefab");
```

### 2) 자동 로드 (에디터/게임 공용)
```cpp
EntityId e = Prefab::InstantiateFromFileAuto("Assets/Prefabs/My.prefab");
```

**장점**
- 에디터 모드: 원본 파일에서 읽음
- 게임 모드: Cooked/Chunks(암호화)에서 읽음  

**조건**
- 프리팹은 `Assets/` 또는 `Resource/` 하위에 있어야 함
  - `Build Game` 시 `Assets -> Metas/Chunks`, `Resource -> Cooked/Chunks`로 패킹됨

---

## 기본 월드 연결
`Prefab::InstantiateFromFileAuto()`는 엔진의 기본 월드를 사용합니다.  
엔진 초기화 시 아래가 이미 설정되어 있습니다.

```cpp
Prefab::SetDefaultWorld(&m_world);
```

스크립트에서 월드 포인터 없이도 사용할 수 있습니다.

---

## 스킨 메시 경로 보정 (FBX 충돌 대응)

프리팹에 `SkinnedMeshComponent`가 포함된 경우, 로딩 시 아래 순서로 경로를 보정합니다.

1. `instanceAssetPath`가 있으면 해당 `.fbxasset`을 읽어 `meshAssetPath`를 최신 키로 갱신
2. `instanceAssetPath`가 없으면 `<meshAssetPath>.fbxasset`을 추정하여 동일하게 보정

이 보정은 **FBX 이름 충돌 방지(해시 키 도입)** 이후에도
기존 프리팹이 깨지지 않도록 하기 위한 안전 장치입니다.

---

## 스크립트 예시: LoadVFXFromPrefab

### 개요
`Assets/Scripts/VFX/LoadVFXFromPrefab`

- **1~9번 키**로 프리팹을 생성
- `m_parentTargetName`에 지정된 엔티티를 **부모**로 붙여서 (0,0,0)에서 스폰
- 메모리 풀을 사용해 **재활용** (미리 생성 후 on/off)

### 주요 프로퍼티
- `m_prefabPath1~9` : 프리팹 경로 (Drag & Drop 가능)
- `m_parentTargetName` : 부모 엔티티 이름 (Hierarchy에서 Drag & Drop 가능)
- `m_loop` : 반복 스폰 모드
- `m_loopInterval` : 반복 간격
- `m_lifeTime` : 수명 (0 이하이면 토글 방식)
- `m_poolSize` : 풀 크기
- `m_prewarm` : 시작 시 풀 미리 생성

---

## 샘플 씬
- `Assets/Scenes/VFX/LoadVFXFromPrefab.scene`
- `Parent` 엔티티 아래에 프리팹이 생성되도록 구성
- 기본 프리팹: `Assets/Prefabs/Combo_slash_fx_01.prefab`

---

## 최종 빌드 검증 포인트

1. **프리팹 위치**
   - `Assets/Prefabs/...` 또는 `Resource/...` 아래에 배치해야 함.
2. **Build Game 패킹**
   - `Assets`는 `Metas/Chunks`로 패킹됨 → `LoadText`로 읽힘.
3. **게임 모드 로딩 경로**
   - `InstantiateFromFileAuto`는 `ResourceManager::LoadText` 사용.
   - gameMode일 때 `Assets/`는 Metas/Chunks에서 로드됨.

이 조건만 지키면 **에디터/최종 빌드 모두 동일하게 동작**합니다.
