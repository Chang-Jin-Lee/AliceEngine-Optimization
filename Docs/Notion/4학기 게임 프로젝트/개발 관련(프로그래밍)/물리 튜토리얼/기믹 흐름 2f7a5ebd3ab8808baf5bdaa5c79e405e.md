# 기믹 흐름

- 레거시
    
    오브젝트 목록
    
    WB_Base
    WB_Back
    
    WB_FrontA
    
    WB_FrontB
    
    WB_FrontC
    
    WB_FrontD
    
    W_EYE
    
    W_Core
    
    W_Tendon
    
    Weapon(combined)
    
    1. Weapon(combined)가 플레이어 소켓에 달려있음(AttachSocketComopnent - 일단 비워두셈)
        
        W_ / WB_로 시작하는 오브젝트들은 비활성화(Enabled false) 상태
        
        이상태에서 기본적인 전투가 진행됨(기본이 되는 상태)
        
    
    1. 스페이스(정확히는 충족 조건이지만, 테스트를 위해 스페이스바)가 눌리게 되면 아래 내용이 동시에 진행됨
        
        A)
        - Weapon(combined) Enabled false(비활성화 됨)
        - W_Core abled(같은 소캣 - AttachSocketComponent를 이용해 같은 자리에서 생성됨 enabled true)
        즉, W_Core ↔ Weapon(combined) 교체됨
        플레이어의 손에 있던 무기가 교체되는거임
        - W_Tendon은 Visible false(안보이지만 결합된 상태 - W_Core와 같은 피벗이라서 그냥 놔두면 됨, 자식으로 상속해둘꺼니 신경 X)
        - 이 연출은 낫의 날이 부셔져서 막대(봉)만 남은 상태임, W_Tendon은 이후 알파값으로 연출 할 예정 - 
        
        B)
        - WB_ 접두사를 가진 오브젝트가 활성화(Enabled true)
        - 생성되는 위치는 W_Croe를 기준으로 상대 좌표가 있음(Y좌표 +2정도임, 조절해야하니까 고정X), 임의로 조정 가능한 상대좌표
        - 생성된 위치에서 WB_오브젝트들과 W_EYE가 각각 다른 방향으로 적당히 날아감(물리기반 날아감) (날아가는 거리(힘)은 상한선을 지정할 수 있게, 각도는 360도 전방향 랜덤)  
        - WB_오브젝트들은 ConvexCollider를 가지고 있고 RigideBody를 가지고 있음
        또한, 플레이어와 보스와의 충돌을 무시함 - WB_들과 W_EYE는 각각 서로 충돌 가능함
        - 이 연출은 낫 무기의 날부분이 부셔져서 파편화 되는 연출임
        
    
    1. 스페이스를 다시 누르면, W_EYE가 isTrigger가 켜지면서(충돌X) 공중으로 떠오름 - 약 Y 1정도
    이후, W_EYE 주변으로 WB_접두사로 시작하는 오브젝트들이 서서히 모여들음 - 가까운 순서대로 하나씩 순차적으로 모여듬 - 이때는 지구와 달 처럼, EYE를 기준으로 파편들이 회전함 - WB_ 오브젝트들도 isTrigger ON - 겹침 허용 상태
    즉, W_EYE가 부셔진 파편을 자석으로 끌어 들이는 것 처럼 하나하나(약 1초 간격)으로 가까운것부터 잡아당겨서 마치 행성과 그 위성들 처럼 공중에 뜨는 연출임
    
    2. 스페이스를 다시 누르면, WB_로 시작하는 오브젝트들이 처음 위치(날아가기 시작한 상대 좌표, 2-B의 상대좌표)로 날아가서 결합됨, 순서는 WB_Base → WB_BACK → WB_A → B → C → D
    굳이 물리기반 움직임일 필요 없고, 보간을 통한 트랜스폼 이동을 시켜도 됨, 플레이어가 유동적으로 움직이고 있을꺼라, 무기가 날아가서 챡 달라붙어야함
    이 결합 또한 위와 같은 간격(약 1초)으로 위에서 언급한 순서대로 진행됨
    
    1. 스페이스를 다시 누르면, WB_EYE가 첫 위치(날아가기 시작한 상대좌표)로 날아감 << 이녀석은 가장 마지막에 붙고, 다른 WB_FRONT/BACk/BASE와 다르게 빠르고 직선적으로 움직임
    W_EYE가 자리잡으면 1초뒤에
    W_Tendon이 visible true로 전환됨(나중에 이부분은 알파값 연출을 넣을껀데, 지금 당장은 알파 조절 기능이 없으므로 1초 뒤 전환으로 설정함)
    
    1. 모든 조립이 완료된 W_Tendon이 나타난 시점에, 다시 WB_ / W_ 접두사를 가진 모든 오브젝트를 Enabled false로 전환함 이후 Weapon(combined)를 Enabled True로 전환
    즉, 조립이 완료되고 원래 상태로 복귀하는거임
    

## Phase 스펙 (스페이스 누를 때마다 다음으로)

### Phase 0: Normal (기본 전투)

- `WeaponCombined: Enabled=true`
- `W_Core, W_Tendon, W_EYE, WB_*: Enabled=false`

### Phase 1: Break (부서져서 파편 날아감)

- `WeaponCombined: Enabled=false`
- `W_Core: Enabled=true` (`WeaponCombined` 과 같은 소켓에 Attach - 원래부터 되어있을꺼긴 함)
- `W_Tendon: Enabled=true, Visible=false` (자식으로 두든 말든 상관없지만 기준은 W_Core)
- `WB_* + W_EYE: Enabled=true` (파편들임)
- 파편 스폰 위치: `W_Core.WorldTransform * (BreakSpawnOffset)` 모든 파편은 같은 피벗으로 맞춰져 있음, 한 점에서 출발하면 됨)
- 파편 물리:
    - `RigidBody: Dynamic`
    - `Collider: Convex, isTrigger=false` (미리 붙여둘것)
    - 충돌 필터: **플레이어/보스 무시**, 파편끼리는 충돌 허용(이그노어 마스크 레이어 사용하면 될듯)
- 힘(임펄스):
    - 방향: 3D 랜덤(360도)
    - 크기: `0 ~ MaxForce` (상한 노출)

### Phase 2: Magnetize (EYE 떠오르고 파편이 하나씩 끌려옴 + 공전)

- `W_EYE: isTrigger=true` (충돌X), 원하는 높이로 이동(예: +Y 1)
- `WB_*: isTrigger=true` (겹침 허용)
- “가까운 순서대로 1초 간격” = 규칙을 딱 한 줄로:
    - 매 1초마다, 아직 미포획 파편 중 **EYE와 거리 최소인 1개를 ‘포획’ 상태로 전환**
- 포획된 파편은 EYE 중심으로 “행성-위성”처럼 공전:
    - kinematic + 각속도 기반 (대부분 이게 디버깅 편함)

### Phase 3: AssembleShards (파편 조립: Base → Back → A→B→C→D)

- 순서대로 1초 간격:
    - 해당 `WB_*`를 `BindLocalTransform` 위치로 **보간 이동**
    - 이동 중 기준은 항상 **현재의 W_Core** (플레이어가 움직여도 “챡” 붙게 하려면 여기 고정이 핵심)
- 붙으면:
    - 파편 `RigidBody 비활성/kinematic`, `Collider isTrigger=true 또는 비활성`
    - `W_Core`의 자식으로 붙여버리는 게 안전

### Phase 4: AssembleEye (EYE는 빠르고 직선으로 마지막 결합)

- `W_EYE`를 `BindLocalTransform`으로 빠른 직선 이동
- 도착 후 1초 뒤 `W_Tendon: Visible=true`

### Phase 5: Restore (원래 무기로 복귀)

- `W_ / WB_ 전부 Enabled=false`
- `WeaponCombined Enabled=true`