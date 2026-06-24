# Lighting Enhancements (IBL Control + Skybox Resolution)

This document explains how to implement:
- Per-material IBL scaling (`envDiffuseStrength`, `envSpecularStrength`)
- Skybox resolution switching (HDR/MDR) and persistence
- Proper Skybox Off behavior (no stale IBL)

The goal is to let artists reduce skybox influence on specific materials
without breaking existing scenes, and to allow runtime skybox resolution
selection with stable results.

> Note  
> 아웃라인 구현(특히 Deferred Edge Detection) 상세는  
> `엔진설명서/RENDERING_LATEST_AND_OUTLINE.md`를 기준으로 관리합니다.

---

## 1) Data Model Changes

### MaterialComponent
Add fields (default 1.0):
```
float envDiffuseStrength = 1.0f;
float envSpecularStrength = 1.0f;
```
Expose them in reflection (RTTR or equivalent) so inspector + scene
serialization work.

### RenderTypes / CBPerObject
Add to per-object constant buffer (aligned properly):
```
float envDiffuseStrength;
float envSpecularStrength;
```

### Material serialization
Clamp to >= 0.0f on load/save (no negative IBL scales).

---

## 2) Forward Rendering Path

### CPU
Pass env strengths from `MaterialComponent` into `UpdatePerObjectCB(...)`.

### Shader (Forward)
Add to per-object cbuffer:
```
float gEnvDiffuseStrength;
float gEnvSpecularStrength;
```
Apply on IBL terms (linear space):
```
diffuseIBL  *= gEnvDiffuseStrength;
specularIBL *= gEnvSpecularStrength;
```

---

## 3) Deferred Rendering Path

### 3.1 Non-ToonPBR encoding
If shading mode is not ToonPBR (5/7), store:
```
ToonParams.xy = float2(envDiffuseStrength, envSpecularStrength)
```

### 3.2 ToonPBR encoding (preserve toon params)
ToonPBR already uses ToonParams for toon controls.  
Pack env strengths into ToonParams.w using 2x8-bit packing.

Suggested packing helpers:
```
float Pack2x8(float a, float b)
{
    a = saturate(a);
    b = saturate(b);
    float2 enc = floor(float2(a, b) * 255.0f + 0.5f);
    return (enc.x + enc.y * 256.0f) / 65535.0f;
}

float2 Unpack2x8(float v)
{
    float raw = saturate(v) * 65535.0f;
    float hi = floor(raw / 256.0f);
    float lo = raw - hi * 256.0f;
    return float2(lo, hi) / 255.0f;
}
```

ToonParams layout for ToonPBR:
- `x`: toonStrength/blur packed (existing)
- `y`: pack(level1, level2)
- `z`: level3
- `w`: pack(envDiffuse, envSpecular)

### 3.3 Light pass decode
If ToonPBR:
- Unpack `ToonParams.y` back into level1/level2
- `ToonParams.z` = level3
- Unpack `ToonParams.w` for env strengths

If not ToonPBR:
- `ToonParams.x/y` are env strengths directly

Apply env strengths to IBL in light pass:
```
diffuseIBL  *= envDiffuseStrength;
specularIBL *= envSpecularStrength;
```

---

## 4) Skybox Resolution + Persistence

### Runtime state
Add:
```
int m_skyboxResolution = 0; // 0 HDR, 1 MDR
```

### Settings persistence
Store in EngineSettings.json:
```
skybox.resolution = 0 or 1
```
Load this at startup and use it when creating IBL resources.

### UI (LightingPanel)
- Provide a combo for Skybox Resolution:
  - `HDR` (suffix = "HDR")
  - `MDR` (suffix = "MDR")
- Optional: show actual DDS size by reading `EnvHDR.dds` / `EnvMDR.dds`.

### Resource loading
When applying skybox:
```
suffix = (resolution == 1) ? "MDR" : "HDR";
SetIblSet(folder, prefix, suffix);
```
Fallback to HDR if MDR files are missing.

---

## 5) Skybox Off Behavior

When skybox is OFF:
- Clear skybox SRV
- Clear IBL SRVs (diffuse/specular/BRDF LUT)
- This must happen in both forward and deferred renderers.

If you only flip a bool, the last skybox stays visible.

---

## 6) Integration Checklist

1) Add MaterialComponent fields + reflection.
2) Add CBPerObject fields and align.
3) Forward: pass env strengths + multiply in shader.
4) Deferred:
   - Encode env strengths in GBuffer (non-toon or packed for ToonPBR).
   - Decode in LightPS and apply to IBL.
5) Skybox:
   - Add resolution state + persistence.
   - UI for HDR/MDR.
   - SetIblSet uses suffix.
   - Off clears SRVs.

---

## 7) Validation

- Material env strengths = 0 should remove skybox influence.
- Switching HDR/MDR updates IBL correctly.
- Skybox Off removes background and IBL.
- ToonPBREditable still works (toon params preserved).

---

## 8) Shadow Strength Controls (Global + Per-Material + Toon)

이 섹션은 그림자 “강도”를 조절하는 기능을 정리합니다.
그림자 **품질(해상도/PCF/필터링)** 을 바꾸는 기능이 아니라,
**그림자의 어두움/진함** 을 조절하는 기능입니다.

### 8.1 Global Shadow Strength

LightingPanel에서 전역 그림자 강도를 조절합니다.
- 저장 위치: `EngineSettings.json`
- UI 범위: `0.0 ~ 1.0` (정규화)
- 내부 적용: 더 강한 범위로 매핑해서 실제 영향력을 키웁니다.

Forward/Deferred 공통 적용 방식:
```
const float kShadowStrengthMax = 12.0f; // UI(0~1)를 더 강한 범위로 매핑
shadowStrength = saturate(GlobalShadowStrength) * kShadowStrengthMax;
shadowVis = saturate(lerp(1.0f, shadowVis, shadowStrength));
```
즉, UI의 1.0은 기존보다 더 강하게 그림자를 어둡게 만듭니다.

### 8.2 Per-Material Shadow Intensity (Material)

머티리얼 단위로 **그림자 강도(수광 강도)** 를 조절할 수 있습니다.
- Material: `shadowStrength` (UI 라벨: **Shadow Intensity**, 0~1)
- 전역(Global)과 **곱셈**으로 적용됩니다.

```
shadowStrength = GlobalShadowStrength * MaterialShadowIntensity
shadowVis = lerp(1.0f, shadowVis, shadowStrength)
```

즉,
- Material = 0 → 그 머티리얼은 그림자 영향 없음
- Global = 0 → 씬 전체 그림자 영향 없음

### 8.3 ToonPBREditable 전용 보정

ToonPBREditable(ShadingMode=7)에서는 별도의 보정값이 적용됩니다.
- LightingPanel: `Toon Shadow Strength (Editable)`
- 적용 방식:
```
if (ToonPBREditable)
    shadowStrength *= saturate(ToonShadowStrength);
```
Toon 쪽만 따로 어둡거나 약하게 만들 수 있습니다.

### 8.4 ToonPBREditable Ramp Intensity (검은 영역 완화)

툰 쉐이딩의 **가장 어두운 밴드(검은 영역)** 를 연하게 만드는 전용 파라미터입니다.
- MaterialComponent: `toonPbrRampIntensity` (0~1)
- 0: 기존과 동일 (어두운 밴드 유지)
- 1: 어두운 밴드를 원본 NdotL 쪽으로 당겨 **완화**
- 적용 위치: `ToonStepEditable()` 내부 (Forward/Deferred 동일)
```
// 가장 어두운 밴드(첫 단계)만 보정
alphaAdj = alpha * (1.0f - toonPbrRampIntensity * darkMask);
return lerp(NdotL, level, strength * alphaAdj);
```

### 8.5 Shadow Quality (맵 해상도 / PCF 반경)

그림자의 선명도는 **섀도우맵 해상도**와 **PCF 반경**으로 결정됩니다.

기본값 (RenderTypes::ShadowSettings):
```
mapSizePx = 4096;
pcfRadius = 0.5f; // texel 단위
```

Deferred의 기본 스케일:
```
m_shadowResolutionScale = 1; // 1: 원본, 2: 1/2
```

선명하게: `mapSizePx` ↑, `pcfRadius` ↓  
부드럽게: `mapSizePx` ↓, `pcfRadius` ↑

### 8.6 요약

- 전역 값: 씬 전체 그림자의 기본 강도
- 머티리얼 값: 전역 값과 곱해지는 **Shadow Intensity**
- Toon 전용 값: ToonPBREditable에만 추가 보정
- Ramp Intensity: ToonPBREditable의 **검은 밴드만 완화**

---

## 9) 최신 운영 정책 메모

### 9.1 Tone Mapping UI 정책

- LightingPanel에서 Tone Mapping 슬라이더를 직접 조정하지 않습니다.
- 안내 문구를 통해 PostProcessVolume(Unbound) 조정을 유도합니다.
- 즉, 톤매핑/컬러그레이딩은 PPV 기준으로 통합 운용합니다.

### 9.2 Outline 정책

- Deferred 기준 아웃라인은 Edge Detection(Sobel) 방식입니다.
- 머티리얼별 `outlineColor`, `outlineWidth`를 사용합니다.
- 상세 구현/트러블슈팅은 `RENDERING_LATEST_AND_OUTLINE.md`를 참고합니다.

