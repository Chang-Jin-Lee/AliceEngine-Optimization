#include "Runtime/UI/UIShaderCode.h"

namespace Alice
{
	namespace AliceUIShader
	{
		const char* UIVS = R"(
cbuffer UIConstants : register(b0)
{
    float4x4 gViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Position = mul(float4(input.Position, 1.0f), gViewProj);
    o.TexCoord = input.TexCoord;
    o.Color = input.Color;
    return o;
}
)";

		const char* UIPixelPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer UIPixelConstants : register(b1)
{
    float4 gOutlineColor;
    float4 gGlowColor;
    float4 gVitalColor;
    float4 gVitalBgColor;
    float4 gParams0;
    float4 gParams1;
    float4 gParams2;
    float4 gParams3;
    float4 gParams4;
    float4 gParams5;
    float4 gGaugeParams;
    float4 gGaugeParams2;
    float4 gEmptyColor;
    float4 gEmptyParams;
    float4 gPencilParams;
    float4 gTime;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float ComputeOutline(float2 uv)
{
    if (gParams0.y < 0.5f)
        return 0.0f;

    uint w, h;
    gTexture.GetDimensions(w, h);
    float2 texel = 1.0f / float2(max(w, 1u), max(h, 1u));
    float thickness = max(gParams0.x, 0.0f);
    float2 o = texel * thickness;

    float a = gTexture.Sample(gSampler, uv).a;
    float m = 0.0f;
    m = max(m, gTexture.Sample(gSampler, uv + float2( o.x, 0.0f)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(-o.x, 0.0f)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(0.0f,  o.y)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(0.0f, -o.y)).a);

    return saturate(m - a);
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.TexCoord;
    float4 tex = gTexture.Sample(gSampler, uv);
    float4 color = tex * input.Color;

    // Vital sign graph
    if (gParams4.x > 0.5f)
    {
        float amp = gParams4.y;
        float freq = gParams4.z;
        float speed = gParams4.w;
        float thickness = gParams5.x;
        float wave = sin((uv.x * freq * 6.2831853f) + gTime.x * speed) * amp;
        float y = 0.5f + wave;
        float dist = abs(uv.y - y);
        float lineMask = smoothstep(thickness, 0.0f, dist);
        color = lerp(gVitalBgColor, gVitalColor, lineMask);
    }

    // Radial cooldown / mask
    if (gParams0.z > 0.5f)
    {
        float2 d = uv - 0.5f;
        float r = length(d);
        float inner = gParams1.x;
        float outer = gParams1.y;
        float soft = max(gParams1.z, 0.0001f);
        float ring = smoothstep(inner, inner + soft, r) * (1.0f - smoothstep(outer - soft, outer, r));

        float ang = atan2(d.y, d.x) + 1.5707963f;
        if (ang < 0.0f) ang += 6.2831853f;
        float ang01 = ang / 6.2831853f;
        float offset = gParams2.x / 6.2831853f;
        ang01 = frac(ang01 + offset);

        float fill = saturate(gParams0.w);
        float cw = gParams1.w;
        float mask = (cw > 0.5f) ? step(ang01, fill) : step(1.0f - ang01, fill);

        float dim = saturate(gParams2.y);
        color.rgb *= lerp(dim, 1.0f, mask);
        color.a *= ring;
    }

    // Outline
    float outline = ComputeOutline(uv);
    if (outline > 0.0f)
    {
        float4 o = gOutlineColor;
        o.a *= outline;
        color = lerp(o, color, saturate(tex.a));
    }

    // Glow sweep
    if (gParams2.z > 0.5f)
    {
        float angle = gParams3.z;
        float2 dir = float2(cos(angle), sin(angle));
        float phase = dot(uv - 0.5f, dir) + gTime.x * gParams3.y;
        float band = abs(frac(phase) - 0.5f) * 2.0f;
        float width = max(gParams3.x, 0.001f);
        float glow = smoothstep(width, 0.0f, band);
        color.rgb += gGlowColor.rgb * gParams2.w * glow;
    }

    // Grayscale
    if (gParams3.w > 0.0f)
    {
        float lum = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
        color.rgb = lerp(color.rgb, lum.xxx, saturate(gParams3.w));
    }

    // Global alpha
    color.a *= gTime.y;

    return color;
}
)";

		const char* UIGrayPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = gTexture.Sample(gSampler, input.TexCoord);
    float lum = dot(tex.rgb, float3(0.299f, 0.587f, 0.114f));
    float4 gray = float4(lum, lum, lum, tex.a);
		return gray * input.Color;
}
)";

        const char* UIGaugeCustomPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer UIPixelConstants : register(b1)
{
    float4 gOutlineColor;
    float4 gGlowColor;
    float4 gVitalColor;
    float4 gVitalBgColor;
    float4 gParams0; // gParams0.x 를 CellScale로 사용 가능
    float4 gParams1;
    float4 gParams2;
    float4 gParams3;
    float4 gParams4;
    float4 gParams5;
    float4 gGaugeParams;
    float4 gGaugeParams2;
    float4 gEmptyColor;
    float4 gEmptyParams; // x: 사용여부, y: 스케일, z: 속도, w: 강도
    float4 gPencilParams;
    float4 gTime;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

// --- 랜덤 및 노이즈 함수 추가 ---
float random(float2 uv) {
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float noise(float2 uv) {
    float2 i = floor(uv);
    float2 f = frac(uv);
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(float2 uv) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(uv);
        uv *= 2.0;
        a *= 0.5;
    }
    return v;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.TexCoord;
    float4 tex = gTexture.Sample(gSampler, uv);
    float4 color = tex * input.Color;
    
    // 1. 기본 투명도 체크 (완전 투명한 부분은 연산 제외)
    if (color.a < 0.1) discard;

    float fillRatio = saturate(gGaugeParams.x);
    float fillLateRatio = saturate(gGaugeParams.y);
    float useFillLate = gGaugeParams.z;
    float direction = gGaugeParams.w;

    float pixelRatio = 0.0f;
    bool isGauge = (direction >= 0.0f && direction <= 3.0f);
    
    if (direction < 0.5f) pixelRatio = uv.x;
    else if (direction < 1.5f) pixelRatio = 1.0f - uv.x;
    else if (direction < 2.5f) pixelRatio = 1.0f - uv.y;
    else pixelRatio = uv.y;

    float filledRatio = (useFillLate > 0.5f) ? max(fillRatio, fillLateRatio) : fillRatio;
    
    // 기본 색상을 텍스처 컬러로 설정
    float4 finalColor = color;

    // 2. 비어 있는 영역(Empty Area) 처리
    if (isGauge && gEmptyParams.x > 0.5f && pixelRatio <= filledRatio)
    {
        float scale = max(gEmptyParams.y, 0.1f);
        float speed = gTime.x * gEmptyParams.z;
        float aspect = max(gGaugeParams2.x, 0.0001f);
        float2 aspectScale = (aspect >= 1.0f)
            ? float2(aspect, 1.0f)
            : float2(1.0f, 1.0f / aspect);
        float2 noiseUV = (uv * aspectScale) * scale + float2(speed, speed * 0.2f);
        
        float cloud = fbm(noiseUV);
        float intensity = gEmptyParams.w;
        float shade = lerp(1.0f - intensity, 1.0f, cloud);
        
        finalColor = gEmptyColor * shade;
        finalColor.a = color.a; // 원본 알파 유지
    }
    
    // --- [수정 포인트] ---
    // gTime.y가 낮아질수록 finalColor의 RGB를 0(검은색)으로 수렴하게 만듭니다.
    // 알파값은 그대로 1.0(불투명)을 유지하거나 원본을 유지하여 배경이 비치지 않게 합니다.
    
    float darkenFactor = saturate(gTime.y); // 0이면 검정, 1이면 원래색
    finalColor.rgb *= darkenFactor; 
    
    // 만약 "완전히 불투명한 검정"을 원하시면 아래처럼 설정하세요.
    // 배경이 투명해야 한다면 그대로 finalColor.a *= gTime.y; 를 쓰셔도 됩니다.
    finalColor.a = color.a; 
    
    return finalColor;
}
)";

		const char* UIPencilPS = R"(
Texture2D gTexture : register(t0);
Texture2D g_PencilTexture : register(t1);
SamplerState gSampler : register(s0);

cbuffer UIPixelConstants : register(b1)
{
    float4 gOutlineColor;
    float4 gGlowColor;
    float4 gVitalColor;
    float4 gVitalBgColor;
    float4 gParams0;
    float4 gParams1;
    float4 gParams2;
    float4 gParams3;
    float4 gParams4;
    float4 gParams5;
    float4 gGaugeParams;
    float4 gGaugeParams2;
    float4 gEmptyColor;
    float4 gEmptyParams; // x: 사용여부, y: 스케일, z: 속도, w: 강도
    float4 gPencilParams; // x: 타일 스케일, y: Jitter 강도, z: 대비, w: (예비)
    float4 gTime;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    // 1. 기본 색상 및 밝기 계산
    float4 color = gTexture.Sample(gSampler, input.TexCoord);
    color = color * input.Color;
    
    // 알파값이 0.1 이하면 discard
    if (color.a < 0.1)
        discard;
    
    float brightness = dot(color.rgb, float3(0.299, 0.587, 0.114));

    // 2. 우둘투둘한 느낌을 위한 UV 왜곡 (Jitter)
    // 시간에 따라 미세하게 변하는 노이즈를 섞으면 애니메이션 느낌이 납니다.
    float jitterStrength = gPencilParams.y;
    float2 jitterUV = input.TexCoord + (sin(input.TexCoord.y * 100.0) * jitterStrength);
    
    // 3. 연필 질감 샘플링
    // 밝기에 따라 연필 질감의 강도를 조절합니다.
    float tileScale = gPencilParams.x;
    float4 pencilNoise = g_PencilTexture.Sample(gSampler, jitterUV * tileScale); // 타일링 크게
    
    // 4. 합성 로직
    // 밝은 부분은 종이색이 남고, 어두운 부분일수록 연필 자국(pencilNoise)이 진해지도록 합니다.
    float4 finalColor = float4(lerp(pencilNoise.rgb, color.rgb, brightness), color.a);
    
    // 우둘투둘한 느낌을 강조하기 위해 대비(Contrast)를 살짝 높입니다.
    float contrast = gPencilParams.z;
    finalColor.rgb = pow(abs(finalColor.rgb), contrast);

    // Global alpha
    finalColor.a = color.a * gTime.y;

    return finalColor;
}
)";
	}
}





