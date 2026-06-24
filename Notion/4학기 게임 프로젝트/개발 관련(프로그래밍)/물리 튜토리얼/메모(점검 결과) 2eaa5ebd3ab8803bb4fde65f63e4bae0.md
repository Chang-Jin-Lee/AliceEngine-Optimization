# 메모(점검 결과)

**점검 결과 (심각도 순)**

1. **[Critical] 보스 경직/인터럽트가 사실상 하드코딩으로 비활성화됨**
    
    m_bossCanBeHitstunned가 선언되어도 실제 로직에서 무시됩니다. 보스는 매 프레임 canBeHitstunned=false로 강제되고, Resolve 스냅샷에서도 forceNoInterrupt=true, 심지어 ForceCancelAttack도 보스 대상은 제거합니다.
    
    참조: [C_CombatSessionComponent.h (line 48)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 674)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 3757)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 3901)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 4208)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [#06Boss.scene (line 3197)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
2. **[Critical] 스태미나 시스템이 사실상 동작하지 않음 (소모 경로 없음)**
    
    FSM은 공격/구르기에 스태미나 조건을 보는데, ConsumeStamina 명령을 발행하는 코드가 없습니다(적용부만 존재). 결과적으로 초기값에서 거의 무한 운용됩니다.
    
    참조: [C_ActionFsm.cpp (line 171)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_ActionFsm.cpp (line 317)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatApply.cpp (line 159)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
3. **[High] 히트스탑이 데미지/이벤트 지연과 결합되어 반응 지연 발생**
    
    현재 히트 시 ApplyDamage와 OnHit/OnGuardBreak 이벤트를 m_hitstopSec만큼 지연 큐에 넣습니다. 기본값도 2.0초라 설정 실수 시 전투 반응이 크게 늦어집니다.
    
    참조: [C_CombatSessionComponent.h (line 56)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 4123)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 4252)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 718)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
4. **[High] 한 프레임 다중 히트에서 스냅샷 갱신이 없어 판정 일관성 깨짐**
    
    한 번 만든 스냅샷(playerSnapshot/bossSnapshot)을 같은 프레임 모든 hit에 재사용합니다. 첫 hit에서 내구도/무적이 바뀌어도 다음 hit 판정은 이전 상태로 계산됩니다.
    
    참조: [C_CombatSessionComponent.cpp (line 3888)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 3990)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 4220)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
5. **[High] 패링 타이밍 설정값이 죽어있고 실제 윈도우가 애니 길이에 종속**
    
    입력 컴포넌트의 m_parryWindowSec/intent.parryTapWindowSec는 사용되지 않습니다. 실제 패링 가능 구간은 guardEnterActive(가드 진입 애니 시간)에 사실상 고정됩니다.
    
    참조: [C_PlayerInputSourceComponent.h (line 47)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_PlayerInputSourceComponent.cpp (line 250)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 3114)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 3792)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
6. **[Medium] 푸시백 방향 0일 때 ‘밀리지는 않는데 이동만 봉인’되는 케이스**
    
    공격자/피격자가 거의 같은 위치면 pushback 방향이 0인데도 pushback 상태는 걸립니다. 이후 이동 명령은 pushback override로 차단되어 제자리 고정처럼 보일 수 있습니다.
    
    참조: [C_CombatApply.cpp (line 336)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 2407)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 2664)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
7. **[Medium] 가드/패링 전방 판정이 180도로 과도하게 넓음**
    
    targetInFront가 dot >= 0 기준이라 반구 전체가 전방입니다. 소울류 기준으로는 가드/특히 패링 판정이 너무 넓게 잡힙니다.
    
    참조: [C_Fighter.cpp (line 86)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 1415)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatResolver.cpp (line 42)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatResolver.cpp (line 56)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
8. **[Medium] 보스 AI 피드백 훅이 사실상 미연결**
    
    NotifyDamageTaken/NotifyPlayerParry/NotifyPlayerGuardOrEvade는 정의돼 있으나 세션에서 호출되지 않습니다(현재는 NotifyAttackOutcome만 호출).
    
    참조: [C_BossBrainComponent.h (line 118)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#), [C_CombatSessionComponent.cpp (line 4063)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
9. **[Low] 2인 전투 가정이 코드에 박혀 있어 확장 시 오동작 위험**
    
    hit 처리에서 attacker/victim 스냅샷을 player/boss 2분기로만 선택합니다. 제3 주체(소환체/함정/다수전) 들어오면 잘못된 스냅샷으로 resolve됩니다.
    
    참조: [C_CombatSessionComponent.cpp (line 3992)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    
10. **[Low] EnterHitstun 커맨드는 구현 미완성**
    
    커맨드 타입은 있으나 실제 적용은 TODO 상태입니다.
    
    참조: [C_CombatApply.cpp (line 240)](https://file+.vscode-resource.vscode-cdn.net/c%3A/Users/A/.cursor/extensions/openai.chatgpt-0.4.71-universal/webview/#)
    

수정은 아직 하지 않았습니다.

원하면 다음 순서로 바로 패치 들어가겠습니다:

1. 보스 경직/인터럽트 정책 정상화(m_bossCanBeHitstunned 반영, 보스 강제 예외 제거)
2. 스태미나 소모/회복 파이프라인 추가
3. 패링 윈도우를 설정값 기반으로 분리하고 히트스탑-데미지 지연 결합 해제

아래는 **지금까지 나온 “델리게이트 + FSM/상태” 위험 포인트 전부를** 한데 모아서 **위험도(심각도) 높은 순**으로 재정렬한 겁니다. (라인/근거는 네가 적어준 그대로 유지)

---

## 🔥 높음 (터지면 크래시/UAF 또는 체감 버그로 바로 드러남)

### 1) **바인드한 세션 ≠ 언바인드하는 세션** → stale callback / UAF 가능

- **Bind:** `m_session`에 직접 바인드 (CombatVfxBridgeScript.cpp **line 449**)
- **Unbind:** `m_session`이 아니라 “이름 재조회 결과”에 언바인드 (CombatVfxBridgeScript.cpp **line 461**)
- 세션 변경 처리도 `Unbind -> m_session 교체` 순서인데, Unbind 내부가 재조회라 **구 세션이 아니라 신 세션을 끊을 수 있음** (CombatVfxBridgeScript.cpp **line 406, 409**)
- Delegate 구현이 **raw pointer 캡처(single-cast)**라, 잘못 끊기면 **파괴된 객체로 콜백 호출(UAF)** 여지 (Delegate.h **line 21**)

**권장 대응(강하게 말하면 이건 꼭 고쳐야 함):**

- Unbind는 “그때 바인드했던 정확히 그 객체/세션 인스턴스”에만 하게 만들어야 함 (재조회 금지).
- 가능하면 “바인드 토큰/핸들” 같은 식별자로 해제하거나, 최소한 `m_boundSession` 따로 들고 해제.

---

### 2) **라이트 콤보 2타 클립 fallback이 가드 진입 클립** → 애니/판정/타이밍 동기화 붕괴

- `idx == 2`에서 `lightAttackClip2` 없으면 `guardEnterClip` 반환 (C_CombatSessionComponent.cpp **line 3060**)
- 결과: **공격 상태인데 가드 애니가 재생**될 수 있고, 그 순간부터 **윈도우/트레이스/히트판정/입력 상태**가 어긋날 확률이 큼 (체감 버그로 바로 나타나는 타입)

**권장 대응:**

- fallback은 “같은 계열(공격)”로만 가야 함. 없으면 `lightAttackClip1`이나 “명시적 error/none” 처리 + 로그/검증.
- 애셋 누락을 런타임에서 땜질하지 말고 에디터/빌드 타임 검증으로 잡는 게 맞음.

---

### 3) publisher가 먼저 Unbind하면 **bridge 플래그(m_resolveBound)와 실제 바인딩 상태 불일치** → 이벤트 영구 미수신

- 세션 컴포넌트가 disable 시 델리게이트 직접 Unbind (C_CombatSessionComponent.cpp **line 604**)
- bridge는 `m_resolveBound`만 보고 재바인드를 막음 (CombatVfxBridgeScript.cpp **line 446**)
- 실제론 끊겼는데 bound라고 믿어서 **이후 이벤트를 못 받는 상태로 고착**
    
    (Start/Update에서 TryBind가 돌아도 early return) (CombatVfxBridgeScript.cpp **line 235, 264**)
    

**권장 대응:**

- `m_resolveBound` 같은 “믿음 플래그” 버리고, **실제 Delegate.IsBound() 기반**으로 판단하거나,
- 세션 disable에서 Unbind 하더라도 bridge가 다음 프레임에 강제 재바인드 가능하게 만들기.

---

## ⚠️ 중간 (크래시까진 덜해도 운영/확장 시 폭탄, 또는 특정 조건에서 꼬임)

### 4) single-cast 채널이라 **다중 구독 시 덮어쓰기/상호 해제 충돌**

- OnCombatResolvedVfx가 single-cast (C_CombatSessionComponent.h **line 39**, Delegate.h **line 53**)
- BindObject가 기존 콜백 덮어씀 (Delegate.h **line 21**)
- bridge disable에서 `Unbind()`를 “전체 해제”로 호출 → 다른 구독자까지 끊길 수 있음 (CombatVfxBridgeScript.cpp **line 462**)

**권장 대응:**

- 다중 구독이 가능해야 하면 multicast로 바꾸거나,
- single-cast 유지할 거면 “오직 하나만 구독”을 코드로 강제(어설픈 운영 규칙 금지).

---

### 5) 히트스탑 중에도 **보스 브레인의 전이/의사결정이 계속 진행** 가능

- 히트스탑 상황에서 bossLogicDt를 0으로 넘기지만, Tick()에서 `brain->Think(0, target)` 호출 (C_BossCombatSessionComponent.cpp **line 247**)
- Think 내부는 시간 누적 없이도(거리/각도/큐 조건 등) 상태를 바꾸는 분기 가능
- 히트스탑을 “완전 정지”로 기대하면 여기서 **상태 전이가 발생하는 설계 불일치** (C_CombatSessionComponent.cpp **line 703, 1697**)

**권장 대응:**

- 정책을 명확히: 히트스탑 중 “AI 전이도 정지”가 맞으면 Think 호출 자체를 막아야 함.
- “타이머만 정지”라면 문서화 + 디버그 가시화 필요.

---

## 🧯 낮음 (당장 치명적이진 않지만 품질/확장성/예측 가능성을 갉아먹음)

### 6) 월드 null 경로에서 **실제 Unbind 없이 플래그만 false**

- `world == nullptr`면 세션 델리게이트는 건드리지 않고 `m_resolveBound=false`만 (CombatVfxBridgeScript.cpp **line 458, 465**)
- teardown 순서에 따라 stale callback 잔존 가능성 낮지만 “없다고 단정”은 못 함

**권장 대응:**

- null 월드면 “바인드했던 세션을 알고 있으면 거기엔 반드시 Unbind 시도”가 더 안전.

---

### 7) 마우스 가드 입력이 `m_useMouseAttack`에 종속 → 설정 분리 불가

- `guardMouseDown/guardMouseHeld = m_useMouseAttack && ...` (C_PlayerInputSourceComponent.cpp **line 232, 234**)
- “마우스 공격 OFF + 마우스 가드 ON” 같은 조합을 못 만듦

**권장 대응:**

- 입력 옵션은 공격/가드 분리해야 UX/디버그가 편해짐.

---

### 8) 보스 브레인 기본 상태와 리셋 초기 상태 불일치

- 헤더 기본값: Orbit (C_BossBrainComponent.h **line 265**)
- ResetBrain: Idle (C_BossBrainComponent.cpp **line 1813**)
- Start/Disable에서 리셋되긴 하지만, 초기화 전에 참조하는 경로 있으면 일관성 이슈 가능

**권장 대응:**

- 기본값/리셋값을 동일하게 맞추고, “초기화 전 참조 금지”를 코드로 막는 게 깔끔.

---

### 9) 보스 AttackDriver 입력 업데이트가 intent 반영보다 앞섬 → 1프레임 꼬임 소지

- `UpdateDriverInput(bossId, bossIntentCompat, ...)` 호출 시점에 bossIntentCompat가 아직 기본값 (C_CombatSessionComponent.cpp **line 1307, 1799**)
- 지금은 보스가 guard 입력 거의 안 써서 티가 덜 나도, 확장하면 꼬일 수 있음

**권장 대응:**

- intent 계산 → driver input 적용 순서를 보장.

---

## 결론(한 줄로 정리)

- **지금 “반드시 손봐야 하는 1순위”는 델리게이트 세션 언바인드 불일치(UAF 가능) + 콤보2 fallback(체감 버그)** 이 두 개야.
- 그 다음은 **바인딩 상태 불일치로 이벤트 영구 미수신**(디버깅 지옥)이 뒤따르고, 나머지는 설계/정책/확장성 문제로 천천히 갉아먹는 타입.

원하면 위 9개를 기준으로 **“재현 시나리오(어떤 순서로 어떤 상태에서 터지는지)”**까지 짧게 적어서 QA 체크리스트처럼 만들어줄 수도 있음.