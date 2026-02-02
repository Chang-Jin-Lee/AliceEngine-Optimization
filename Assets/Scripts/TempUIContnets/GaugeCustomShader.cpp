#include "GaugeCustomShader.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    REGISTER_SCRIPT(GaugeCustomShader);

    // 게이지용 커스텀 픽셀 쉐이더 소스 코드
    // 이 쉐이더는 UIRenderer::RegisterShader를 통해 등록되어야 합니다
    // 엔진 초기화 시점이나 적절한 시점에 등록해야 합니다
    const char* GaugeCustomPixelShader = R"(
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
    float2 uv = input.TexCoord;
    float4 tex = gTexture.Sample(gSampler, uv);
    float4 color = tex * input.Color;
    
    // 알파값이 0.1 이하면 discard
    if (color.a < 0.1)
        discard;
    
    // RGB 값이 전부 0.1 이하면 return (투명하게)
    if (color.r <= 0.1 && color.g <= 0.1 && color.b <= 0.1)
        return float4(0, 0, 0, 0);
    
    // 게이지 정보는 gParams0에 저장됨 (BuildPixelConstants에서 설정)
    // gParams0.x = fill ratio (0~1)
    // gParams0.y = fillLate ratio (0~1, useFillLate가 true일 때만)
    // gParams0.z = useFillLate flag (1.0 = true, 0.0 = false)
    // gParams0.w = direction flag (0.0 = LeftToRight, 1.0 = RightToLeft, 2.0 = BottomToTop, 3.0 = TopToBottom)
    
    float fillRatio = saturate(gParams0.x);
    float fillLateRatio = saturate(gParams0.y);
    float useFillLate = gParams0.z;
    float direction = gParams0.w;
    
    // UV 좌표를 사용해서 현재 픽셀이 어느 영역에 있는지 판단
    float pixelRatio = 0.0f;
    
    if (direction < 0.5f) // LeftToRight
    {
        pixelRatio = uv.x;
    }
    else if (direction < 1.5f) // RightToLeft
    {
        pixelRatio = 1.0f - uv.x;
    }
    else if (direction < 2.5f) // BottomToTop
    {
        pixelRatio = 1.0f - uv.y;
    }
    else // TopToBottom
    {
        pixelRatio = uv.y;
    }
    
    // 색상 우선순위: 빨강 > 노랑 > 흰색
    float4 finalColor = float4(1, 1, 1, 1); // 기본: 흰색 (배경)
    
    // FillLate 영역 (노란색) - fillRatio보다 크고 fillLateRatio 이하
    if (useFillLate > 0.5f && pixelRatio > fillRatio && pixelRatio <= fillLateRatio)
    {
        finalColor = float4(1, 1, 0, 1); // 노란색
    }
    
    // Fill 영역 (빨간색) - fillRatio 이하 (우선순위가 가장 높으므로 마지막에 체크)
    if (pixelRatio <= fillRatio)
    {
        finalColor = float4(1, 0, 0, 1); // 빨간색
    }
    
    // 원본 텍스처의 알파를 유지
    finalColor.a = color.a;
    
    // Global alpha 적용
    finalColor.a *= gTime.y;
    
    return finalColor;
}
)";

    void GaugeCustomShader::Start()
    {
        ALICE_LOG_INFO("[GaugeCustomShader] Custom gauge shader code is ready.");
        ALICE_LOG_INFO("[GaugeCustomShader] To use this shader, register it using UIRenderer::RegisterShader(\"GaugeCustom\", GaugeCustomPixelShader)");
        ALICE_LOG_INFO("[GaugeCustomShader] Then set UIWidgetComponent::shaderName = \"GaugeCustom\" on your gauge widget");
    }

    void GaugeCustomShader::Update(float deltaTime)
    {
        (void)deltaTime;
    }
}
