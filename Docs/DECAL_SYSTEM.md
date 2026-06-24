# Decal System (DX11 Deferred)

이 문서는 EGOSIS 엔진의 데칼 시스템 구현과 사용법을 한 번에 이해할 수 있게 정리한 설명서입니다.

## 목적
- 벽/바닥에 스티커처럼 투영되는 데칼을 표현합니다.
- Deferred 파이프라인의 DBuffer 방식으로 구현했습니다.
- 에디터와 게임 모드 모두에서 동일하게 동작합니다.

## 구현 요약
- 데칼은 “표면에 실제로 붙는 텍스처”가 아니라, 렌더링 시점에 투영되는 별도 패스입니다.
- BasePass 이후에 DecalPass를 실행하여 DBuffer(현재는 알베도 1장)에 누적합니다.
- Deferred Light 패스에서 GBuffer와 DBuffer를 합성하여 최종 색을 만듭니다.

## 데이터 구조
### DecalComponent
- 위치/회전/스케일은 TransformComponent를 사용합니다.
- 필드
- `enabled` : 데칼 활성화 여부
- `albedoTexturePath` : 데칼 텍스처 경로 (RGBA, A=마스크)
- `color` : 틴트 색상
- `opacity` : 전역 불투명도
- `sortOrder` : 데칼 정렬 순서
- `uvScale` / `uvOffset` : UV 제어

## 렌더링 흐름
1. GBuffer Pass
- 일반 메시를 GBuffer에 렌더링합니다.

2. Decal Pass
- DBuffer를 0으로 클리어합니다.
- 데칼 엔티티를 수집하고 정렬합니다.
- 데칼마다 큐브 볼륨을 그립니다.
- Depth SRV로 월드 위치를 복원합니다.
- WorldToDecal로 로컬 좌표 변환 후 박스 범위 밖이면 discard합니다.
- 데칼 텍스처를 샘플링하고 premultiplied alpha로 DBuffer에 누적합니다.

3. Deferred Light Pass
- GBuffer + DBuffer를 합성합니다.
- 현재 구현은 알베도만 합성합니다.
- 공식: `baseColor = baseColor * (1 - alpha) + decalColor`

## 핵심 셰이더 동작 (알베도 전용)
- `DecalPS`는 `g_SceneDepth`로 월드 위치 복원 후 데칼 박스 내부만 처리합니다.
- `DBufferA`에는 premultiplied 알베도와 알파를 기록합니다.

## 에디터 기능
### 인스펙터
- DecalComponent 전용 인스펙터에서 알베도 텍스처 선택 및 파라미터 편집.
- Drag&Drop 및 Browse 지원.

### 디버그 뷰
- Game 뷰포트 상단 `View` 콤보에서 `Decal DBuffer` 선택 시 DBuffer를 직접 확인 가능.
- Forward 렌더링 모드에서는 사용 불가.

### 데칼 볼륨 디버그 드로우
- `Show DebugDraw`가 켜져 있으면 데칼 박스가 라인으로 표시됩니다.
- 선택된 데칼은 강조 색으로 표시됩니다.

## 사용 방법 (빠른 시작)
1. 씬에 데칼 엔티티 생성
2. DecalComponent 추가
3. `albedoTexturePath`에 데칼 텍스처 지정
4. Transform으로 위치/회전/스케일 조정

## 데모 씬
- `EGOSIS/Assets/Scenes/Decal/DecalDemo.scene`
- `Resource/Test/Image/Hanako.png` 데칼 텍스처 사용
- `Assets/Fbx/Cube.fbxasset`로 만든 벽/바닥에 투영
- `DecalDemoController` 스크립트로 회전/UV 스크롤/불투명도 펄스

## 알려진 제한
- 알베도 전용 데칼만 지원합니다. (Normal/RMA 미지원)
- 수신자(Receiver) 마스크/스텐실 필터 미구현입니다.
- Forward 렌더링 모드에서는 데칼 패스가 동작하지 않습니다.

## 발생했던 문제와 해결
### 문제: 인스펙터에 “Decal” 섹션이 2개 출력됨
- 원인: DecalComponent가 전용 인스펙터와 레지스트리 기반 일반 인스펙터에 동시에 노출됨.
- 해결: 레지스트리 기반 일반 렌더 목록에서 DecalComponent를 제외.

### 문제: ImGui ID 충돌 경고
- 원인: 동일한 라벨이 여러 섹션에서 중복 사용될 때 ImGui ID 충돌 발생.
- 해결: Decal 인스펙터에 `PushID("DecalComponent")` + `Decal##DecalComponent` 라벨 적용.

## 주요 파일 위치
- `Engine/src/Runtime/Rendering/Components/DecalComponent.h`
- `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`
- `Engine/src/Runtime/Rendering/ShaderCode/DeferredShader.h`
- `Engine/src/Editor/Inspector/Inspector_Rendering.cpp`
- `Engine/src/Editor/Panels/GameViewportPanel.cpp`
- `Engine/src/Runtime/Rendering/DebugDrawComponentSystem.cpp`

## 확장 아이디어
- DBuffer에 Normal/RMA 추가
- Receiver 마스크(Stencil/Material 플래그) 추가
- 거리 페이드, 슬로프 제한, 투영 방향 제한

