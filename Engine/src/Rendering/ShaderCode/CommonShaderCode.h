#pragma once

namespace Alice
{
    /// 포워드/디퍼드 양쪽에서 공통으로 사용되는 셰이더 코드
    class CommonShaderCode
    {
    public:
        // Quad Vertex Shader (FullScreen) - 톤매핑/포스트프로세스용
        inline static const char* QuadVS = R"(
struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.TexCoord = input.TexCoord;
    return output;
}
)";

        // Skybox Vertex Shader
        inline static const char* SkyboxVS = R"(
cbuffer CBSkybox : register(b0)
{
    float4x4 gWorldViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Direction = input.Position;
    float4 posH = mul(float4(input.Position, 1.0f), gWorldViewProj);
    o.Position = posH.xyww;
    return o;
}
)";

        // Skybox Pixel Shader
        inline static const char* SkyboxPS = R"(
TextureCube g_TexCube : register(t0);
SamplerState g_Sam : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return g_TexCube.Sample(g_Sam, input.Direction);
}
)";

        // Tone Mapping Pixel Shader (LDR/Standard ACES)
        inline static const char* ToneMappingPS_LDR = R"(
Texture2D g_SceneHDR : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer PostProcessConstantBuffer : register(b2)
{
    float g_Exposure;
    float g_MaxHDRNits;
    float2 padding;

    float4 g_ColorGradingSaturation;  // Color Grading: 채도 (R,G,B 채널별, 0.0 = 흑백, 1.0 = 원본, 2.0 = 과포화, W=1.0)
    float4 g_ColorGradingContrast;   // Color Grading Contrast: 룩 조절 (R,G,B 채널별, 0.0 = 저대비, 1.0 = 원본, 2.0 = 고대비, W=1.0)
                                      // Pivot 기반 대비 적용 (Pivot = 0.5). 출력 감마 보정과 분리된 룩 조절 파라미터입니다.
    float4 g_ColorGradingGamma;      // Color Grading Gamma: 룩/중간톤 조절 (R,G,B 채널별, 0.1~3.0, 1.0 = 원본, W=1.0)
                                      // 주의: 이것은 "출력 감마 보정"이 아니라 luminance에 영향을 주는 color grading 파라미터입니다.
    float4 g_ColorGradingGain;      // Color Grading Gain: Multiply 스케일 (R,G,B 채널별, 0.0 = 검정, 1.0 = 원본, >1.0 = 밝게, W=1.0)
                                      // 주의: 이것은 "출력 감마 보정"이 아니라 Color Grading 단계에서 색상을 곱하는 룩 조절 파라미터입니다.
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Linear to sRGB (Output Gamma Correction - 디스플레이 변환)
// 주의: 이것은 고정된 출력 감마 보정이며, Color Grading과 분리되어 있습니다.
float3 LinearToSRGB(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

// Color Grading 함수들 (룩 조절용)
float3 ApplySaturation(float3 color, float3 saturation)
{
    // Luminance 계산 (Rec.709 가중치)
    float3 luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    // Saturation 적용: 0.0 = 흑백, 1.0 = 원본, >1.0 = 과포화
    return lerp(float3(luminance.x, luminance.y, luminance.z), color, saturation);
}

// Color Grading Contrast: 룩 조절 (출력 감마 보정이 아님)
// Pivot 기반 대비 적용 (Unreal Engine 스타일)
// 1.0 = 원본, <1.0 = 저대비, >1.0 = 고대비
float3 ApplyContrast(float3 color, float3 contrastColor)
{
    // Pivot (중간 기준점): 0.5 (Unreal Engine 기본값)
    float3 pivot = float3(0.5f, 0.5f, 0.5f);
    
    // 채널별 대비 적용: (color - pivot) * contrast + pivot
    float3 result = (color - pivot) * contrastColor + pivot;
    
    // Tone Mapping 이후 단계이므로 [0,1] 범위로 클램프
    return saturate(result);
}

// Color Grading Gamma: 룩/중간톤 조절 (출력 감마 보정이 아님)
// luminance에 영향을 주는 color grading 파라미터로 사용됩니다.
// 1.0 = 원본, <1.0 = 중간톤 밝게 (lift), >1.0 = 중간톤 어둡게 (lower)
float3 ApplyColorGradingGamma(float3 color, float3 gammaColor)
{
    // 각 채널별로 gamma curve 적용 (luminance에 영향을 주는 룩 조절)
    // pow(color, 1/gamma) 형태로 중간톤 휘도 커브를 조절
    float3 invGamma = float3(1.0f / max(gammaColor.x, 0.1f), 1.0f / max(gammaColor.y, 0.1f), 1.0f / max(gammaColor.z, 0.1f));
    return pow(max(color, 0.0f), invGamma);
}

  float3 ApplyColorGradingGain(float3 color, float3 gainColor)
  {
      return color * gainColor;
  }

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    
    // 1. Exposure 적용
    float exposureFactor = pow(2.0f, g_Exposure);
    C_linear709 *= exposureFactor;
    
    // 2. Tone Mapping
    float3 C_tonemapped = ACESFilm(C_linear709);
    
    // 3. Color Grading (Tone Mapping 이후 적용, RGB 채널별 제어)
    //    주의: 이것은 "룩 조절"이며, 최종 출력 감마 보정과 분리되어 있습니다.
    //    적용 순서: Saturation → Contrast → Gamma → Gain (multiply 스케일)
    C_tonemapped = ApplySaturation(C_tonemapped, g_ColorGradingSaturation.rgb);
    C_tonemapped = ApplyContrast(C_tonemapped, g_ColorGradingContrast.rgb);
    C_tonemapped = ApplyColorGradingGamma(C_tonemapped, g_ColorGradingGamma.rgb);
    C_tonemapped = ApplyColorGradingGain(C_tonemapped, g_ColorGradingGain.rgb);
    
    // 4. Output Gamma Correction (디스플레이 변환 - 고정된 경로)
    //    sRGB 변환: Linear → sRGB (고정된 1/2.2 gamma)
    //    주의: 이것은 Color Grading과 분리된 최종 출력 변환입니다.
    float3 C_final = LinearToSRGB(C_tonemapped);
    return float4(C_final, 1.0f);
}
)";

		// Tone Mapping Pixel Shader - HDR (포워드 전용)
		inline static const char* ToneMappingPS_HDR = R"(
Texture2D g_SceneHDR : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer PostProcessConstantBuffer : register(b2)
{
    float g_Exposure;
    float g_MaxHDRNits;
    float2 padding;

    float4 g_ColorGradingSaturation;  // Color Grading: 채도 (R,G,B 채널별, 0.0 = 흑백, 1.0 = 원본, 2.0 = 과포화, W=1.0)
    float4 g_ColorGradingContrast;   // Color Grading Contrast: 룩 조절 (R,G,B 채널별, 0.0 = 저대비, 1.0 = 원본, 2.0 = 고대비, W=1.0)
                                      // Pivot 기반 대비 적용 (Pivot = 0.5). 출력 감마 보정과 분리된 룩 조절 파라미터입니다.
    float4 g_ColorGradingGamma;      // Color Grading Gamma: 룩/중간톤 조절 (R,G,B 채널별, 0.1~3.0, 1.0 = 원본, W=1.0)
                                      // 주의: 이것은 "출력 감마 보정"이 아니라 luminance에 영향을 주는 color grading 파라미터입니다.
    float4 g_ColorGradingGain;      // Color Grading Gain: Multiply 스케일 (R,G,B 채널별, 0.0 = 검정, 1.0 = 원본, >1.0 = 밝게, W=1.0)
                                      // 주의: 이것은 "출력 감마 보정"이 아니라 Color Grading 단계에서 색상을 곱하는 룩 조절 파라미터입니다.
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Color Grading 함수들 (RGB 채널별 제어 - 룩 조절용)
float3 ApplySaturation(float3 color, float3 saturation)
{
    // Luminance 계산 (Rec.709 가중치)
    float3 luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    // Saturation 적용: 각 채널별로 적용 (0.0 = 흑백, 1.0 = 원본, >1.0 = 과포화)
    return lerp(float3(luminance, luminance, luminance), color, saturation);
}

// Color Grading Contrast: 룩 조절 (출력 감마 보정이 아님)
// Pivot 기반 대비 적용 (Unreal Engine 스타일)
// 1.0 = 원본, <1.0 = 저대비, >1.0 = 고대비
float3 ApplyContrast(float3 color, float3 contrastColor)
{
    // Pivot (중간 기준점): 0.5 (Unreal Engine 기본값)
    float3 pivot = float3(0.5f, 0.5f, 0.5f);
    
    // 채널별 대비 적용: (color - pivot) * contrast + pivot
    float3 result = (color - pivot) * contrastColor + pivot;
    
    // Tone Mapping 이후 단계이므로 [0,1] 범위로 클램프
    return saturate(result);
}

// Color Grading Gamma: 룩/중간톤 조절 (출력 감마 보정이 아님)
// luminance에 영향을 주는 color grading 파라미터로 사용됩니다.
// 1.0 = 원본, <1.0 = 중간톤 밝게 (lift), >1.0 = 중간톤 어둡게 (lower)
float3 ApplyColorGradingGamma(float3 color, float3 gammaColor)
{
    // 각 채널별로 gamma curve 적용 (luminance에 영향을 주는 룩 조절)
    // pow(color, 1/gamma) 형태로 중간톤 휘도 커브를 조절
    float3 invGamma = float3(1.0f / max(gammaColor.x, 0.1f), 1.0f / max(gammaColor.y, 0.1f), 1.0f / max(gammaColor.z, 0.1f));
    return pow(max(color, 0.0f), invGamma);
}

// Color Grading Gain: Multiply 스케일 (출력 감마 보정이 아님)
// Color Grading 단계에서 색상을 곱하는 룩 조절 파라미터입니다.
// 1.0 = 원본, <1.0 = 어둡게, >1.0 = 밝게
float3 ApplyColorGradingGain(float3 color, float3 gainColor)
{
    // 각 채널별로 multiply 적용 (색상 스케일링)
    return color * gainColor;
}

// Rec709 to Rec2020 색공간 변환
float3 Rec709ToRec2020(float3 color)
{
    static const float3x3 conversion =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(color, conversion);
}

// Linear to ST2084 (PQ 인코딩)
float3 LinearToST2084(float3 color)
{
    // g_MaxHDRNits를 반영하여 HDR 스케일링 (10000 nits 기준으로 정규화)
    const float st2084max = 10000.0;
    float hdrScalar = g_MaxHDRNits / st2084max;
    float3 scaledColor = color * hdrScalar;
    
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(scaledColor), m1);
    return pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
}

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 예제 프로젝트 36_ToneMappingPS_HDR.hlsl와 동일한 로직
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    
    // 1. Exposure 적용
    float3 C_exposure = C_linear709 * pow(2.0f, g_Exposure);
    
    // 2. Tone Mapping
    float3 C_tonemapped = ACESFilm(C_exposure);
    
    // 3. Color Grading (Tone Mapping 이후, 색공간 변환 전 적용, RGB 채널별 제어)
    //    주의: 이것은 "룩 조절"이며, 최종 출력 감마 보정과 분리되어 있습니다.
    //    적용 순서: Saturation → Contrast → Gamma → Gain (multiply 스케일)
    C_tonemapped = ApplySaturation(C_tonemapped, g_ColorGradingSaturation.rgb);
    C_tonemapped = ApplyContrast(C_tonemapped, g_ColorGradingContrast.rgb);
    C_tonemapped = ApplyColorGradingGamma(C_tonemapped, g_ColorGradingGamma.rgb);
    C_tonemapped = ApplyColorGradingGain(C_tonemapped, g_ColorGradingGain.rgb);
    
    // 4. Output Transform (디스플레이 변환 - 고정된 경로)
    //    Rec709 → Rec2020 색공간 변환 후 ST2084 (PQ) 인코딩
    //    주의: 이것은 Color Grading과 분리된 최종 출력 변환입니다.
    float3 C_Rec2020 = Rec709ToRec2020(C_tonemapped);
    float3 C_ST2084 = LinearToST2084(C_Rec2020);
    
    // 최종 PQ 인코딩된 값 [0.0, 1.0]을 R10G10B10A2_UNORM 백버퍼에 출력
    return float4(C_ST2084, 1.0);
}
)";

        // Bloom Bright Pass Pixel Shader
        inline static const char* BloomBrightPassPS = R"(
Texture2D g_SceneHDR : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 color = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    
    // Luminance 계산
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    
    // Soft threshold 적용
    float softThreshold = g_Threshold - g_Knee;
    float contribution = max(luminance - softThreshold, 0.0f);
    contribution = contribution / (g_Knee * 2.0f + contribution);
    
    float3 result = color * contribution;
    return float4(result, 1.0f);
}
)";

        // Bloom Blur Pass Pixel Shader (Horizontal)
        inline static const char* BloomBlurPassPS_H = R"(
Texture2D g_BloomInput : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    
    // Gaussian blur (9-tap)
    float offsets[9] = { -4.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float weights[9] = { 0.01621622f, 0.05405405f, 0.12162162f, 0.19459459f, 0.22702703f,
                         0.19459459f, 0.12162162f, 0.05405405f, 0.01621622f };
    
    for (int i = 0; i < 9; ++i)
    {
        float2 uvOffset = float2(offsets[i] * g_TexelSize.x * g_GaussianIntensity, 0.0f);
        float3 sampleColor = g_BloomInput.Sample(g_SamplerLinear, input.uv + uvOffset).rgb;
        color += sampleColor * weights[i];
        weightSum += weights[i];
    }
    
    // Unreal Engine 스타일: Gaussian 블러 결과에 Gaussian Intensity 적용
    float3 blurredColor = (color / weightSum);
    return float4(blurredColor, 1.0f);
}
)";

        // Bloom Blur Pass Pixel Shader (Vertical)
        inline static const char* BloomBlurPassPS_V = R"(
Texture2D g_BloomInput : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    
    // Gaussian blur (9-tap)
    float offsets[9] = { -4.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float weights[9] = { 0.01621622f, 0.05405405f, 0.12162162f, 0.19459459f, 0.22702703f,
                         0.19459459f, 0.12162162f, 0.05405405f, 0.01621622f };
    
    for (int i = 0; i < 9; ++i)
    {
        float2 uvOffset = float2(0.0f, offsets[i] * g_TexelSize.y * g_GaussianIntensity);
        float3 sampleColor = g_BloomInput.Sample(g_SamplerLinear, input.uv + uvOffset).rgb;
        color += sampleColor * weights[i];
        weightSum += weights[i];
    }
    
    // Unreal Engine 스타일: Gaussian 블러 결과에 Gaussian Intensity 적용
    float3 blurredColor = (color / weightSum) ;
    return float4(blurredColor, 1.0f);
}
)";

        // Bloom Downsample Pixel Shader (2x2 다운샘플링)
        inline static const char* BloomDownsamplePS = R"(
Texture2D g_BloomInput : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 2x2 다운샘플링: 4개 픽셀의 평균을 사용
    // g_TexelSize는 입력 텍스처의 텍셀 크기 (다운샘플링 전 크기)
    float2 offsets[4] = {
        float2(-0.5f, -0.5f) * g_TexelSize,
        float2(0.5f, -0.5f) * g_TexelSize,
        float2(-0.5f, 0.5f) * g_TexelSize,
        float2(0.5f, 0.5f) * g_TexelSize
    };
    
    float3 color = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
        color += g_BloomInput.Sample(g_SamplerLinear, input.uv + offsets[i]).rgb;
    }
    color /= 4.0f;
    
    return float4(color, 1.0f);
}
)";

        // Bloom Upsample Pixel Shader (업샘플링만 수행, Additive Blending은 OM State로 처리)
        inline static const char* BloomUpsamplePS = R"(
Texture2D g_BloomLowRes : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 저해상도 텍스처를 업샘플링 (bilinear 보간으로 자동 확대)
    // Additive Blending은 OM State에서 처리되므로, 여기서는 저해상도 값만 반환
    float3 lowRes = g_BloomLowRes.Sample(g_SamplerLinear, input.uv).rgb;
    
    return float4(lowRes, 1.0f);
}
)";

        // Bloom Composite Pixel Shader (Scene + Bloom 합성 + ToneMapping)
        inline static const char* BloomCompositePS = R"(
Texture2D g_SceneHDR : register(t0);
Texture2D g_Bloom : register(t1);
SamplerState g_SamplerLinear : register(s0);

cbuffer BloomConstantBuffer : register(b3)
{
    float g_Threshold;
    float g_Knee;
    float g_BloomIntensity;      // Bloom 합성 강도 (Composite 패스에서 사용)
    float g_GaussianIntensity;   // Gaussian 블러 강도 (Blur 패스에서 사용)
    float g_Radius;
    float2 g_TexelSize;
    int g_Downsample;
    float g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    float3 sceneColor = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    
    // Bloom 텍스처는 다운샘플링되어 있으므로 원본 UV 사용 (다운샘플링된 텍스처는 자동으로 보간됨)
    float3 bloomColor = g_Bloom.Sample(g_SamplerLinear, input.uv).rgb;
    
    // HDR 합성만 수행 (ToneMapping은 별도 패스에서 수행)
    // Bloom Intensity는 최종 합성 단계에서 적용
    float3 combined = sceneColor + bloomColor * g_BloomIntensity;
    
    return float4(combined, 1.0f);
}
)";

        // Particle Overlay Pixel Shader (Additive blending)
        inline static const char* ParticleOverlayPS = R"(
Texture2D g_ParticleTexture : register(t0);
SamplerState g_SamplerLinear : register(s0);

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT_QUAD input) : SV_TARGET
{
    float4 particleColor = g_ParticleTexture.Sample(g_SamplerLinear, input.uv);
    // CS에서 이미 premultiplied alpha로 저장하므로 알파 곱 제거
    return float4(particleColor.rgb, particleColor.a);
}
)";
    };
}
