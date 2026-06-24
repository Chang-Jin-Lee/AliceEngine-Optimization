# Unity VFX → AliceRenderer 파이프라인 (v1 + v2)

## 목표
- Unity ParticleSystem 기반 프리팹을 `effect.json + resources/ + report.json`으로 추출해,
  AliceRenderer에서 `UnityVfxComponent`로 재생할 수 있게 한다.
- v1은 **ComputeEffect 기반 매핑**으로 동작하며, 일부 모듈은 비지원(보고서에 기록).
- v2는 **UnityVfxMeshRenderSystem 기반 CPU 시뮬 + 전용 렌더**로,
  텍스처/머티리얼/트레일 등 “유니티 룩”에 필요한 모듈을 직접 반영한다.

---

## 1) Unity Exporter 사용법

Unity 프로젝트의 메뉴:
- `Tools/VFX Export/Export All VFX (JSON)`
- `Tools/VFX Export/Export Selected Prefab (JSON)`

Exporter에서 지정할 것:
- Output Root (엔진 리포의 `Assets/VFX/...` 권장)
- Scan Folders (기본 `Assets`)
- Exclude Keywords (예: Demo, Sample, _DemoScene)
- 옵션: copyTextures / exportMeshesAsJson / exportMaterialsAsJson / includeVFXGraph

출력 구조:
```
<outRoot>/<EffectName>/
  effect.json
  report.json
  resources/
    textures/
    materials/
    meshes/
```

`report.json`에 미지원 모듈/오류가 기록된다.

---

## 2) 엔진 사용법 (UnityVfxComponent)

### 컴포넌트 필드
- `effectPath` : `Assets/.../effect.json` 논리 경로
- `sizeScale` : Unity startSize → 엔진 sizePx 스케일
- `speedScale` : Unity startSpeed 스케일
- `intensityScale` : 컬러 밝기 스케일
- `spawnRateScale` : Emission rate 스케일

### 예시
`Assets/Scenes/VFX/UnityVfxDemo.scene` 참고.

---

## 3) v1 매핑 규칙 (ComputeEffect 기반)

Unity → Engine 매핑:
- Main.startLifetime → `lifeMin/lifeMax`
- Main.startSpeed → `startSpeed`
- Main.startSize → `sizePx` (sizeScale 곱)
- Main.startColor → `color`
- Main.gravityModifier → `gravity` (0, -9.81 * modifier, 0)
- Emission.rateOverTime → `spawnRate` (rate / 50, 0~1 클램프)
- Shape.radius/box → `radius`

지원 모듈 외 항목은 무시되며 `report.json`에 기록.

---

## 4) 제한사항 (v1)

- 텍스처/머티리얼 정보는 **export**되지만 **현재 ComputeEffect에는 적용되지 않음**.
- ParticleSystem의 복잡한 모듈(Noise/Collision/Trail/SubEmitters 등) 미지원.
- 정확히 동일한 룩은 v2에서 전용 파티클 렌더러가 필요.

---

## 5) v2 런타임 구조 (UnityVfxMeshRenderSystem)

### 5.1 핵심 이유 (v1이 큐브/점처럼 보이는 원인)
- v1은 ComputeEffect로 **스폰/시뮬만 매핑**하고,
  실제 렌더는 DebugDraw 성격이라 텍스처/머티리얼/트레일이 적용되지 않는다.
- 따라서 유니티의 “검기/아크” 룩은 v1만으로는 재현 불가.

### 5.2 시스템 개요
`UnityVfxMeshRenderSystem`이 effect.json을 파싱해 CPU 시뮬을 돌리고,
**Mesh/Billboard/Trail**을 직접 렌더링한다.

- **EffectCache**: effect.json 파싱 결과 (EmitterDef 목록)
- **RuntimeCache**: 엔티티별 시뮬 상태 (EmitterRuntime/Particle)
- **Render**: 매 프레임 dt로 시뮬 → 머티리얼/텍스처 바인딩 → 메쉬/빌보드/트레일 렌더

### 5.3 노드/변환 계층
- effect.json `nodes[].path`를 기준으로 트리 구성.
- `path`의 부모/자식 관계로 로컬 → 월드 행렬을 합성해
  **EmitterDef.localPos/Rot/Scale**에 적용.
- 유니티처럼 “자식 여러 개 조합 → 프리팹” 구조를 재현 가능.

### 5.4 CPU 시뮬 모듈 매핑 (지원 범위)
- **Main**
  - startLifetime, startSpeed, startSize, startColor
  - startRotation / startRotation3D
  - simulationSpace (Local/World)
- **Emission**
  - rateOverTime
  - bursts (min/max, cycleCount, repeatInterval, probability)
- **Shape**
  - Cone / Box / Sphere
  - position / rotation / alignToDirection
- **ColorOverLifetime**
- **SizeOverLifetime**
- **RotationOverLifetime** (separateAxes 지원)
- **VelocityOverLifetime** (Local/World 변환 보정)
- **TextureSheetAnimation** (tiles/frameOverTime/startFrame/cycleCount)
- **Renderer**
  - Billboard / Horizontal / Stretch / Mesh
  - material / mesh
- **Trails**
  - lifetime / minVertexDistance
  - widthOverTrail / colorOverTrail

### 5.5 렌더링 로직 (요약)
1. 파티클 스폰:
   - Shape에서 초기 위치/방향 샘플링
   - startSpeed → baseVel
   - startRotation/3D 적용
2. 파티클 업데이트:
   - velocityOverLifetime 추가
   - size/color/rotation over lifetime 적용
3. 빌보드 렌더:
   - 카메라 right/up 기반 쿼드 구성
   - stretch 모드 시 velocity 방향으로 확장
4. 메쉬 렌더:
   - 파티클별 월드 행렬 생성 후 메쉬 드로우
5. 트레일 렌더:
   - 파티클 위치를 시간 축으로 샘플링
   - triangle strip 생성
   - viewDir와 movement 방향으로 폭 계산

### 5.6 머티리얼/텍스처 매핑 (v2)
- `TexEnv` → 메인 텍스처
  - `noise/mask/dissolve` → Noise 텍스처
  - `ramp/gradient` → Ramp 텍스처
- `Float/Range` → dissolve/noiseStrength/rampStrength
- `Vector` → UV 스크롤
- BlendMode는 `_BlendSrc/_BlendDst` 또는 shaderName 키워드로 추정

### 5.7 안전 장치
- `kMaxParticlesPerEmitter`, `kMaxParticlesPerEffect`로 폭주 방지
- `unity_builtin_extra` 텍스처는 로드 무시 (존재하지 않는 경우 대비)
- ResourceManager 로딩 실패 시 기본 텍스처로 fallback

---

## 6) v2 제한사항 (현재 상태)
- GPU 시뮬이 아니라 CPU 시뮬 → 이펙트 수가 많으면 부하 가능
- 정렬(소팅) 미지원 → 투명 렌더 결과가 유니티와 차이날 수 있음
- Soft Particle/Depth Fade/노이즈 워프 등 일부 셰이더 기능 미구현
- Unity MaterialPropertyBlock 대응은 현재 없음

---

## 7) 데모 씬
- `Assets/Scenes/VFX/UnityExportVfxDemo_01~05.scene`
- `Assets/Scenes/VFX/UnitySlashVfxDemo.scene`
- `Assets/Scenes/VFX/UnityVfxDemo.scene`

---

## 8) 빌드/리소스

- Export 결과를 **엔진 리포의 `Assets/` 하위**에 두어야
  ResourceManager가 chunk/암호화 파이프라인에 포함시킨다.
- 최종 빌드에서도 `effect.json`은 ResourceManager를 통해 로드된다.

---

## 9) AI에게 질문할 때 제공하면 좋은 정보
- 문제되는 이펙트의 `effect.json`에서:
  - Renderer 설정 (renderMode/material/mesh)
  - Trails 모듈 유무
  - Texture/Material 경로
- Unity/Engine 비교 스크린샷
- 사용 중인 scene 이름 및 엔티티 이름
