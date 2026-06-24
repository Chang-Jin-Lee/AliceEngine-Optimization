# UI/UX 기획

[튜토리얼  및 조작법 설명 방식](UI%20UX%20%EA%B8%B0%ED%9A%8D/%ED%8A%9C%ED%86%A0%EB%A6%AC%EC%96%BC%20%EB%B0%8F%20%EC%A1%B0%EC%9E%91%EB%B2%95%20%EC%84%A4%EB%AA%85%20%EB%B0%A9%EC%8B%9D%202fca5ebd3ab8802e8149fcff22f9e33b.md)

[02/04 추가) 사망 및 전투 클리어 시 UI](UI%20UX%20%EA%B8%B0%ED%9A%8D/02%2004%20%EC%B6%94%EA%B0%80)%20%EC%82%AC%EB%A7%9D%20%EB%B0%8F%20%EC%A0%84%ED%88%AC%20%ED%81%B4%EB%A6%AC%EC%96%B4%20%EC%8B%9C%20UI%202fda5ebd3ab8804799e1ea5b303b6fc2.md)

[폰트 - 눈누](UI%20UX%20%EA%B8%B0%ED%9A%8D/%ED%8F%B0%ED%8A%B8%20-%20%EB%88%88%EB%88%84%202fea5ebd3ab880b49701d392f12acc42.md)

[타이틀 화면 레퍼](UI%20UX%20%EA%B8%B0%ED%9A%8D/%ED%83%80%EC%9D%B4%ED%8B%80%20%ED%99%94%EB%A9%B4%20%EB%A0%88%ED%8D%BC%202ffa5ebd3ab8809e87c3fc3f211859dc.md)

[결과창  레퍼](UI%20UX%20%EA%B8%B0%ED%9A%8D/%EA%B2%B0%EA%B3%BC%EC%B0%BD%20%EB%A0%88%ED%8D%BC%20303a5ebd3ab880f4ae0ce050d5664115.md)

[그로기 알림 레퍼](UI%20UX%20%EA%B8%B0%ED%9A%8D/%EA%B7%B8%EB%A1%9C%EA%B8%B0%20%EC%95%8C%EB%A6%BC%20%EB%A0%88%ED%8D%BC%20303a5ebd3ab880b6aed6f81f227a35fe.md)

[락온 레퍼](UI%20UX%20%EA%B8%B0%ED%9A%8D/%EB%9D%BD%EC%98%A8%20%EB%A0%88%ED%8D%BC%20303a5ebd3ab8807d9a46e5fc300e4cfe.md)

![{5A782F10-A924-4E0A-95CC-7C0AFE22A8CC}.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/5A782F10-A924-4E0A-95CC-7C0AFE22A8CC.png)

![ui,ux 초안.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/uiux_%EC%B4%88%EC%95%88.png)

내용 및 기획 의도 1

| 넘버 | 이름  | 구성 | 내용 |  리소스 목록 | 상태 목록 |  |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 보스 이름 및 정보창 | 보스 이름, 보스 체력바, 강인도 바 | 보스 이름 표기,
체력 및 강인도 비율을 나타내는 바 | 텍스트필드,
붉은색 체력바,
노란색 체력바,
흰색 강인도 바,
부서진 강인도 바 | 꽉 채워진 체력바
데미지를 받아서 감소한 체력바
공격받기 이전 체력과 나중 체력을 구분하게 해주는 노란색 체력바,
꽉 채워진 강인도바,
데미지를 받아서 감소한 강인도바
부서진 강인도바(균열정도면 충분) |  |
| 2 | 에고웨폰 상태바
(고려 사항) | 원형 아이콘 | 에고웨폰 체력 비율을 나타내는 바
(에고웨폰 아이콘을  색깔 바 위에 씌워서 얼마나 줄어들고 있는지를 나타냄) | 흰색 바탕에 노란색 눈이 있는 낫이 그려진  아이콘, 
아이콘의 금 가고 눈을 감은 버젼, 
붉은색 레이어 마스크,
노란색 레이어 마스크 | 눈이 완벽히 떠 있는 사신낫 아이콘,
눈을 반쯤 감은 사신낫 아이콘,
레이어 마스크에따라 채워지는 빨간색 마스크, 체력이 단 것을 표현하는 노란색 마스크, 금이 가고 눈을 감은 사신낫 아이콘. |  |
| 3 | 플레이어 체력 바 | 체력바 | 플레이어 체력 비율을 나타내는 바
(플레이어 아이콘을  색깔 바 위에 씌워서 얼마나 줄어들고 있는지를 나타냄) | 붉은색 체력바, 
노란색 체력바,
플레이어 아이콘 | 꽉 채워진 체력바
데미지를 받아서 감소한 체력바
공격받기 이전 체력과 나중 체력을 구분하게 해주는 노란색 체력바, |  |
| 4 | 특정 쪽 방향으로 항하는 파티클 | 파티클 흐름 | 플레이어에서 웨폰에게로, 웨폰에서 나에게로 흐르는 파티클 흐름 | 빨간색 파티클 | 왼쪽에서 오른쪽으로 가는 파티클
오른쪽에서 왼쪽으로 가는 파티클  |  |
| 5 | 대사 출력 | 텍스트 박스 | 전투 중 나오는 대사 출력 | 글자 폰트 | 대사가 출력되는 상태, 점차 사라지는 상태 |  |
| 6  | 플레이어 스태미나 바 | 스테미나 바  | 플레이어 스태미나 비율을 나타내는 바` | 초록색 스태미나 바  | 이거 삭제 스태미나 이제 없습니다 |  |
| 7 | 조작법 출력
(말씀드렸지만 이건 없앨 가능성이 높아요) | 키보드 아이콘 
텍스트 박스 | 공격 조작접. 공격/ 강공격/회피/가드  | 키보드 아이콘 및 
텍스트 박스 | 아이콘으로 조작법 표시, 
옆에 텍스트박스로 나머지 표시.
이것도 삭제 |  |

1번 참고 자료

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%201.png)

3번 참고 자료

5번 참고 자료

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%202.png)

7번 참고 자료 

6번 참고 자료 

HP바 레퍼

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%203.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%204.png)

- 후보
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%205.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%206.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%207.png)
    

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%208.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%209.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2010.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2011.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2012.png)

![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2013.png)

엘든링

타이틀 UI

설정 UI

HUD

대사(인게임대사)

보스 UI 

빛의 계승자체 영어로

피격시 UI 변화 레퍼런스

[이펙트레퍼_피격시 테두리 변화_1.mp4](UI%20UX%20%EA%B8%B0%ED%9A%8D/%EC%9D%B4%ED%8E%99%ED%8A%B8%EB%A0%88%ED%8D%BC_%ED%94%BC%EA%B2%A9%EC%8B%9C_%ED%85%8C%EB%91%90%EB%A6%AC_%EB%B3%80%ED%99%94_1.mp4)

- 기존 레퍼
    
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2014.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2015.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2016.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2017.png)
    
    ![image.png](UI%20UX%20%EA%B8%B0%ED%9A%8D/image%2018.png)