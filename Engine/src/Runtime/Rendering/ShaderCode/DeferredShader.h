#pragma once

namespace Alice
{
    /// 디퍼드 렌더링 전용 셰이더 코드
    class DeferredShader
    {
    public:
        // G-Buffer Vertex Shader
        inline static const char* GBufferVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    float3 N = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    
    float3 posOffset = float3(0.0f, 0.0f, 0.0f);
    
    float4 posW = mul(float4(input.Position + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal = N;
    
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Instanced Vertex Shader (정적 메시 인스턴싱)
        inline static const char* GBufferInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position   : POSITION;
    float3 Normal     : NORMAL;
    float2 TexCoord   : TEXCOORD0;
    float4 iWorld0    : INSTANCE_WORLD0;
    float4 iWorld1    : INSTANCE_WORLD1;
    float4 iWorld2    : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 인스턴스 월드 행렬 복원 (전치 행렬 기준)
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);
    
    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    
    float3 posOffset = float3(0.0f, 0.0f, 0.0f);
    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal = N;
    
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Skinned Vertex Shader
        inline static const char* GBufferSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));
    
    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    float3 posOffset = float3(0.0f, 0.0f, 0.0f);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Skinned Instanced Vertex Shader (본 없는 FBX 인스턴싱용)
        inline static const char* GBufferSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // 인스턴스 월드 행렬 복원 (마지막 행은 (0,0,0,1))
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float3 N = normalize(mul(float4(input.Normal, 0.0f), world).xyz);

    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    float3 posOffset = float3(0.0f, 0.0f, 0.0f);

    //float4 posW = mul(float4(input.Position + posOffset, 1.0f), world);
    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    //output.TangentW = normalize(mul(float4(input.Tangent, 0.0f), world).xyz);
    //output.BitanW   = normalize(mul(float4(input.Binormal, 0.0f), world).xyz);
    output.TangentW = normalize(mul(world, float4(input.Tangent, 0.0f)).xyz);
    output.BitanW   = normalize(mul(world, float4(input.Binormal, 0.0f)).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // G-Buffer Pixel Shader
        inline static const char* GBufferPS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2 gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

struct GBufferOut
{
    float4 NormalRoughness : SV_Target0;
    float4 Metalness       : SV_Target1;
    float4 BaseColor       : SV_Target2;
    float4 ToonParams      : SV_Target3;
    float4 ToonAlphas      : SV_Target4;
    float4 OutlineData     : SV_Target5;
};

Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);
SamplerState g_Sam : register(s0);

float DitherThreshold(float2 pos)
{
    // Interleaved gradient noise (per-pixel hash, less visible grid)
    float n = 0.06711056f * pos.x + 0.00583715f * pos.y;
    return frac(52.9829189f * frac(n));
}

float Pack2x8(float a, float b)
{
    a = saturate(a);
    b = saturate(b);
    float2 enc = floor(float2(a, b) * 255.0f + 0.5f);
    return (enc.x + enc.y * 256.0f) / 65535.0f;
}

GBufferOut main(VertexOut pIn)
{
    GBufferOut gOut;
    float ao = saturate(gAmbientOcclusion);
    float aoPacked = min(ao, 0.999f);
    float shadingEncoded = ((float)gShadingMode + aoPacked) / 8.0f;

    float4 textureColor = float4(1,1,1,1);
    if (gUseTexture != 0)
    {
        textureColor = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);
    }
    
    float alphaTex = textureColor.a * gMaterialColor.a;
    // 알파 테스트는 텍스처 알파에만 적용 (머티리얼 알파는 블렌딩으로 처리)
    if (gUseTexture != 0)
    {
        clip(textureColor.a - 0.1f);
    }
    // NDC 기반 디더링으로 투명도 처리 (알파 블렌딩 대신 화면 도트 컷아웃)
    float alpha = saturate(alphaTex);
    if (alpha < 1.0f)
    {
        float threshold = DitherThreshold(pIn.Position.xy);
        clip(alpha - threshold);
    }
    
    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
    {
        baseColor *= textureColor.rgb;
    }
    
    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }
    
    float metalness = saturate(gMetalness);
    float roughness = saturate(gRoughness);
    
    // Normal을 [0,1] 범위로 인코딩하여 저장 (LightPS에서 디코딩)
    float3 normalEncoded = N * 0.5f + 0.5f;
    
    gOut.NormalRoughness = float4(normalEncoded, roughness);
    gOut.Metalness  = float4(metalness, saturate(gToonPbrCuts.x), saturate(gToonPbrCuts.y), saturate(gToonPbrCuts.z));
    float toonStrength = saturate(gToonPbrCuts.w);
    float toonBlur = (gToonPbrLevels.w > 0.5f) ? 1.0f : 0.0f;
    float toonStrengthPacked = toonStrength * 0.5f + toonBlur * 0.5f;
    const bool isToonPbr = (gShadingMode == 5 || gShadingMode == 7);
    if (isToonPbr)
    {
        float packedLevels12 = Pack2x8(gToonPbrLevels.x, gToonPbrLevels.y);
        float packedLevel3Ramp = Pack2x8(saturate(gToonPbrLevels.z), saturate(gToonPbrRampIntensity));
        float packedEnv = Pack2x8(gEnvDiffuseStrength, gEnvSpecularStrength);
        gOut.ToonParams = float4(toonStrengthPacked, packedLevels12, packedLevel3Ramp, packedEnv);
    }
    else
    {
        gOut.ToonParams = float4(saturate(gEnvDiffuseStrength), saturate(gEnvSpecularStrength), 0.0f, 0.0f);
    }
    float packedShadowSelf = Pack2x8(gToonPbrAlphas.w, gToonSelfShadowStrength);
    gOut.ToonAlphas = float4(saturate(gToonPbrAlphas.xyz), packedShadowSelf);
    // shadingMode + AO를 [0,1] 범위로 인코딩하여 저장
    gOut.BaseColor  = float4(baseColor, saturate(shadingEncoded));
    gOut.OutlineData = float4(saturate(gOutlineColor), max(gOutlineWidth, 0.0f));
    
    return gOut;
}
)";

        // Decal Pass Vertex Shader
        inline static const char* DecalVS = R"(
cbuffer DecalCB : register(b0)
{
    float4x4 g_DecalWorldViewProj;
    float4x4 g_WorldToDecal;
    float4x4 g_InvViewProj;
    float4   g_ColorOpacity;
    float4   g_UVScaleOffset;
    float2   g_ScreenSize;
    float2   g_Pad;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Position = mul(float4(input.Position, 1.0f), g_DecalWorldViewProj);
    return o;
}
)";

        // Decal Pass Pixel Shader (DBuffer Albedo)
        inline static const char* DecalPS = R"(
cbuffer DecalCB : register(b0)
{
    float4x4 g_DecalWorldViewProj;
    float4x4 g_WorldToDecal;
    float4x4 g_InvViewProj;
    float4   g_ColorOpacity;
    float4   g_UVScaleOffset;
    float2   g_ScreenSize;
    float2   g_Pad;
};

Texture2D g_DecalAlbedo : register(t0);
Texture2D<float> g_SceneDepth : register(t1);
SamplerState g_Sam : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
};

struct DBufferOut
{
    float4 Albedo : SV_Target0;
};

DBufferOut main(PSInput input)
{
    DBufferOut o;

    float2 uv = input.Position.xy / g_ScreenSize;
    float depth = g_SceneDepth.Sample(g_Sam, uv);
    if (depth >= 0.9999f) discard;

    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 posW4 = mul(clip, g_InvViewProj);
    float3 posW = posW4.xyz / max(posW4.w, 1e-6f);

    float3 localPos = mul(float4(posW, 1.0f), g_WorldToDecal).xyz;
    if (abs(localPos.x) > 1.0f || abs(localPos.y) > 1.0f || abs(localPos.z) > 1.0f)
        discard;

    float2 decalUV = localPos.xy * 0.5f + 0.5f;
    decalUV = decalUV * g_UVScaleOffset.xy + g_UVScaleOffset.zw;

    float4 tex = g_DecalAlbedo.Sample(g_Sam, decalUV);
    float alpha = tex.a * g_ColorOpacity.a;
    if (alpha <= 0.001f) discard;

    float3 color = tex.rgb * g_ColorOpacity.rgb;
    o.Albedo = float4(color * alpha, alpha);
    return o;
}
)";

        // Deferred Light Pixel Shader
        inline static const char* LightPS1 = R"(
// PBR 헬퍼 함수들
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

// 임시 느낌
// 거칠기에 따른 프레넬 보정 (화이트아웃/과한 반사 방지)
float3 fresnelSchlickRoughness(float3 F0, float cosTheta, float roughness)
{
    cosTheta = saturate(cosTheta);
    float3 F90 = float3(1.0f, 1.0f, 1.0f);
    float3 fresnel = F0 + (F90 - F0) * pow(1.0f - cosTheta, 5.0f);
    // Roughness가 높을수록 F0(기본 반사율)로 수렴하게 하여 엣지 발광 억제
    float roughSq = roughness * roughness;
    return lerp(fresnel, F0, roughSq);
}

// 호라이즌 스페큘러 오클루전 (비스듬한 각도의 빛샘 방지)
float horizonSpecularOcclusion(float NoV, float roughness)
{
    float NoV_sat = saturate(NoV);
    float t = roughness * roughness;
    return pow(NoV_sat, 1.0f + 3.0f * t);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float ToonLevel(float n)
{
    if (n > 0.95f) return 1.0f;
    if (n > 0.5f)  return 0.7f;
    if (n > 0.2f)  return 0.4f;
    return 0.1f;
}

float ToonStepEditable(float n, float3 cuts, float3 levels, float3 alphas, float strength, float blur, float rampIntensity)
{
    float c1 = saturate(cuts.x);
    float c2 = saturate(cuts.y);
    float c3 = saturate(cuts.z);
    c2 = max(c2, c1 + 1e-4f);
    c3 = max(c3, c2 + 1e-4f);

    float l0 = saturate(levels.x);
    float l1 = saturate(levels.y);
    float l2 = saturate(levels.z);
    float l3 = 1.0f;

    float a0 = saturate(alphas.x);
    float a1 = saturate(alphas.y);
    float a2 = saturate(alphas.z);
    float a3 = 1.0f;

    float t = saturate(strength);
    float ramp = saturate(rampIntensity);
    if (blur > 0.5f)
    {
        float w = max(fwidth(n) * 2.0f, 0.02f);
        float s1 = smoothstep(c1 - w, c1 + w, n);
        float s2 = smoothstep(c2 - w, c2 + w, n);
        float s3 = smoothstep(c3 - w, c3 + w, n);

        float level = lerp(l0, l1, s1);
        level = lerp(level, l2, s2);
        level = lerp(level, l3, s3);
        float alpha = lerp(a0, a1, s1);
        alpha = lerp(alpha, a2, s2);
        alpha = lerp(alpha, a3, s3);
        float darkMask = 1.0f - s1;
        alpha *= (1.0f - ramp * darkMask);
        return lerp(n, level, t * alpha);
    }

    float level = (n > c3) ? l3 :
                  (n > c2) ? l2 :
                  (n > c1) ? l1 :
                             l0;
    float alpha = (n > c3) ? a3 :
                  (n > c2) ? a2 :
                  (n > c1) ? a1 :
                             a0;
    float darkMask = (n > c1) ? 0.0f : 1.0f;
    alpha *= (1.0f - ramp * darkMask);
    return lerp(n, level, t * alpha);
}

float ApplySelfShadowNdotL(float shadedNdotL, float selfShadowStrength)
{
    return lerp(1.0f, shadedNdotL, saturate(selfShadowStrength));
}

float ApplyLocalToonSelfShadow(float ndotl, float shadedNdotL, bool toonEditable, float toonSelfShadowStrength)
{
    if (ndotl <= 0.0f)
    {
        return 0.0f;
    }

    float selfShadowNdotL = ApplySelfShadowNdotL(shadedNdotL, toonSelfShadowStrength);
    if (toonEditable)
    {
        // 로컬 라이트(Point/Spot/Rect)는 Toon SelfShadow를 약하게만 적용한다.
        // 강한 밴딩으로 생기는 지저분한 검은 조각을 막기 위해 원본 ndotl 하한을 보장한다.
        const float kLocalSelfShadowWeight = 0.40f;   // 0.3~0.6 권장 범위
        const float kLocalSelfShadowMinRatio = 0.50f; // 최소 조도 보장 비율
        selfShadowNdotL = lerp(ndotl, selfShadowNdotL, kLocalSelfShadowWeight);
        selfShadowNdotL = max(selfShadowNdotL, ndotl * kLocalSelfShadowMinRatio);
    }
    return saturate(selfShadowNdotL);
}

float2 Unpack2x8(float v)
{
    float raw = saturate(v) * 65535.0f;
    float hi = floor(raw / 256.0f);
    float lo = raw - hi * 256.0f;
    return float2(lo, hi) / 255.0f;
}


// ShadowCB (register b4)
cbuffer ShadowCB : register(b4)
{
    float4x4 g_ShadowLightViewProj;
    float    g_ShadowBias2;
    float    g_ShadowMapSize2;
    float    g_ShadowPCFRadius2;
    int      g_ShadowEnabled2;
    float    g_ShadowStrength2;
    float    g_ToonShadowStrength2;
    float2   g_ShadowPad2;
};

// 그림자 계산 함수 (PCF)
float CalcShadowFactorDeferred(float3 posW, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
{
    if (g_ShadowEnabled2 == 0) return 1.0f;

    float4 shadowPos = mul(float4(posW, 1.0f), g_ShadowLightViewProj);
    shadowPos.xyz /= shadowPos.w;

    float2 shadowTex;
    shadowTex.x = shadowPos.x * 0.5f + 0.5f;
    shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
    float depth = shadowPos.z;

    if (shadowTex.x < 0.0f || shadowTex.x > 1.0f || shadowTex.y < 0.0f || shadowTex.y > 1.0f)
        return 1.0f;

    const float2 texelSize = float2(1.0f, 1.0f) / max(g_ShadowMapSize2, 1.0f);
    const float2 pcfStep = max(g_ShadowPCFRadius2, 0.0f) * texelSize;

    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * pcfStep;
            sum += shadowMap.SampleCmpLevelZero(shadowSampler, shadowTex + offset, depth - g_ShadowBias2);
        }
    }
    return sum / 9.0f;
}

// 구조체 정의
struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// G-Buffer 텍스처 (압축)
Texture2D g_NormalRoughness : register(t0);
Texture2D g_Metalness : register(t1);
Texture2D g_BaseColor : register(t2);
Texture2D g_ToonParams : register(t3);
Texture2D g_ToonAlphas : register(t4);
Texture2D g_OutlineData : register(t5);
Texture2D<float> g_SceneDepth : register(t6);
TextureCube g_IBL_Diffuse : register(t7);
TextureCube g_IBL_Specular : register(t8);
Texture2D   g_IBL_BRDF_LUT : register(t9);
Texture2D<float> g_ShadowMap : register(t10);
Texture2D g_DecalAlbedo : register(t11);
Texture2DArray g_LocalShadow2DArray : register(t12);
TextureCubeArray g_LocalShadowCubeArray : register(t13);

SamplerState g_Sam : register(s0);
SamplerComparisonState g_ShadowSampler : register(s1);
SamplerState g_SamplerLinear : register(s2);

// 상수 버퍼
cbuffer ConstantBuffer : register(b0)
{
    float4x4 g_World;
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_InvViewProj;
    float4x4 g_WorldInvTranspose;
    float4 g_Material_ambient;
    float4 g_Material_diffuse;
    float4 g_Material_specular;
    float4 g_Material_reflect;
    float4 g_DirLight_ambient;
    float4 g_DirLight_diffuse;
    float4 g_DirLight_specular;
    float3 g_DirLight_direction;
    float  g_DirLight_intensity;
    float3 g_EyePosW;
    int    g_ShadingMode;
    int    g_EnableNormalMap;
    int    g_UseSpecularMap;
    int    g_UseDiffuseMap;
    float  g_Pad;
    int    g_UseTextureColor;
    float3 g_PBRPad;
    float4 g_PBRBaseColor;
    float  g_PBRMetalness;
    float  g_PBRRoughness;
    float  g_PBRAmbientOcclusion;
    float  g_PBRPad2;
    float  g_OutlineWidth;
    float  g_OutlinePow;
    float  g_OutlineThickness;
    float  g_OutlineStrength;
    float4 g_OutlineColor;
    float4x4 g_LightViewProj;
    float  g_ShadowBias;
    float  g_ShadowMapSize;
    float  g_ShadowPCFRadius;
    int    g_ShadowEnabled;
    int    g_BoundsBoneIndex;
    float3 g_BoundsPad;
};

cbuffer DirectionalLightBuffer : register(b3)
{
    float4 g_LightDirection;
    float4 g_LightColor;
    float g_intensity;
    float g_pad[3];
};

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 16
#define MAX_RECT_LIGHTS 16
#define MAX_POINT_SHADOW_LIGHTS 1
#define MAX_SPOT_SHADOW_LIGHTS 2
#define MAX_RECT_SHADOW_LIGHTS 1
#define MAX_SPOT_RECT_SHADOWS (MAX_SPOT_SHADOW_LIGHTS + MAX_RECT_SHADOW_LIGHTS)

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
    int    shadowIndex;
    float  shadowStrength;
    float2 pad;
};

struct SpotLight
{
    float3 position;
    float  range;
    float3 direction;
    float  innerCos;
    float3 color;
    float  outerCos;
    float  intensity;
    int    shadowIndex;
    float  shadowStrength;
    float  pad0;
};

struct RectLight
{
    float3 position;
    float  range;
    float3 direction;
    float  width;
    float3 color;
    float  height;
    float  intensity;
    int    shadowIndex;
    float  shadowStrength;
    float  pad0;
};

cbuffer ExtraLightsBuffer : register(b5)
{
    int g_PointLightCount;
    int g_SpotLightCount;
    int g_RectLightCount;
    int g_ExtraPad0;
    PointLight g_PointLights[MAX_POINT_LIGHTS];
    SpotLight  g_SpotLights[MAX_SPOT_LIGHTS];
    RectLight  g_RectLights[MAX_RECT_LIGHTS];
};

cbuffer LocalShadowBuffer : register(b6)
{
    float4x4 g_SpotRectShadowViewProj[MAX_SPOT_RECT_SHADOWS];
    float    g_SpotShadowMapSize;
    int      g_PointShadowCount;
    float    g_PointShadowMapSize;
    int      g_SpotRectShadowCount;
    float    g_PointShadowNearZ;
    float3   g_LocalShadowPad0;
};

float ComputeAttenuation(float dist, float range)
{
    float r = max(range, 0.001f);
    float att = saturate(1.0f - dist / r);
    return att * att;
}

float ComputeSpotFactor(float3 L, float3 lightDir, float innerCos, float outerCos)
{
    float cosTheta = dot(-L, normalize(lightDir));
    float denom = max(innerCos - outerCos, 1e-4f);
    return saturate((cosTheta - outerCos) / denom);
}

float ComputeRectFactor(float3 L, float3 lightDir)
{
    return saturate(dot(-L, normalize(lightDir)));
}

float CalcLocalSpotRectShadowFactor(float3 posW, int shadowIndex)
{
    if (shadowIndex < 0 || shadowIndex >= g_SpotRectShadowCount)
        return 1.0f;

    float4 shadowPos = mul(float4(posW, 1.0f), g_SpotRectShadowViewProj[shadowIndex]);
    shadowPos.xyz /= max(shadowPos.w, 1e-6f);

    float2 uv = float2(shadowPos.x * 0.5f + 0.5f, -shadowPos.y * 0.5f + 0.5f);
    float depth = shadowPos.z;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || depth <= 0.0f || depth >= 1.0f)
        return 1.0f;

    const float2 texelSize = float2(1.0f, 1.0f) / max(g_SpotShadowMapSize, 1.0f);
    const float bias = 0.0015f;
    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            sum += g_LocalShadow2DArray.SampleCmpLevelZero(g_ShadowSampler, float3(uv + offset, shadowIndex), depth - bias);
        }
    }
    return sum / 9.0f;
}

float ComputePointShadowDepth(float distanceToLight, float nearPlane, float farPlane)
{
    float safeFar = max(farPlane, nearPlane + 1e-3f);
    float m33 = safeFar / (safeFar - nearPlane);
    float m43 = (-nearPlane * safeFar) / (safeFar - nearPlane);
    return m33 + m43 / max(distanceToLight, nearPlane + 1e-4f);
}

float CalcLocalPointShadowFactor(float3 posW, float3 lightPos, float lightRange, int shadowIndex, float3 N, float3 L)
{
    if (shadowIndex < 0 || shadowIndex >= g_PointShadowCount)
        return 1.0f;
    if (lightRange <= g_PointShadowNearZ + 1e-4f)
        return 1.0f;

    float3 toPixel = posW - lightPos;
    float dist = length(toPixel);
    if (dist <= 1e-4f || dist >= lightRange)
        return 1.0f;

    float3 dir = toPixel / dist;
    // Point shadow is rendered as six 90-degree perspective faces.
    // The stored depth corresponds to face-space Z (dominant axis), not radial length.
    float faceDepth = max(max(abs(toPixel.x), abs(toPixel.y)), abs(toPixel.z));
    float currentDepth = ComputePointShadowDepth(faceDepth, g_PointShadowNearZ, lightRange);
    float invMapSize = 1.0f / max(g_PointShadowMapSize, 1.0f);
    float normalizedDist = saturate(dist / max(lightRange, 1e-4f));
    float ndotl = saturate(dot(normalize(N), normalize(L)));
    float slope = 1.0f - ndotl;
    float baseBias = invMapSize * lerp(0.28f, 0.52f, normalizedDist) + 0.00015f;
    float slopeBias = slope * (invMapSize * 0.75f + 0.00035f);
    float bias = min(baseBias + slopeBias, 0.0065f);

    float3 basisUp = (abs(dir.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 tangent = normalize(cross(basisUp, dir));
    float3 bitangent = normalize(cross(dir, tangent));
    float kernelRadius = invMapSize * (0.75f + 0.45f * normalizedDist);

    const float2 kernel[9] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
        float2(0.0f, 1.0f), float2(0.0f, -1.0f),
        float2(1.0f, 1.0f), float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f), float2(-1.0f, -1.0f)
    };

    float visible = 0.0f;
    [unroll] for (int i = 0; i < 9; ++i)
    {
        float3 offset = (kernel[i].x * tangent + kernel[i].y * bitangent) * kernelRadius;
        float3 sampleDir = normalize(dir + offset);
        float sampleDepth = g_LocalShadowCubeArray.SampleLevel(g_SamplerLinear, float4(sampleDir, shadowIndex), 0.0f).r;
        visible += (currentDepth - bias <= sampleDepth) ? 1.0f : 0.0f;
    }
    return visible / 9.0f;
}

float3 EvaluatePBRLight(float3 N, float3 V, float3 L, float3 albedoPBR, float metalness, float roughness, float3 lightColor, float ndotlOverride)
{
    float3 H = normalize(L + V);
    float NdotLSpec = saturate(dot(N, L));
    float NdotLDiff = saturate(ndotlOverride);
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    if (NdotLSpec <= 1e-5f && NdotLDiff <= 1e-5f)
    {
        return 0.0f;
    }

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotLSpec, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotLSpec, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoPBR * INV_PI;

    float3 diffuseLit = diffuse * lightColor * NdotLDiff;
    float3 specularLit = specular * lightColor * NdotLSpec;
    return diffuseLit + specularLit;
}

void AccumulateLegacy(float3 N, float3 V, float3 L, float3 lightColor, float atten, int mode, float shininess,
                      inout float3 outDiffuse, inout float3 outSpecular)
{
    float NdotL = max(dot(N, L), 0.0f);
    if (mode == 3)
    {
        float level = ToonLevel(NdotL);
        outDiffuse += level * lightColor * atten;
        return;
    }

    outDiffuse += NdotL * lightColor * atten;

    if (mode == 0 || NdotL <= 0.0f)
        return;

    float specTerm = 0.0f;
    if (mode == 2) // Blinn-Phong
    {
        float3 H = normalize(L + V);
        specTerm = pow(max(dot(N, H), 0.0f), shininess);
    }
    else // Phong
    {
        float3 R = reflect(-L, N);
        specTerm = pow(max(dot(R, V), 0.0f), shininess);
    }
    outSpecular += specTerm * lightColor * atten;
}
)";

	//float ComputeOutlineEdge(float2 uv, float2 texelSize, out float3 edgeColor, out float maxOutlineWidth)
//{
//    // Sample 대신 Load 사용 (int3 좌표: x, y, mipLevel)
//    // 텍스처 좌표는 정수형 인덱스로 접근해야 정확합니다.
//    int3 centerPos = int3(uv, 0);
//
//    //float4 center = g_OutlineData.Sample(g_Sam, uv);
//    float4 center = g_OutlineData.Load(centerPos);
//    maxOutlineWidth = max(center.a, 0.0f);
//
//    float3 colorAccum = float3(0.0f, 0.0f, 0.0f);
//    float colorWeight = 0.0f;
//    if (center.a > 1e-5f)
//    {
//        colorAccum += center.rgb;
//        colorWeight += 1.0f;
//    }
//
//    [unroll] for (int y = -1; y <= 1; ++y)
//    {
//        [unroll] for (int x = -1; x <= 1; ++x)
//        {
//            if (x == 0 && y == 0) continue;
//            float4 s = g_OutlineData.Sample(g_Sam, uv + float2(x, y) * texelSize);
//            maxOutlineWidth = max(maxOutlineWidth, max(s.a, 0.0f));
//            if (s.a > 1e-5f)
//            {
//                colorAccum += s.rgb;
//                colorWeight += 1.0f;
//            }
//        }
//    }
//
//    if (maxOutlineWidth <= 1e-5f)
//    {
//        edgeColor = float3(0.0f, 0.0f, 0.0f);
//        return 0.0f;
//    }
//
//    edgeColor = (colorWeight > 0.0f) ? (colorAccum / colorWeight) : center.rgb;
//
//    float widthPx = clamp(maxOutlineWidth * 120.0f, 1.0f, 8.0f);
//    float2 stepUV = texelSize * widthPx;
//
//    float m00 = (g_OutlineData.Sample(g_Sam, uv + float2(-1, -1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m10 = (g_OutlineData.Sample(g_Sam, uv + float2( 0, -1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m20 = (g_OutlineData.Sample(g_Sam, uv + float2( 1, -1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m01 = (g_OutlineData.Sample(g_Sam, uv + float2(-1,  0) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m21 = (g_OutlineData.Sample(g_Sam, uv + float2( 1,  0) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m02 = (g_OutlineData.Sample(g_Sam, uv + float2(-1,  1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m12 = (g_OutlineData.Sample(g_Sam, uv + float2( 0,  1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//    float m22 = (g_OutlineData.Sample(g_Sam, uv + float2( 1,  1) * stepUV).a > 1e-5f) ? 1.0f : 0.0f;
//
//    float gx = (m20 + 2.0f * m21 + m22) - (m00 + 2.0f * m01 + m02);
//    float gy = (m02 + 2.0f * m12 + m22) - (m00 + 2.0f * m10 + m20);
//    return saturate((abs(gx) + abs(gy)) * 0.25f);
//}
        inline static const char* LightPS2 = R"(
// 호출 시 pixelPos에는 SV_Position.xy (화면 픽셀 좌표)를 넣어주세요.
float ComputeOutlineEdge(float2 pixelPos, out float3 edgeColor, out float maxOutlineWidth)
{
    // 1. 픽셀 좌표 정수 변환 (Load 사용을 위해 필수)
    int3 C = int3((int)pixelPos.x, (int)pixelPos.y, 0);
    uint texWidth = 1;
    uint texHeight = 1;
    g_OutlineData.GetDimensions(texWidth, texHeight);
    int2 maxCoord = int2((int)texWidth - 1, (int)texHeight - 1);
    float minOutlineDepth = 1.0f;
    int2 outlineDepthCoord = C.xy;

    // 2. 중심 픽셀 로드
    float4 center = g_OutlineData.Load(C);
    
    // 초기값 설정
    maxOutlineWidth = center.a; 
    float3 colorAccum = float3(0.0f, 0.0f, 0.0f);
    float colorWeight = 0.0f;

    // 중심점이 아웃라인 오브젝트라면 색상 누적
    if (center.a > 1e-5f)
    {
        colorAccum += center.rgb;
        colorWeight += 1.0f;
        int2 centerCoord = clamp(C.xy, int2(0, 0), maxCoord);
        float centerDepth = g_SceneDepth.Load(int3(centerCoord, 0));
        if (centerDepth < minOutlineDepth)
        {
            minOutlineDepth = centerDepth;
            outlineDepthCoord = centerCoord;
        }
    }

    // 3. 주변 1픽셀 탐색 (최대 두께 및 색상 찾기)
    [unroll] 
    for (int y = -1; y <= 1; ++y)
    {
        [unroll] 
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            
            // Sample 대신 Load로 정확한 인접 픽셀 가져오기
            float4 s = g_OutlineData.Load(C + int3(x, y, 0));
            
            // 주변에서 가장 두꺼운 아웃라인 설정값 찾기
            maxOutlineWidth = max(maxOutlineWidth, s.a);
            
            // 유효한 색상이면 누적 (배경인 픽셀에서도 엣지 색상을 알기 위함)
            if (s.a > 1e-5f)
            {
                colorAccum += s.rgb;
                colorWeight += 1.0f;
                int2 sampleCoord = clamp(C.xy + int2(x, y), int2(0, 0), maxCoord);
                float sampleDepth = g_SceneDepth.Load(int3(sampleCoord, 0));
                if (sampleDepth < minOutlineDepth)
                {
                    minOutlineDepth = sampleDepth;
                    outlineDepthCoord = sampleCoord;
                }
            }
        }
    }

    // 주변에 아웃라인 데이터가 전혀 없으면 빈 엣지 리턴
    if (maxOutlineWidth <= 1e-5f)
    {
        edgeColor = float3(0.0f, 0.0f, 0.0f);
        return 0.0f;
    }

    // 평균 색상 계산 (주변 색상들을 섞어서 부드럽게)
    edgeColor = (colorWeight > 0.0f) ? (colorAccum / colorWeight) : center.rgb;

    // 4. 카메라 거리 기반 보정 (멀수록 얇아지되, 근거리 최대 두께는 유지)
    const float kOutlineReferenceDistance = 3.0f;
    float distanceScale = 1.0f;
    if (minOutlineDepth < 0.9999f)
    {
        float2 sampleUV = (float2(outlineDepthCoord) + 0.5f) / float2((float)texWidth, (float)texHeight);
        float2 sampleNdc;
        sampleNdc.x = sampleUV.x * 2.0f - 1.0f;
        sampleNdc.y = (1.0f - sampleUV.y) * 2.0f - 1.0f;
        float4 sampleClip = float4(sampleNdc, minOutlineDepth, 1.0f);
        float4 samplePosW4 = mul(sampleClip, g_InvViewProj);
        float3 samplePosW = samplePosW4.xyz / max(samplePosW4.w, 1e-6f);
        float cameraDistance = max(length(g_EyePosW - samplePosW), 0.1f);
        distanceScale = min(kOutlineReferenceDistance / cameraDistance, 1.0f);
    }

    // 5. Sobel Edge Detection (가변 두께 적용)
    // 두께(Alpha)에 따라 탐색 간격(Stride) 결정
    // outline의 최대 두께
    //int stride = clamp((int)(maxOutlineWidth * 120.0f), 1, 8); 
    float strideF = maxOutlineWidth * 200.0f * distanceScale;
    int stride = clamp((int)strideF, 1, 32);
    float subPixelFade = saturate(strideF);

    // Sobel 커널 적용 (Sample 대신 Load 사용)
    float m00 = (g_OutlineData.Load(C + int3(-stride, -stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;
    float m10 = (g_OutlineData.Load(C + int3( 0,      -stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;
    float m20 = (g_OutlineData.Load(C + int3( stride, -stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;
    
    float m01 = (g_OutlineData.Load(C + int3(-stride,  0,      0)).a > 1e-5f) ? 1.0f : 0.0f;
    float m21 = (g_OutlineData.Load(C + int3( stride,  0,      0)).a > 1e-5f) ? 1.0f : 0.0f;
    
    float m02 = (g_OutlineData.Load(C + int3(-stride,  stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;
    float m12 = (g_OutlineData.Load(C + int3( 0,       stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;
    float m22 = (g_OutlineData.Load(C + int3( stride,  stride, 0)).a > 1e-5f) ? 1.0f : 0.0f;

    // 수평/수직 변화량 계산
    float gx = (m20 + 2.0f * m21 + m22) - (m00 + 2.0f * m01 + m02);
    float gy = (m02 + 2.0f * m12 + m22) - (m00 + 2.0f * m10 + m20);

    // 엣지 강도 계산
    return saturate(sqrt(gx * gx + gy * gy)) * subPixelFade;
}
)" R"(
float4 main(PS_INPUT_QUAD pIn) : SV_Target
{
    // G-Buffer 가져오기
    float4 normalRoughness = g_NormalRoughness.Sample(g_Sam, pIn.uv);
    float4 metalness_packed = g_Metalness.Sample(g_Sam, pIn.uv);
    float4 baseColor = g_BaseColor.Sample(g_Sam, pIn.uv);
    float4 toonParams = g_ToonParams.Sample(g_Sam, pIn.uv);
    float4 toonAlphasSample = g_ToonAlphas.Sample(g_Sam, pIn.uv);
    
    float depth = g_SceneDepth.Sample(g_Sam, pIn.uv);

    // [수정] 아웃라인 계산 (pIn.Position -> pIn.position 소문자로 수정)
    float3 outlineEdgeColor = float3(0.0f, 0.0f, 0.0f);
    float outlineMaxWidth = 0.0f;
    float outlineEdge = ComputeOutlineEdge(pIn.position.xy, outlineEdgeColor, outlineMaxWidth);

    // [배경 처리] Depth가 1.0(배경)이라도 아웃라인이 있으면 그려야 함
    if (depth >= 0.9999f) 
    {
        if (outlineEdge > 1e-4f) 
        {
            return float4(outlineEdgeColor, 1.0f);
        }
        discard; // 아웃라인도 없으면 그리지 않음
    }

    // -- 이 아래는 물체 라이팅 연산 --

    float3 toonAlphas = toonAlphasSample.rgb;
    float2 shadowSelfPacked = Unpack2x8(toonAlphasSample.a);
    float  materialShadowStrength = shadowSelfPacked.x;
    float  toonSelfShadowStrength = shadowSelfPacked.y;
    float4 decalAlbedo = g_DecalAlbedo.Sample(g_Sam, pIn.uv);
    
    // 데이터 복원
    float3 N = normalize(normalRoughness.xyz * 2.0f - 1.0f);
    float metalness = metalness_packed.r;
    float3 toonCuts = float3(metalness_packed.g, metalness_packed.b, metalness_packed.a);
    float toonStrengthPacked = toonParams.r;
    float toonBlur = (toonStrengthPacked >= 0.5f) ? 1.0f : 0.0f;
    float toonStrength = saturate((toonStrengthPacked - toonBlur * 0.5f) * 2.0f);
    float3 toonLevels = toonParams.gba;
    float roughness = max(normalRoughness.w, 0.04f);
    
    // 월드 포지션 복원
    float2 ndc;
    ndc.x = pIn.uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - pIn.uv.y) * 2.0f - 1.0f;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 posW4 = mul(clip, g_InvViewProj);
    float3 posW = posW4.xyz / max(posW4.w, 1e-6f);

    // Decal
    baseColor.rgb = baseColor.rgb * (1.0f - decalAlbedo.a) + decalAlbedo.rgb;
    float3 albedo = baseColor.rgb;
    float3 albedoLinear = max(albedo, 0.0f);
    
    // shadingMode 디코딩
    float modeAo = saturate(baseColor.a) * 8.0f;
    int shadingMode = (int)floor(modeAo + 1e-4f);
    shadingMode = clamp(shadingMode, 0, 7);
    float ao = saturate(modeAo - shadingMode);
    
    // TextureOnly 모드
    if (shadingMode == 6)
    {
        float3 colorTexOnly = lerp(albedoLinear, outlineEdgeColor, outlineEdge);
        return float4(colorTexOnly, 1.0f);
    }

    // 라이팅 벡터
    float3 L = normalize(-g_LightDirection.xyz);
    float3 V = normalize(g_EyePosW - posW);
    float NdotV = saturate(dot(N, V));

    const bool usePbr = (shadingMode == 4 || shadingMode == 5 || shadingMode == 7);
    const bool toonPbr = (shadingMode == 5 || shadingMode == 7);
    const bool toonEditable = (shadingMode == 7);

    float envDiffuseStrength = 1.0f;
    float envSpecularStrength = 1.0f;
    float toonRampIntensity = 0.0f;

    if (toonPbr)
    {
        float2 levels12 = Unpack2x8(toonParams.g);
        float2 level3Ramp = Unpack2x8(toonParams.b);
        toonLevels = float3(levels12.x, levels12.y, level3Ramp.x);
        toonRampIntensity = level3Ramp.y;
        float2 env = Unpack2x8(toonParams.a);
        envDiffuseStrength = env.x;
        envSpecularStrength = env.y;
    }
    else
    {
        envDiffuseStrength = toonParams.r;
        envSpecularStrength = toonParams.g;
    }

    float shadowVis = CalcShadowFactorDeferred(posW, g_ShadowMap, g_ShadowSampler);
    float shadowStrength = saturate(g_ShadowStrength2);
    shadowStrength *= saturate(materialShadowStrength);
    if (toonEditable)
    {
        shadowStrength *= saturate(g_ToonShadowStrength2);
        const float kToonPbrShadowAtten = 0.35f;
        shadowStrength *= kToonPbrShadowAtten;
    }
    shadowStrength = saturate(shadowStrength);
    shadowVis = saturate(lerp(1.0f, shadowVis, shadowStrength));

    // [Legacy Lighting]
    if (!usePbr)
    {
        float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
        float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
        float shininess = max(g_Material_specular.a, 1.0f);
        float3 lightColorDir = g_LightColor.rgb * g_intensity;

        AccumulateLegacy(N, V, L, lightColorDir, shadowVis, shadingMode, shininess, totalDiffuse, totalSpecular);

        [loop] for (int i = 0; i < g_PointLightCount; ++i) {
             PointLight pl = g_PointLights[i];
             float3 toLight = pl.position - posW;
             float dist = length(toLight);
             float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
             float atten = ComputeAttenuation(dist, pl.range);
             float localShadow = (pl.shadowIndex >= 0) ? CalcLocalPointShadowFactor(posW, pl.position, pl.range, pl.shadowIndex, N, Lp) : 1.0f;
             localShadow = lerp(1.0f, localShadow, saturate(pl.shadowStrength));
             float3 lc = pl.color * pl.intensity * atten * localShadow;
             AccumulateLegacy(N, V, Lp, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }
        
        [loop] for (int i = 0; i < g_SpotLightCount; ++i) {
            SpotLight sl = g_SpotLights[i];
            float3 toLight = sl.position - posW;
            float dist = length(toLight);
            float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, sl.range);
            float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
            float localShadow = (sl.shadowIndex >= 0) ? CalcLocalSpotRectShadowFactor(posW, sl.shadowIndex) : 1.0f;
            localShadow = lerp(1.0f, localShadow, saturate(sl.shadowStrength));
            float3 lc = sl.color * sl.intensity * atten * spot * localShadow;
            AccumulateLegacy(N, V, Ls, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        [loop] for (int i = 0; i < g_RectLightCount; ++i) {
            RectLight rl = g_RectLights[i];
            float3 toLight = rl.position - posW;
            float dist = length(toLight);
            float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, rl.range);
            float facing = ComputeRectFactor(Lr, rl.direction);
            float areaScale = max(rl.width * rl.height, 0.01f);
            float localShadow = (rl.shadowIndex >= 0) ? CalcLocalSpotRectShadowFactor(posW, rl.shadowIndex) : 1.0f;
            localShadow = lerp(1.0f, localShadow, saturate(rl.shadowStrength));
            float3 lc = rl.color * rl.intensity * atten * facing * areaScale * localShadow;
            AccumulateLegacy(N, V, Lr, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        float3 ambient = g_DirLight_ambient.rgb * albedoLinear;
        float3 color = ambient + totalDiffuse * albedoLinear + totalSpecular * g_Material_specular.rgb;
        
        // [아웃라인 합성]
        color = lerp(color, outlineEdgeColor, outlineEdge);
        return float4(color, 1.0f);
    }

    // [PBR Lighting]
    float3 albedoPBR = albedoLinear;
    roughness = max(roughness, 0.04f);
    
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float3 kS_IBL = FresnelSchlick(F0, NdotV);
    float3 kD = (1.0f - kS_IBL) * (1.0f - metalness);
    
    // Direct Light
    float3 lightColorDir = g_LightColor.rgb * g_intensity;
    float3 directLighting = 0.0f;
    {
        float ndotl = max(dot(N, L), 0.0f);
        float shadedNdotL = ndotl;
        if (toonPbr && ndotl > 0.0f) {
            shadedNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonAlphas, toonStrength, toonBlur, toonRampIntensity) : ToonLevel(ndotl);
        }
        float selfShadowNdotL = ApplySelfShadowNdotL(shadedNdotL, toonSelfShadowStrength);
        float3 lit = EvaluatePBRLight(N, V, L, albedoPBR, metalness, roughness, lightColorDir, selfShadowNdotL);
        directLighting += lit * shadowVis * ao;
    }

    float3 extraLighting = float3(0.0f, 0.0f, 0.0f);
    
    [loop] for (int i = 0; i < g_PointLightCount; ++i) {
        PointLight pl = g_PointLights[i];
        float3 toLight = pl.position - posW;
        float dist = length(toLight);
        float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, pl.range);
        float3 lc = pl.color * pl.intensity * atten;
        float localShadow = (pl.shadowIndex >= 0) ? CalcLocalPointShadowFactor(posW, pl.position, pl.range, pl.shadowIndex, N, Lp) : 1.0f;
        localShadow = lerp(1.0f, localShadow, saturate(pl.shadowStrength));
        float ndotl = max(dot(N, Lp), 0.0f);
        float shadedNdotL = ndotl;
        if (toonPbr && ndotl > 0.0f) {
            shadedNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonAlphas, toonStrength, toonBlur, toonRampIntensity) : ToonLevel(ndotl);
        }
        float selfShadowNdotL = ApplyLocalToonSelfShadow(ndotl, shadedNdotL, toonEditable, toonSelfShadowStrength);
        float3 lit = EvaluatePBRLight(N, V, Lp, albedoPBR, metalness, roughness, lc, selfShadowNdotL);
        extraLighting += lit * ao * localShadow;
    }

    [loop] for (int i = 0; i < g_SpotLightCount; ++i) {
        SpotLight sl = g_SpotLights[i];
        float3 toLight = sl.position - posW;
        float dist = length(toLight);
        float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, sl.range);
        float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
        float3 lc = sl.color * sl.intensity * atten * spot;
        float localShadow = (sl.shadowIndex >= 0) ? CalcLocalSpotRectShadowFactor(posW, sl.shadowIndex) : 1.0f;
        localShadow = lerp(1.0f, localShadow, saturate(sl.shadowStrength));
        float ndotl = max(dot(N, Ls), 0.0f);
        float shadedNdotL = ndotl;
        if (toonPbr && ndotl > 0.0f) {
            shadedNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonAlphas, toonStrength, toonBlur, toonRampIntensity) : ToonLevel(ndotl);
        }
        float selfShadowNdotL = ApplyLocalToonSelfShadow(ndotl, shadedNdotL, toonEditable, toonSelfShadowStrength);
        float3 lit = EvaluatePBRLight(N, V, Ls, albedoPBR, metalness, roughness, lc, selfShadowNdotL);
        extraLighting += lit * ao * localShadow;
    }

    [loop] for (int i = 0; i < g_RectLightCount; ++i) {
        RectLight rl = g_RectLights[i];
        float3 toLight = rl.position - posW;
        float dist = length(toLight);
        float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, rl.range);
        float facing = ComputeRectFactor(Lr, rl.direction);
        float areaScale = max(rl.width * rl.height, 0.01f);
        float3 lc = rl.color * rl.intensity * atten * facing * areaScale;
        float localShadow = (rl.shadowIndex >= 0) ? CalcLocalSpotRectShadowFactor(posW, rl.shadowIndex) : 1.0f;
        localShadow = lerp(1.0f, localShadow, saturate(rl.shadowStrength));
        float ndotl = max(dot(N, Lr), 0.0f);
        float shadedNdotL = ndotl;
        if (toonPbr && ndotl > 0.0f) {
            shadedNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonAlphas, toonStrength, toonBlur, toonRampIntensity) : ToonLevel(ndotl);
        }
        float selfShadowNdotL = ApplyLocalToonSelfShadow(ndotl, shadedNdotL, toonEditable, toonSelfShadowStrength);
        float3 lit = EvaluatePBRLight(N, V, Lr, albedoPBR, metalness, roughness, lc, selfShadowNdotL);
        extraLighting += lit * ao * localShadow;
    }

    //// 임시 느낌
    // Indirect Light (IBL)
    //  Diffuse 계산 시 거칠기(Roughness)를 고려한 Fresnel 사용
    // 기존에는 그냥 kD를 구했지만, 이제는 거친 표면에서 에너지가 보존되도록 kS를 먼저 구합니다.
    

    float3 kS_IBL_ = fresnelSchlickRoughness(F0, NdotV, roughness);
    float3 kD_IBL_ = (1.0f - kS_IBL_) * (1.0f - metalness);
    
    // Diffuse 샘플링
    float3 diffuseIBL = kD_IBL_ * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoPBR;


    //float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoPBR;
    
    // Specular 계산 및 호라이즌 오클루전 적용
    float3 Renv = reflect(-V, N);
    
    // 텍스처 밉맵 레벨 (가지고 계신 텍스처가 256x256이면 보통 8, 128x128이면 7 정도입니다)
    const float kMaxSpecularMip = 8.0f; 
    
    // Prefiltered Color 샘플링 (Roughness에 따라 흐릿한 MipMap 가져오기)
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    
    // BRDF LUT 샘플링
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_SamplerLinear, float2(NdotV, roughness)).rg;
    
    // Specular 합산
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);
    
    // Horizon Specular Occlusion 적용 
    // (시야각이 표면과 수평에 가까울 때 생기는 과한 반사를 억제)
    float horizon = horizonSpecularOcclusion(NdotV, roughness);
    specularIBL *= horizon;

    // 강도 조절 (기존 파라미터 유지)
    diffuseIBL *= envDiffuseStrength;
    specularIBL *= envSpecularStrength;

    // 그림자/AO 적용 (기존 로직 유지)
    float shadowIBLDiffuse = lerp(0.35f, 1.0f, shadowVis);
    float shadowIBLSpecular = 1.0f;
    if (toonEditable) {
        shadowIBLDiffuse = shadowVis;
        shadowIBLSpecular = lerp(0.25f, 1.0f, shadowVis);
    }

    // 최종 IBL 합산 (Diffuse + Specular) * Ambient Occlusion
    float3 iblColor = (diffuseIBL * shadowIBLDiffuse + specularIBL * shadowIBLSpecular) * ao;
    
    // -----------------------------------------------------------------------

    // 최종 색상 계산
    float3 color = directLighting + extraLighting + iblColor;

    // [아웃라인 합성]
    color = lerp(color, outlineEdgeColor, outlineEdge);

    return float4(color, 1.0f);
}
)";

        // Transparent Forward-Style Skinned VS
        inline static const char* TransparentSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    float2 TexCoord     : TEXCOORD0;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];

    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));

    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style Skinned Instanced VS (본 없는 FBX 인스턴싱용)
        inline static const char* TransparentSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // 인스턴스 월드 행렬 복원
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float3 N = normalize(mul(float4(input.Normal, 0.0f), world).xyz);

    // 아웃라인: 스무스 노멀 방향으로 확장
    //float3 smoothN = normalize(mul(float4(input.SmoothNormal, 0.0f), world).xyz);
    //float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    //
    //float4 posW = mul(float4(input.Position + posOffset, 1.0f), world);
    //output.Position = mul(mul(posW, gView), gProj);
    //output.WorldPos = posW.xyz;
    
    //output.Normal   = N;
    //output.TangentW = normalize(mul(float4(input.Tangent, 0.0f), world).xyz);
    //output.BitanW   = normalize(mul(float4(input.Binormal, 0.0f), world).xyz);
    //output.TexCoord = input.TexCoord;

    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    float3 smoothN = normalize(mul(world, float4(input.SmoothNormal, 0.0f)).xyz);
    
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);

    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    output.TangentW = normalize(mul(world, float4(input.Tangent, 0.0f)).xyz);
    output.BitanW   = normalize(mul(world, float4(input.Binormal, 0.0f)).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style PS
        inline static const char* TransparentPS = R"(
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float ToonLevel(float n)
{
    if (n > 0.95f) return 1.0f;
    if (n > 0.5f)  return 0.7f;
    if (n > 0.2f)  return 0.4f;
    return 0.1f;
}

float ToonStepEditable(float n, float3 cuts, float3 levels, float3 alphas, float strength, float blur, float rampIntensity)
{
    float c1 = saturate(cuts.x);
    float c2 = saturate(cuts.y);
    float c3 = saturate(cuts.z);
    c2 = max(c2, c1 + 1e-4f);
    c3 = max(c3, c2 + 1e-4f);

    float l0 = saturate(levels.x);
    float l1 = saturate(levels.y);
    float l2 = saturate(levels.z);
    float l3 = 1.0f;

    float a0 = saturate(alphas.x);
    float a1 = saturate(alphas.y);
    float a2 = saturate(alphas.z);
    float a3 = 1.0f;

    float t = saturate(strength);
    float ramp = saturate(rampIntensity);
    if (blur > 0.5f)
    {
        float w = max(fwidth(n) * 2.0f, 0.02f);
        float s1 = smoothstep(c1 - w, c1 + w, n);
        float s2 = smoothstep(c2 - w, c2 + w, n);
        float s3 = smoothstep(c3 - w, c3 + w, n);

        float level = lerp(l0, l1, s1);
        level = lerp(level, l2, s2);
        level = lerp(level, l3, s3);
        float alpha = lerp(a0, a1, s1);
        alpha = lerp(alpha, a2, s2);
        alpha = lerp(alpha, a3, s3);
        float darkMask = 1.0f - s1;
        alpha *= (1.0f - ramp * darkMask);
        return lerp(n, level, t * alpha);
    }

    float level = (n > c3) ? l3 :
                  (n > c2) ? l2 :
                  (n > c1) ? l1 :
                             l0;
    float alpha = (n > c3) ? a3 :
                  (n > c2) ? a2 :
                  (n > c1) ? a1 :
                             a0;
    float darkMask = (n > c1) ? 0.0f : 1.0f;
    alpha *= (1.0f - ramp * darkMask);
    return lerp(n, level, t * alpha);
}

float ApplySelfShadowNdotL(float shadedNdotL, float selfShadowStrength)
{
    return lerp(1.0f, shadedNdotL, saturate(selfShadowStrength));
}

// 텍스처
Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);

// IBL
TextureCube g_IBL_Diffuse : register(t5);
TextureCube g_IBL_Specular : register(t6);
Texture2D   g_IBL_BRDF_LUT : register(t7);

SamplerState g_Sam : register(s0);

cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBTransparentLight : register(b1)
{
    float3 g_LightDir;
    float  g_LightIntensity;
    float3 g_LightColor;
    float  _pad0;
    float3 g_CameraPosW;
    float  _pad1;
};

struct PSIn
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

float DitherThreshold(float2 pos)
{
    // Interleaved gradient noise (per-pixel hash, less visible grid)
    float n = 0.06711056f * pos.x + 0.00583715f * pos.y;
    return frac(52.9829189f * frac(n));
}

float3 LinearToSRGB(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

float4 main(PSIn pIn) : SV_Target
{
    float4 tex = float4(1,1,1,1);
    if (gUseTexture != 0)
        tex = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);

    float alphaTex = tex.a * gMaterialColor.a;

    // 컷아웃은 텍스처 알파로만 처리 (머티리얼 알파는 블렌딩)
    if (gUseTexture != 0)
    {
        clip(tex.a - 0.1f);
    }
    // NDC 기반 디더링으로 투명도 처리 (알파 블렌딩 대신 화면 도트 컷아웃)
    float alpha = saturate(alphaTex);
    if (alpha < 1.0f)
    {
        float threshold = DitherThreshold(pIn.Position.xy);
        clip(alpha - threshold);
    }
    // 거의 불투명은 디퍼드에서 처리하므로 여기서는 제외
    if (alphaTex >= 0.99f) discard;
    float alphaOut = 1.0f;

    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
        baseColor *= tex.rgb;

    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (gShadingMode == 6)
    {
        return float4(baseColor, alphaOut);
    }

    // 컬러 텍스처 샘플은 이미 linear 공간입니다.
    float3 albedoLinear = max(baseColor, 0.0f);

    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }

    float metalness = saturate(gMetalness);
    float roughness = max(saturate(gRoughness), 0.04f);
    float ao = saturate(gAmbientOcclusion);

    float3 L = normalize(-g_LightDir);
    float3 V = normalize(g_CameraPosW - pIn.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    const bool toonPbr = (gShadingMode == 5 || gShadingMode == 7);
    const bool toonEditable = (gShadingMode == 7);
    float shadedNdotL = NdotL;
    if (toonPbr && NdotL > 0.0f)
    {
        shadedNdotL = toonEditable
            ? ToonStepEditable(NdotL, gToonPbrCuts.xyz, gToonPbrLevels.xyz, gToonPbrAlphas.xyz, gToonPbrCuts.w, gToonPbrLevels.w, gToonPbrRampIntensity)
            : ToonLevel(NdotL);
    }
    float selfShadowNdotL = ApplySelfShadowNdotL(shadedNdotL, gToonSelfShadowStrength);

    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoLinear, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, selfShadowNdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * selfShadowNdotL, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoLinear * INV_PI;

    float3 radiance = g_LightColor.rgb * g_LightIntensity;
    float3 direct = (diffuse + specular) * radiance * selfShadowNdotL * ao;

    // IBL
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoLinear;
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_Sam, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);

    diffuseIBL *= gEnvDiffuseStrength;
    specularIBL *= gEnvSpecularStrength;
    float3 ibl = (diffuseIBL + specularIBL) * ao;

    float3 outLinear = direct + ibl;
    return float4(outLinear, alphaOut);
}
)";

        // Shadow VS (Static)
        inline static const char* ShadowVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    float4 posW = mul(float4(input.Position, 1.0f), gWorld);
    o.Position = mul(mul(posW, gView), gProj);
    return o;
}
)";

        // Shadow Instanced VS (Static)
        inline static const char* ShadowInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 iWorld0  : INSTANCE_WORLD0;
    float4 iWorld1  : INSTANCE_WORLD1;
    float4 iWorld2  : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);
    
    float4 posW = mul(world, float4(input.Position, 1.0f));
    o.Position = mul(mul(posW, gView), gProj);
    return o;
}
)";

        // Shadow Skinned VS
        inline static const char* ShadowSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0; 
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    
    // 본 인덱스와 가중치를 가져옴
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    
    // 스키닝 행렬 계산
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    // 위치 변환 (Local -> Skinned -> World -> View -> Proj)
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float4 posW = mul(skinnedPos, gWorld);
    
    o.Position = mul(mul(posW, gView), gProj);
    
    return o;
}
)";

        // Shadow Skinned Instanced VS (본 없는 FBX 인스턴싱용)
        inline static const char* ShadowSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    float4   gToonPbrAlphas;
    float    gToonPbrRampIntensity;
    float    gToonSelfShadowStrength;
    float2   gPadOutline;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // 인스턴스 월드 행렬 복원
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float4 posW = mul(float4(input.Position, 1.0f), world);
    float4 posW = mul(world, float4(input.Position, 1.0f));
    o.Position = mul(mul(posW, gView), gProj);

    return o;
}
)";

        // UI Render Vertex Shader
        inline static const char* UIRenderVS = R"(
cbuffer UICompositeCB : register(b0)
{
    uint isUseMetalness;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

PS_INPUT main(uint vid : SV_VertexID) 
{
    PS_INPUT o;
    uint tmp = isUseMetalness;
    
    //사각형 4점
    float2 p0 = float2(-1.0, -1.0); 
    float2 p1 = float2(-1.0, 1.0); 
    float2 p2 = float2(1.0, 1.0); 
    float2 p3 = float2(1.0, -1.0); 

    // UV좌표 4점
    float2 t0 = float2(0.0, 1.0);
    float2 t1 = float2(0.0, 0.0);
    float2 t2 = float2(1.0, 0.0);
    float2 t3 = float2(1.0, 1.0);

    // 삼각형 2개
    float2 pos[6] = { p0, p1, p2, p0, p2, p3 };
    float2 uv[6] = { t0, t1, t2, t0, t2, t3 };

    o.Pos = float4(pos[vid][0], pos[vid][1], 0.0, 1.0);
    o.Tex = uv[vid];
    return o;
}
)";

        // UI Render Pixel Shader
        inline static const char* UIRenderPS = R"(
    struct PS_INPUT
    {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };

    Texture2D UITexture : register(t101);
    SamplerState SamLinear : register(s0);

    float4 main(PS_INPUT input) : SV_Target
    {
        float4 UITex = UITexture.Sample(SamLinear, input.Tex);
    
        // UI 텍스처 알파값으로 UI, 화면 구분
        if (UITex.a <= 0.0f)
        {
            discard;
        }
        
        return UITex;
    }
)";
    };
}

