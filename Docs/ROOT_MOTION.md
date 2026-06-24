# Root Motion (Advanced Animation)

## 개요
현재 엔진은 **Extract → Consume → (선택) Apply** 방식으로 Root Motion을 처리한다.

- **Extract**: 루트 본(root bone)의 프레임 델타를 추출
- **Consume**: 추출한 루트 이동/회전을 포즈에서 제거(되감김/스냅 방지)
- **Apply(옵션)**: 추출한 델타를 엔티티 Transform에 누적 적용

이 구조는 언리얼과 동일한 철학으로, **항상 적용** 대신 **모드로 제어**한다.

---

## 모드/옵션 (간소화)

- 루트 모션은 **on/off 토글**만 제공
- 내부 기본값:
  - RootLock: `AnimFirstFrame`
  - 축: XZ 이동 + Yaw 회전만 추출

---

## 시스템 동작 흐름 (구현 위치)

### 1) AdvancedAnimSystem
파일: `Engine/src/Runtime/Engine/AdvancedAnimSystem.cpp`

- Root Motion 소스는 **base.clipA**를 기준으로 삼는다.
- 다음 조건에서 reset:
  - base.clipA 변경
  - loop 랩(시간이 루프 경계 넘어감)
  - `rootMotion.resetNextFrame`가 true인 경우
- `UpdateDesc.rootMotion`을 채워 `AdvancedAnimator::Update()`에 전달
- `FromEverything` 모드일 때만 **TransformComponent에 델타 적용**

### 2) AdvancedAnimator
파일: `Engine/src/Runtime/Gameplay/Animation/AdvancedAnimator.h`

- `ComputeGlobalsFromLocals()` 직후에 Root Motion 추출
- 루트 본 글로벌에서 델타 계산 후:
  - `m_LastRootDelta`에 저장
  - **m_GlobalMatrices 전체에 consume 적용** (팔레트/소켓 모두 일치)

---

## 사용법 (컴포넌트)

파일: `Engine/src/Runtime/Gameplay/Animation/AdvancedAnimationComponent.h`

```cpp
// 캐릭터 기본값 (Inspector)
anim.rootBoneName = "root";
anim.rootMotionUnlock = false; // true면 RootMotion 적용(오브젝트 이동)
```

---

## 주의 사항 / 운영 규칙

- 기본은 `NoExtraction`.
- 제자리 로코모션은 `Ignore`.
- 애니 기반 이동(대시/전진 공격 등)은 `FromEverything`.
- 점프/낙하에서만 `extractTranslationY = true` 권장.
- 상체 전용 애니에는 root 키를 제거하거나, 추후 **애셋별 EnableRootMotion** 플래그로 확장 가능.

---

## 테스트 체크리스트

1. **모드별 확인**
   - `Ignore`: 클립 끝에서 원점으로 되감김 없음
   - `FromEverything`: 캐릭터 Transform이 누적 이동
2. **루프 애니**
   - 루프 경계에서 텔레포트/폭주 없음
3. **클립 전환**
   - 첫 프레임 팝 없음 (AnimFirstFrame 확인)
4. **소켓 안정성**
   - 무기/손 소켓이 메시와 항상 일치
