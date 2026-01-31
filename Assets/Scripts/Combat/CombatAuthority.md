 # Combat Authority Rules (ScriptCombat)
 
 ## A) Trace authority
 - ScriptCombat mode: trace 활성/비활성은 스크립트만 권한 보유.
 - 경로: FSM Command → CombatApply → WeaponTraceComponent.
 - Engine AttackDriverSystem은 ScriptCombat 모드에서 trace 토글 금지.
 
 ## B) Window/flags source of truth
 - 윈도우(attack/guard/dodge/parry 등)의 진실은 AnimNotify/AttackDriver가 계산한 Sensors 값.
 - FSM은 Sensors 윈도우를 그대로 flags에 복사(pass-through).
 - Resolver는 flags만 보고 판정(상태명 분기 금지).
 
 ## C) Apply 책임
 - Apply는 즉시 안전장치만 수행.
 - ForceCancelAttack은 같은 프레임에 trace 비활성 포함.
 - 상태 전환은 DeferredEvent로 다음 프레임 FSM이 수행.
 
 ## Edge rules
 - attackInstanceId는 hit window 상승 edge에서 증가.
 - 중복 히트 방지(hitVictims)는 trace 활성화 시 리셋.
