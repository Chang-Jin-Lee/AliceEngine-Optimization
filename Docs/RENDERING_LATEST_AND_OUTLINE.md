# Rendering 최신 가이드 (Deferred Outline 포함)

이 문서는 현재 엔진의 렌더링 기능을 **실제 코드 기준 최신 상태**로 정리한 문서입니다.  
목표는 다음 두 가지입니다.

1. 아트/레벨 작업자가 UI에서 어떤 값을 만지면 무엇이 바뀌는지 빠르게 이해
2. 다른 AI/엔지니어가 렌더링 흐름과 구현 지점을 바로 찾을 수 있게 기준 문서 제공

---

## 1) 렌더러 모드와 적용 범위

- 상단 메뉴 `Forward Rendering` 체크
  - 체크: Forward 렌더러 사용
  - 해제: Deferred 렌더러 사용
- 에디터에서 모드를 바꾸면 엔진이 프레임 경계에서 렌더러 상태를 전환합니다.
- **중요**: Deferred 전용 기능(예: Edge Detection Outline)은 Deferred 모드에서만 동작합니다.

관련 코드:
- `Engine/src/Editor/UI/EditorMainMenuBar.cpp`
- `Engine/src/Runtime/Engine/EngineRender.cpp`

---

## 2) 최신 렌더링 기능 요약

### 공통 (Forward/Deferred 공통 체감 기능)

- 머티리얼 셰이딩 모드
  - `Lambert`, `Phong`, `Blinn-Phong`, `Toon`, `PBR`, `ToonPBR`, `ToonPBREditable`, `OnlyTextureWithOutline`
- 머티리얼 커스텀 파라미터 (Inspector)
  - `Shadow Intensity` (머티리얼별 그림자 영향)
  - `Toon Ramp Intensity` (ToonPBREditable 어두운 밴드 완화)
  - `Toon Self Shadow` (자체 음영 강도)
  - 수치 편집은 Drag + 더블클릭 직접 입력(정밀 값 입력) 지원
- 그림자 품질
  - `Shadow Map Size`
  - `Shadow PCF Radius`
- 주광 방향 입력 개선
  - `Yaw / Pitch` 드래그 + 숫자 직접 입력
  - `Roll` 항목은 제거됨 (주광 방향 벡터 제어에 불필요)

### LightingPanel 최신 동작

- `Tone Mapping`은 LightingPanel에서 직접 조절하지 않음
- 대신 안내 문구를 표시:
  - `포스트 프로세스 볼륨으로 조절해야합니다. 포스트프로세스 볼륨을 만들고 Unbound로 조절하세요.`
- 즉, 톤매핑/컬러그레이딩은 PostProcessVolume(특히 Unbound)에서 관리

관련 코드:
- `Engine/src/Editor/Panels/LightingPanel.cpp`
- `Engine/src/Editor/Inspector/Inspector_Rendering.cpp`

---

## 3) 아웃라인 구현 방식 (현재 기준)

현재 엔진에는 경로가 2개 있습니다.

1. Forward: Inverted Hull(기존 방식)
2. Deferred: Edge Detection(Sobel) 기반 외곽선 합성 (최신)

실무 권장:
- 캐릭터/메시 외곽선 품질과 제어 안정성은 Deferred Edge 방식 기준으로 사용

---

## 4) Deferred Edge Outline 구현 상세

### 4.1 데이터 원천 (Material)

- 머티리얼 컴포넌트:
  - `outlineColor` (`float3`)
  - `outlineWidth` (`float`)
- Inspector에서 Reflection UI로 노출되어 수치 직접 입력 가능
- 스키닝 렌더 커맨드까지 동일 값이 전달됨

관련 코드:
- `Engine/src/Runtime/Rendering/Components/MaterialComponent.h`
- `Engine/src/Editor/Inspector/Inspector_Rendering.cpp`
- `Engine/src/Runtime/Gameplay/Animation/SkinnedMeshSystem.h`

### 4.2 G-Buffer 확장

- Deferred GBuffer MRT를 5개 -> 6개로 확장
- 추가 타깃: `OutlineData`
  - 포맷: `R16G16B16A16_FLOAT`
  - 의미: `rgb = outlineColor`, `a = outlineWidth`

관련 코드:
- `Engine/src/Runtime/Rendering/DeferredRenderSystem.h`
- `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`

### 4.3 GBuffer Pixel Shader 기록

- GBuffer 패스에서 모든 픽셀에 대해 `OutlineData`를 기록
- 기록식:
  - `OutlineData = float4(saturate(gOutlineColor), max(gOutlineWidth, 0.0f))`
- 이전처럼 “아웃라인 전용 Pass2를 별도 그려서 색 덮기”가 아니라, 메타데이터 기록 후 라이트 패스에서 합성

관련 코드:
- `Engine/src/Runtime/Rendering/ShaderCode/DeferredShader.h`

### 4.4 라이트 패스 Edge Detection

- 라이트 패스에서 `g_OutlineData`를 샘플링하고 Sobel 3x3으로 엣지를 계산
- 핵심 함수:
  - `ComputeOutlineEdge(float2 uv, float2 texelSize, out float3 edgeColor, out float maxOutlineWidth)`
- 두께 반영:
  - `widthPx = clamp(maxOutlineWidth * 120.0f, 1.0f, 8.0f)`
  - 이 값으로 Sobel 샘플 오프셋 거리(`stepUV`)를 조절
- 색 합성:
  - 물체 픽셀: `color = lerp(litColor, outlineEdgeColor, outlineEdge)`
  - 배경 픽셀(depth=1): edge가 있으면 외곽선만 출력

효과:
- 메시 바깥 실루엣 라인이 안정적으로 보임
- 머티리얼별 색/두께 제어 가능

관련 코드:
- `Engine/src/Runtime/Rendering/ShaderCode/DeferredShader.h`
- `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp` (SRV 바인딩 슬롯 포함)

---

## 5) Forward Outline 구현 상태

- Forward는 기존 `Inverted Hull` 방식 유지
  - Pass1: 원본
  - Pass2: `CullFront` + 정점 노말 방향 확장 + 단색 출력
- 따라서 Forward에서 보이는 아웃라인은 Deferred Edge 방식과 시각적 특성이 다를 수 있습니다.

관련 코드:
- `Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp`
- `Engine/src/Runtime/Rendering/ShaderCode/ForwardShader.h`

---

## 6) 아트팀 사용 가이드 (실전)

### 6.1 아웃라인 켜는 순서 (Deferred 기준)

1. 상단에서 `Forward Rendering` 체크 해제 (Deferred 사용)
2. 대상 오브젝트의 머티리얼 선택
3. `outlineWidth` 값을 0보다 크게 설정
4. `outlineColor`를 원하는 색으로 설정
5. 필요 시 카메라 거리/배경 대비를 조절해 실루엣 확인

### 6.2 권장 시작값

- 캐릭터 외곽선 시작점:
  - `outlineWidth`: 0.01 ~ 0.03
  - `outlineColor`: 완전 검정보다 살짝 채도/밝기 있는 톤 추천

### 6.3 그림자/툰 관련 같이 조절하기

- `Shadow Intensity`: 물체가 받는 그림자 강도
- `Toon Self Shadow`: 캐릭터 자체 음영 강도
- `Toon Ramp Intensity`: 가장 어두운 밴드 완화

---

## 7) 트러블슈팅 체크리스트

### 아웃라인이 안 보일 때

1. 렌더러가 Forward인지 Deferred인지 먼저 확인
2. 머티리얼 `outlineWidth`가 0인지 확인
3. `outlineColor`가 배경/피사체와 너무 유사한지 확인
4. 머티리얼이 투명(`transparent`)인지 확인
   - 투명 경로는 별도 패스를 타므로 결과가 다를 수 있음
5. 셰이딩 모드/광량이 과도해 라인이 묻히는지 확인

### 톤매핑이 안 바뀌는 것처럼 보일 때

- 정상 동작입니다. LightingPanel에서는 직접 조절하지 않음
- PostProcessVolume 생성 후 `Unbound`로 톤매핑 파라미터를 조절해야 함

---

## 8) 구현 파일 인덱스

- Deferred 렌더링 메인
  - `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`
  - `Engine/src/Runtime/Rendering/DeferredRenderSystem.h`
- Deferred 셰이더 코드
  - `Engine/src/Runtime/Rendering/ShaderCode/DeferredShader.h`
- Forward 렌더링 메인
  - `Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp`
- Forward 셰이더 코드
  - `Engine/src/Runtime/Rendering/ShaderCode/ForwardShader.h`
- 머티리얼 컴포넌트/인스펙터
  - `Engine/src/Runtime/Rendering/Components/MaterialComponent.h`
  - `Engine/src/Editor/Inspector/Inspector_Rendering.cpp`
- 라이팅 패널
  - `Engine/src/Editor/Panels/LightingPanel.cpp`

---

## 9) 변경 이력 (문서)

- 렌더링 최신 정리 및 Deferred Edge Outline 상세 문서 추가
- LightingPanel 톤매핑 제어 정책(PostProcessVolume 기반) 반영
- 머티리얼 그림자/툰 파라미터 사용 가이드 반영
- HalfCut(카메라 화면 분할) 포스트프로세스 기능 및 사용 가이드 반영
- HalfCut 확장 기능(절단면 수학 FX, FOV 커브, VFX 페이드) 및 트러블슈팅 기록 반영

---

## 10) HalfCut 카메라 분할 연출 (신규)

### 10.1 기능 개요

- HalfCut은 **톤매핑 단계의 풀스크린 포스트프로세스**입니다.
- 화면을 하나의 분할선으로 나눠 위/아래 영역 UV를 반대 방향으로 이동시켜 “카메라가 잘린 듯한” 연출을 만듭니다.
- 실제 카메라(Projection/View)나 월드 Transform을 바꾸지 않습니다.

### 10.2 제어 파라미터 (PostProcessSettings)

- `splitAmount`: 분할 이동 강도 (`0`이면 비활성)
- `splitAngleDeg`: 분할선 각도(도)
- `splitLineOffset`: 분할선 위치 오프셋(정규화 공간)
- `splitFeather`: 분할 경계 블렌딩 폭

관련 코드:
- `Engine/src/Runtime/Rendering/PostProcessSettings.h`
- `Engine/src/Runtime/Rendering/RenderTypes.h`
- `Engine/src/Runtime/Rendering/ShaderCode/CommonShaderCode.h`

### 10.3 사용 방법 (권장)

1. 월드에 `PostProcessVolume` 생성
2. `Unbound = true` 설정
3. 아래 Override를 켠 뒤 값 조절
   - `bOverride_SplitAmount`
   - `bOverride_SplitAngleDeg`
   - `bOverride_SplitLineOffset`
   - `bOverride_SplitFeather`
4. 런타임 연출 스크립트에서 `splitAmount`를 시간 커브로 제어

데모 리소스:
- 씬: `Assets/Scenes/Camera/CameraHalfCut.scene`
- 스크립트: `Assets/Scripts/Camera/CameraHalfCutDemo.h`
- 스크립트: `Assets/Scripts/Camera/CameraHalfCutDemo.cpp`

### 10.4 호환성/안정성 범위

- Forward/Deferred 모두 반영됩니다.
- 기본값(`splitAmount = 0`)에서는 기존 출력과 동일한 경로로 동작합니다.
- 영향 범위는 포스트프로세스(톤매핑) 단계로 제한됩니다.
  - 물리 시뮬레이션, 애니메이션 계산, 게임 로직 dt에는 영향이 없습니다.
  - 렌더링 본 패스(GBuffer/Shadow/SkinnedMesh) 데이터에는 영향이 없습니다.

### 10.5 주의사항

- `splitAmount`를 과도하게 키우면 화면 가장자리 샘플이 늘어나 경계 왜곡이 눈에 띌 수 있습니다.
- 분할선 각도는 UV 공간 기준이므로 종횡비에 따라 체감 각도가 달라 보일 수 있습니다.

---

## 11) HalfCut 확장 (절단면 수학 FX + FOV 커브 + VFX 동기화)

### 11.1 이번 확장에서 추가된 파라미터

- `splitFxIntensity`: 절단면 하이라이트/스파크 강도 (`0`이면 완전 비활성)
- `splitFxWidth`: 절단면 영향 폭(정규화 UV 공간)
- `splitFxSpeed`: 절단선 방향으로 흐르는 노이즈 속도
- `splitFxTimeSec`: 절단 FX 시간 입력값(스크립트에서 구동)

관련 코드:
- `Engine/src/Runtime/Rendering/PostProcessSettings.h`
- `Engine/src/Runtime/Rendering/RenderTypes.h`
- `Engine/src/Runtime/ECS/ComponentRegistry.cpp`
- `Engine/src/Runtime/Rendering/PostProcessVolumeSystem.cpp`

### 11.2 렌더링 데이터 전달 경로

1. `PostProcessVolume.settings`에서 `splitFx*` 값을 override 기반으로 입력
2. `PostProcessVolumeSystem::CalculateFinalSettings()`에서 볼륨 블렌딩
3. `ForwardRenderSystem` / `DeferredRenderSystem`의 `m_postProcessParams`에 반영
4. `PostProcessCB.splitFxParams`로 GPU 상수버퍼 업로드
5. `CommonShaderCode.h` 톤매핑 픽셀 셰이더(LDR/HDR)에서 최종 합성

핵심 파일:
- `Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp`
- `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`
- `Engine/src/Runtime/Rendering/ShaderCode/CommonShaderCode.h`

### 11.3 절단면 수학 FX 구현 방식 (텍스처 미사용)

- 분할선 기준 signed distance(`side`)와 분할선 방향 좌표(`along`)를 계산
- `exp(-abs(side)/width)` 기반으로 절단선 근처 밴드/코어 마스크 생성
- `Hash11` 기반 1D 노이즈를 시간(`splitFxTimeSec`)과 속도(`splitFxSpeed`)로 스크롤
- 노이즈를 스파크 마스크로 변환해 emissive 성분(따뜻한 glow + 냉색 fringe) 합성
- 기존 split UV 이동(`splitAmount`)과 독립적으로 동작하지만 같은 분할선 파라미터를 공유

### 11.4 CameraHalfCut 데모 스크립트 연출 흐름

- 데모 씬: `Assets/Scenes/Camera/CameraHalfCut.scene`
- 데모 스크립트: `Assets/Scripts/Camera/CameraHalfCutDemo.h`
- 데모 스크립트: `Assets/Scripts/Camera/CameraHalfCutDemo.cpp`

동작:
- `1` 키 입력 시 연출 시작
- Attack/Hold/Release 구간으로 split/bloom/exposure를 시간 제어
- FOV는 `UICurveAsset`(`Assets/Curves/UI/FadeInOut.uicurve`)으로 비선형 제어
- VFX는 `LeftCube`/`RightCube` 중점에서 생성되어 연출 중 루프, 종료 구간에서 서서히 fade-out
- 종료 시 PostProcess override 및 카메라 FOV를 baseline으로 복원

### 11.5 구현 중 발생한 문제와 해결

- 문제 1: Forward의 PostProcess 상수버퍼 크기가 `sizeof(float) * 4`로 고정되어, 확장된 `PostProcessCB`와 불일치
- 증상: 확장 파라미터 사용 시 메모리 overwrite/렌더링 이상 가능성
- 해결: Forward/Deferred 모두 `sizeof(PostProcessCB)` 기준(16-byte align)으로 버퍼 생성
  - `Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp`
  - `Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp`

- 문제 2: `CameraHalfCutDemo` 리팩터링 도중 `ApplyPostProcess` 시그니처와 호출부 불일치
- 해결: `timeSec` 전달 포함으로 통일하고 splitFx 시간 구동을 해당 함수 내부로 집약

- 문제 3: splitFx 신규 필드 baseline 복원이 누락되면 연출 종료 후 볼륨 상태가 남을 수 있음
- 해결: baseline capture/restore에 `splitFx*` 값과 override 플래그를 모두 추가

### 11.6 회귀 방지 기준

- 기본값에서 `splitFxIntensity = 0`이라 기존 씬은 동작 변화 없음
- override를 켜지 않으면 볼륨 블렌딩에 영향 없음
- 영향 범위는 톤매핑 포스트프로세스 단계로 제한
- 물리, 애니메이션 평가, 게임플레이 dt 경로에는 영향 없음
- 주광 회전 UI를 `Yaw/Pitch` 2축 기준으로 최신화 (`Roll` 제거)
- Inspector 수치 입력 방식(더블클릭 정밀 입력) 반영