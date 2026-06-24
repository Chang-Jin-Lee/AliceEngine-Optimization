# 앞잡

앞잡(페이탈) 진입 판정은 `C_ActionFsm`이 아니라 `C_CombatSessionComponent`에서 합니다.

- 진입 체크 위치: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1320`
- 조건:
    - `!m_state->fatal.active`
    - `playerIntent.lightAttackPressed`
    - `m_state->boss.state == Groggy`
    - `m_state->bossGroggyEnterBlendBlockSec <= 0`
    - 양쪽 전방 콘 판정 통과 (`InFrontCone` 2번): `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1329`

전방 판정 방식:

- `dot >= cos(m_fatalFrontAngleDeg * 0.5)` 비교
- 각도/거리 튜닝값은 여기:
    - `Assets/Scripts/Combat/C_CombatSessionComponent.h:206`
    - `Assets/Scripts/Combat/C_CombatSessionComponent.h:207`

진입 시 실제로 일어나는 것:

- `fatalTriggered`, `forceFatalAttack`, `m_state->fatal.active = true` 세팅: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1351`
- 입력 잠금 + 보스 groggy hold: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1419`
- 라이트 공격 강제 입력: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1816`
- 페이탈 시퀀스(보스 위치 보간/데미지/종료): `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3072`
- 페이탈 전용 공격 클립 선택: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3278`

즉, “앞잡 들어갔는지”는 `m_state->fatal.active`가 가장 확실한 기준입니다.

---

1. 진행중 여부
    
    `m_state->fatal.active`가 핵심입니다.
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:191`, `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3074`
    
2. 시작 프레임 포함 판정
    
    진입 프레임은 `fatalTriggered`가 1프레임 true라서, 실제 판정은 `fatalActive = (m_state->fatal.active || fatalTriggered)`로 씁니다.
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1295`, `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:1616`
    
3. 끝났는지 판정
    
    페이탈 종료 시 `m_state->fatal = {}`로 리셋됩니다.
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3166`, `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3168`
    
    (트랜스폼 없을 때도 즉시 리셋: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3081`)
    
4. 얼마나 진행됐는지
    
    경과 시간: `m_state->fatal.timerSec` (매 프레임 증가)
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3106`
    
    총 시간: `totalSec` 계산값
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3103`
    
    진행률은 사실상 `clamp(timerSec / totalSec, 0~1)`로 보면 됩니다.
    
    남은 시간은 코드에서도 `totalSec - timerSec`로 씁니다.
    
    참고: `Assets/Scripts/Combat/C_CombatSessionComponent.cpp:3161`
    

추가로, 외부 스크립트에서 바로 읽는 공개 Getter는 현재 없습니다 (`GetPlayerState/GetBossState`만 공개).

참고: `Assets/Scripts/Combat/C_CombatSessionComponent.h:29`

원하면 `IsFatalActive()`, `GetFatalProgress()`, `GetFatalRemainSec()` 바로 추가해줄 수 있습니다.