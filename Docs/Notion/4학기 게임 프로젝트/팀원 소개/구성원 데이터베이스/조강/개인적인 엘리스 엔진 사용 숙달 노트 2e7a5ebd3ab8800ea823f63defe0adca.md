# 개인적인 엘리스 엔진 사용 숙달 노트

- Alice Engine 사용하는 방법
    - 컴포넌트 생성할 때에는 gameobject에서 addcomponent를 해줌
    - 컴포넌트를 만들게 되면
        
        ```cpp
        rttr::registration::class<component...>("component name").constructor<>().property().property...
        ```
        
        이렇게 엔진에서 등록을 해야할 때 ⇒ 에디터에서 작업이 필요할 때 넣어주기
        
    - namespace Alice 추가 해주기

- 순서
    - 타이머
    - 업데이트
        - 타이머
        - input
        - Camera
        - Transfrom
        - mouse → view matrix
        - Scene Manager
            - 현재 Scene Update
    - 렌더
        - 모든 렌더타겟 및 세이더 리소스, 버퍼 해제
        - Flush
        - UI render
        - 애니메이션
        - 카메라 entity
        - Forward / Deferred render
        - Tone Mapping(Game Mode)
        - ImGui