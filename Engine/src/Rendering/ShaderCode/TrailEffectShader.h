#pragma once

// TrailEffectShader header
namespace Alice
{
	class TrailEffectShader {
	public:
		inline static const char* g_TrailEffectVS = R"(
cbuffer CBPerTrailEffectVS : register(b0)
{
    float4x4 gViewProj;     // View * Projection 행렬
    float3   gCameraPos;    // 카메라 위치 (월드 좌표)
    float    padding0;
    float    gCurrentTime;
    float    gFadeDuration;
    float    gWidth;        // 트레일의 기본 폭
    float    padding1;
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float  BirthTime : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float2 TexCoord : TEXCOORD0;
    float  Age : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // 버텍스 위치는 이미 월드 좌표로 저장되어 있음
    float4 worldPos = float4(input.Position, 1.0f);
    output.WorldPos = worldPos.xyz;
    
    // World → View → Projection 연쇄 변환
    output.Position = mul(worldPos, gViewProj);
    
    // Age 기반 계산
    float age = gCurrentTime - input.BirthTime;
    output.Age = age;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        inline static const char* g_TrailEffectPS = R"(
Texture2D gSwordTexture : register(t20);
SamplerState gSwordSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float2 TexCoord : TEXCOORD0;
    float  Age : TEXCOORD2;
};

cbuffer CBPerTrailEffectPS : register(b1)
{
    float3   gColor;
    float    gFadeDuration;
    float    gWidth;    // 트레일의 기본 폭
    float    padding0;
    float    padding1;
    float    padding2;
};

float4 main(PSInput input) : SV_TARGET
{
    // 텍스처 샘플링
    float4 texColor = gSwordTexture.Sample(gSwordSampler, input.TexCoord);
    
    // Age 기반 알파 계산 (서서히 사라짐)
    float alpha = 1.0f - saturate(input.Age / gFadeDuration);
    
    // Age 기반 폭 감소 (점차 줄어들면서 사라짐)
    float widthFactor = 1.0f - saturate(input.Age / gFadeDuration);
    
    // 최종 색상 (폭 감소를 알파에 반영)
    float finalAlpha = alpha * widthFactor;
    
    return float4(texColor.rgb * gColor, texColor.a * finalAlpha);
}
)";
	};
}
