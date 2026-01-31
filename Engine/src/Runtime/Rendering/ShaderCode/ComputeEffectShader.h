#pragma once

namespace Alice
{
    /// 컴퓨트 셰이더 이펙트 전용 셰이더 코드
    class ComputeEffectShader
    {
    public:
        // 기본 컴퓨트 셰이더
        // - 간단한 이미지 처리 예제 (각 픽셀에 색상 조정)
        inline static const char* BasicCS = R"(
cbuffer CBParams : register(b0)
{
    float4 gParams;      // 이펙트 파라미터
    float4 gTime;        // 시간 정보
    float4 gResolution;  // 해상도 정보 (width, height, 1/width, 1/height)
    float4 gPadding;
};

// 입력 텍스처 (옵션)
Texture2D<float4> gInputTexture : register(t0);
SamplerState gSampler : register(s0);

// 출력 UAV
RWTexture2D<float4> gOutputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    
    // 해상도 체크
    if (pixelCoord.x >= (uint)gResolution.x || pixelCoord.y >= (uint)gResolution.y)
        return;

    // UV 좌표 계산
    float2 uv = (pixelCoord + 0.5) * gResolution.zw;
    
    // 기본 색상 (또는 입력 텍스처에서 샘플링)
    float4 color = float4(uv.x, uv.y, 0.5, 1.0);
    
    // 간단한 이펙트 적용 (예: 그라데이션)
    color.rgb *= gParams.rgb;
    color.a *= gParams.a;
    
    // 출력
    gOutputTexture[pixelCoord] = color;
}
)";

        //============================================================
        // GPU 파티클 (2D 오버레이) 예제
        //
        // 리소스 레이아웃(각 셰이더 별)
        //  - ClearCS : u0 = RWTexture2D<float4>
        //  - UpdateCS: u0 = RWStructuredBuffer<Particle>
        //  - DrawCS  : t0 = StructuredBuffer<Particle>, u0 = RWTexture2D<float4>
        //
        // 상수 버퍼(b0)
        //  gParams0 = (emitterX, emitterY, emitterRadius, spawnJitter)
        //  gParams1 = (colorR, colorG, colorB, particleSizePx)
        //  gTime    = (timeSec, dtSec, particleCount, 0)
        //  gResolution = (width, height, 1/width, 1/height)
        //============================================================

        inline static const char* ParticleClearCS = R"(
cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWTexture2D<float4> outTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 p = id.xy;
    if (p.x >= (uint)resolution.x || p.y >= (uint)resolution.y) return;
    outTex[p] = float4(0,0,0,0);
}
)";

        inline static const char* ParticleUpdateCS = R"(
struct Particle
{
    float3 pos;   // 월드 좌표
    float  life;  // sec
    float3 vel;   // 월드 좌표 기준 속도
    float  seed;
    uint   emitterIndex;
    float3 pad;   // 16바이트 정렬용
};

struct EmitterGPU
{
    float4 p0; // xyz = pos, w = radius
    float4 p1; // xyz = color, w = sizePx
    float4 p2; // xyz = gravity, w = drag
    float4 p3; // x = lifeMin, y = lifeMax, z = intensity, w = depthBiasMeters
    float4 p4; // x = depthTest (1.0 or 0.0), yzw unused
};

cbuffer CBParams : register(b0)
{
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4 emitterInfo; // x = emitterCount, y = nearZ, z = farZ, w = 0 (bias는 emitter별로 EmitterGPU.p3.w에 저장됨)
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];

    float t  = time.x;
    float dt = time.y;
    float jitter = time.w;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    // emitter 선택 (죽은 파티클 리스폰 시)
    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        // Hash 기반으로 emitter 선택
        float base = (float)i * 1.2345 + t * 13.37 + p.seed * 101.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        // 즉시 재스폰이 가장 안전
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.2345 + t * 13.37 + p.seed * 101.0;

        float2 r01 = Hash21(base);
        float3 r11 = float3(r01 * 2.0 - 1.0, Hash11(base + 37.0) * 2.0 - 1.0);

        float3 dir = normalize(r11 + 1e-5);
        float  rr  = sqrt(Hash11(base + 91.0)) * radius;

        p.pos = emitter + dir * rr;

        float3 rv = float3(Hash21(base + 191.0) * 2.0 - 1.0, Hash11(base + 251.0) * 2.0 - 1.0);
        p.vel = normalize(rv + 1e-5) * float3(0.20, 0.45, 0.20);

        p.life = lerp(lifeMin, lifeMax, Hash11(base + 311.0));
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        p.vel += gravity * dt;
        p.vel *= pow(max(drag, 0.001), dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* ParticleDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0; // xyz = pos, w = radius
    float4 p1; // xyz = color, w = sizePx
    float4 p2; // xyz = gravity, w = drag
    float4 p3; // x = lifeMin, y = lifeMax, z = intensity, w = depthBiasMeters
    float4 p4; // x = depthTest (1.0 or 0.0), yzw unused
};

cbuffer CBParams : register(b0)
{
    float4 time;        // timeSec, dtSec, particleCount, spawnJitter
    float4 resolution;  // w,h,invW,invH
    float4 emitterInfo; // x = emitterCount, y = nearZ, z = farZ, w = 0 (bias는 emitter별로 EmitterGPU.p3.w에 저장됨)
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

// sceneDepth가 null일 수 있으므로 체크 필요 (HLSL에서는 포인터 체크 불가, 일단 사용)

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지) - emitterCount는 이미 위에서 선언됨
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        // 범위 밖이면 즉시 재스폰
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float size = max(e.p1.w, 1.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias

    // 월드 좌표 → 클립 공간
    // viewProj는 XMMatrixTranspose로 전달되므로 mul(vector, matrix) 사용 (다른 셰이더와 동일)
    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    // 클립 공간 → NDC → 스크린 좌표
    if (clipPos.w <= 0.0) return; // 카메라 뒤에 있으면 스킵
    
    float3 ndc = clipPos.xyz / clipPos.w;
    // DirectX 좌표계: NDC Y는 +1(위)~-1(아래), 스크린 Y는 0(위)~height(아래)
    // 따라서 Y축을 뒤집어야 함: screenY = (1.0 - (ndc.y * 0.5 + 0.5)) * height
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // LH 투영: z = (nearZ * farZ) / (farZ - depthValue * (farZ - nearZ))
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    int2 ip = int2(screenPos);

    int r = (int)clamp(size * 0.5, 1.0, 6.0);

    float speed = length(p.vel);
    float intensity = saturate(speed * 1.5) * saturate(p.life);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);

        // 원자적 누적이 아니라서 완벽한 additive는 아님(그래도 데모로는 충분히 보임)
        outTex[q] = max(outTex[q], stamp);
    }
}
)";


        //============================================================
        // 추가 파티클 프리셋들
        //  - ClearCS는 ParticleClearCS 재사용
        //  - Update/Draw만 타입별로 다르게 구성
        //============================================================

        // --------------------------
        // Sparks (스파크/불꽃 튐)
        // --------------------------
        inline static const char* SparksUpdateCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    float t  = time.x;
    float dt = time.y;
    float jitter = time.w;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        float base = (float)i * 1.913 + t * 19.71 + p.seed * 97.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.913 + t * 19.71 + p.seed * 97.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 37.0) * 2.0 - 1.0);

        float3 dir = normalize(r3 + 1e-5);
        float rr = sqrt(Hash11(base + 91.0)) * radius;
        p.pos = emitter + dir * rr;

        float3 rv = float3(Hash21(base + 131.0) * 2.0 - 1.0, Hash11(base + 251.0) * 2.0 - 1.0);
        float spd = 0.45 + Hash11(base + 171.0) * 0.85;
        p.vel = normalize(rv + 1e-5) * spd;

        p.life = lerp(lifeMin, lifeMax, Hash11(base + 211.0));
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        p.vel += gravity * dt;
        p.vel *= pow(max(drag, 0.001), dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* SparksDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지)
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float baseSize = max(e.p1.w, 1.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias

    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    float2 vPx = float2(0.0, 0.0);
    float4 clipPos2 = mul(float4(p.pos + p.vel * 0.03, 1.0), viewProj);
    if (clipPos2.w > 0.0)
    {
        float3 ndc2 = clipPos2.xyz / clipPos2.w;
        float2 screenPos2 = float2(
            (ndc2.x * 0.5 + 0.5) * resolution.x,
            (1.0 - (ndc2.y * 0.5 + 0.5)) * resolution.y
        );
        vPx = screenPos2 - screenPos;
    }

    float speed = max(length(vPx), 1.0);
    float2 dir  = normalize(vPx + 1e-3);

    float L = clamp(speed * 0.03 + baseSize * 1.2, 3.0, 22.0);
    float R = clamp(baseSize * 0.35, 1.0, 5.0);

    int2 ip = int2(screenPos);
    int maxR = (int)ceil(max(L, R)) + 1;

    float fade = saturate(p.life / 0.8);

    for (int y = -maxR; y <= maxR; ++y)
    for (int x = -maxR; x <= maxR; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float2 pPx = (float2(q) + 0.5) - screenPos;
        float  along = dot(pPx, dir);
        float2 perpV = pPx - dir * along;
        float  perp = length(perpV);

        // 꼬리: 뒤로 조금, 앞으로 길게
        if (along < -L * 0.25 || along > L) continue;
        if (perp > R * 3.0) continue;

        float wAlong = exp(- (along*along) / (L*L));
        float wPerp  = exp(- (perp*perp) / (R*R));
        float a = wAlong * wPerp * fade;

        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Smoke (연기)
        // --------------------------
        inline static const char* SmokeUpdateCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    float t  = time.x;
    float dt = time.y;
    float jitter = time.w;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        float base = (float)i * 0.777 + t * 3.1 + p.seed * 113.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    if (p.life <= 0.0)
    {
        float base = (float)i * 0.777 + t * 3.1 + p.seed * 113.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 33.0) * 2.0 - 1.0);

        float3 dir = normalize(r3 + 1e-5);
        float rr = sqrt(Hash11(base + 33.0)) * radius;
        p.pos = emitter + dir * rr;

        float3 rv = float3(Hash21(base + 77.0) * 2.0 - 1.0, Hash11(base + 97.0) * 2.0 - 1.0);
        p.vel = float3(rv.x * 0.05, 0.10 + abs(rv.y) * 0.08, rv.z * 0.05);

        p.life = lerp(lifeMin, lifeMax, Hash11(base + 401.0));
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        float a = 6.2831853 * Hash11(p.seed * 31.0 + floor(t * 2.0));
        float3 swirl = float3(cos(a) * 0.02, 0.0, sin(a) * 0.02);

        p.vel += (gravity + swirl) * dt;
        p.vel *= pow(max(drag, 0.001), dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* SmokeDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지)
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float baseSize = max(e.p1.w, 1.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    float maxLife = lerp(lifeMin, lifeMax, frac(p.seed * 19.1));
    float age01 = 1.0 - saturate(p.life / maxLife);

    int2 ip = int2(screenPos);

    float size = clamp(baseSize * (0.7 + age01 * 2.2), 2.0, 32.0);
    int r = (int)clamp(size * 0.5, 2.0, 16.0);

    float fade = (1.0 - age01) * 0.55;

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        // 픽셀마다 미세한 알파 노이즈(구름 질감)
        float n = Hash11((float)(q.x * 17 + q.y * 131) + p.seed * 997.0);
        float a = w * fade * (0.75 + 0.35 * n);

        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Vortex (소용돌이)
        // --------------------------
        inline static const char* VortexUpdateCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    float t  = time.x;
    float dt = time.y;
    float jitter = time.w;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        float base = (float)i * 2.11 + t * 7.7 + p.seed * 111.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    if (p.life <= 0.0)
    {
        float base = (float)i * 2.11 + t * 7.7 + p.seed * 111.0;
        float2 r01 = Hash21(base);
        float ang = r01.x * 6.2831853;
        float rr = sqrt(r01.y) * radius;
        float h = (Hash11(base + 19.0) - 0.5) * radius * 0.5;

        p.pos = emitter + float3(cos(ang) * rr, h, sin(ang) * rr);

        float3 tang = float3(-sin(ang), 0.0, cos(ang));
        p.vel = tang * (0.05 + Hash11(base + 19.0) * 0.10);

        p.life = lerp(lifeMin, lifeMax, Hash11(base + 211.0));
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        float3 toC = emitter - p.pos;
        float dist = length(toC) + 1e-4;
        float3 dirC = toC / dist;
        float3 tang = cross(dirC, float3(0.0, 1.0, 0.0));
        if (length(tang) < 1e-3) tang = float3(1.0, 0.0, 0.0);
        tang = normalize(tang);

        float pull = 0.20 + 0.35 * saturate(dist / radius);
        float spin = 0.65;

        p.vel += (dirC * pull + tang * spin + gravity) * dt;
        p.vel *= pow(max(drag, 0.001), dt);
        p.pos += p.vel * dt;
        p.life -= dt;

        if (dist > radius * 2.5) p.life = 0.0;
    }

    particles[i] = p;
}
)";

        inline static const char* VortexDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지)
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float size = clamp(max(e.p1.w, 1.0) * 0.9, 1.0, 10.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias

    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    int2 ip = int2(screenPos);

    int r = (int)clamp(size * 0.5, 1.0, 6.0);

    float intensity = saturate(p.life * 0.5);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Snow (눈/먼지)
        // --------------------------
        inline static const char* SnowUpdateCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    float t  = time.x;
    float dt = time.y;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        float base = (float)i * 0.91 + t * 0.37 + p.seed * 401.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;
    
    if (p.life <= 0.0)
    {
        float base = (float)i * 0.91 + t * 0.37 + p.seed * 401.0;
        float2 r = Hash21(base);

        p.pos = emitter + float3((r.x - 0.5) * radius * 2.0, radius * 0.5 + r.y * radius, (r.y - 0.5) * radius * 2.0);
        p.vel = float3((r.y - 0.5) * 0.05, -(0.05 + r.x * 0.08), 0.0);
        p.life = lerp(lifeMin, lifeMax, Hash11(base + 19.0));
        p.seed = frac(p.seed + Hash11(base + 401.0));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        float sway = (Hash11(p.seed * 91.0 + floor(t * 2.0)) - 0.5) * 0.02;
        p.vel.x += sway * dt;
        p.vel.x = clamp(p.vel.x, -0.08, 0.08);
        p.vel += gravity * dt;
        p.vel *= pow(max(drag, 0.001), dt);

        p.pos += p.vel * dt;
        p.life -= dt;

        if (p.pos.y < emitter.y - radius * 0.5) p.life = 0.0;
    }

    particles[i] = p;
}
)";

        inline static const char* SnowDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지)
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float size = clamp(max(e.p1.w, 1.0) * 0.6, 1.0, 6.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias

    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    int2 ip = int2(screenPos);

    int r = (int)clamp(size * 0.5, 1.0, 3.0);

    float intensity = saturate(p.life / 6.0);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";

        // --------------------------
        // Explosion (폭발/버스트)
        // --------------------------
        inline static const char* ExplosionUpdateCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

RWStructuredBuffer<Particle> particles : register(u0);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }
float2 Hash21(float n)
{
    float x = Hash11(n);
    float y = Hash11(n + 17.0);
    return float2(x, y);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    float t  = time.x;
    float dt = time.y;
    float jitter = time.w;
    uint emitterCount = (uint)emitterInfo.x;

    if (emitterCount == 0) return;

    uint emitterIdx = p.emitterIndex;
    if (p.life <= 0.0)
    {
        float base = (float)i * 1.57 + t * 9.0 + p.seed * 181.0;
        emitterIdx = (Hash11(base) * (float)emitterCount);
        emitterIdx = min(emitterIdx, emitterCount - 1);
    }
    
    // OOB 방지: emitterCount가 줄었을 때 안전하게 처리
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }

    EmitterGPU e = gEmitters[emitterIdx];
    float3 emitter = e.p0.xyz;
    float radius = max(e.p0.w, 0.0001);
    float3 gravity = e.p2.xyz;
    float drag = e.p2.w;
    float lifeMin = e.p3.x;
    float lifeMax = e.p3.y;

    if (p.life <= 0.0)
    {
        float base = (float)i * 1.57 + t * 9.0 + p.seed * 181.0;
        float2 r = Hash21(base);
        float3 r3 = float3(r * 2.0 - 1.0, Hash11(base + 31.0) * 2.0 - 1.0);
        float3 dir = normalize(r3 + 1e-5);

        float rr = sqrt(Hash11(base + 31.0)) * radius;
        p.pos = emitter + dir * rr;

        float spd = 0.35 + Hash11(base + 71.0) * 0.95;
        p.vel = dir * spd;

        p.life = lerp(lifeMin, lifeMax, Hash11(base + 211.0));
        p.seed = frac(p.seed + Hash11(base + 401.0) * (1.0 + jitter));
        p.emitterIndex = emitterIdx;
    }
    else
    {
        p.vel += gravity * dt;
        p.vel *= pow(max(drag, 0.001), dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }

    particles[i] = p;
}
)";

        inline static const char* ExplosionDrawCS = R"(
struct Particle
{
    float3 pos;
    float  life;
    float3 vel;
    float  seed;
    uint   emitterIndex;
    float3 pad;
};

struct EmitterGPU
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4; // x = depthTest (1.0 or 0.0)
};

cbuffer CBParams : register(b0)
{
    float4 time;
    float4 resolution;
    float4 emitterInfo;
    float4x4 viewProj;
    float4 cameraPos;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D<float> sceneDepth : register(t1);
StructuredBuffer<EmitterGPU> gEmitters : register(t2);
SamplerState PointSampler : register(s0);  // Point Clamp 샘플러 (depth 경계 보간 방지)
RWTexture2D<float4> outTex : register(u0);

float Hash11(float n) { return frac(sin(n) * 43758.5453123); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // emitterCount==0 가드 (GPU 크래시 방지)
    uint emitterCount = (uint)emitterInfo.x;
    if (emitterCount == 0) return;
    
    uint i = id.x;
    uint maxCount = (uint)time.z;
    if (i >= maxCount) return;

    Particle p = particles[i];
    if (p.life <= 0.0) return;

    // emitter 정보 읽기 (OOB 방지)
    uint emitterIdx = p.emitterIndex;
    if (emitterIdx >= emitterCount)
    {
        p.life = 0.0;
        emitterIdx = 0;
    }
    EmitterGPU e = gEmitters[emitterIdx];
    bool depthTest = (e.p4.x > 0.5);
    float emitterIntensity = e.p3.z;
    float3 color = e.p1.xyz * emitterIntensity; // intensity 곱하기
    float baseSize = max(e.p1.w, 1.0);
    float depthBiasMeters = e.p3.w;  // emitter별 depth bias

    float4 worldPos = float4(p.pos, 1.0);
    float4 clipPos = mul(worldPos, viewProj);
    
    if (clipPos.w <= 0.0) return;
    
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 screenPos = float2(
        (ndc.x * 0.5 + 0.5) * resolution.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * resolution.y
    );
    
    // depth test (emitter별 설정) - view-space Z로 선형화해서 비교
    if (depthTest)
    {
        float nearZ = emitterInfo.y;
        float farZ = emitterInfo.z;
        float bias = depthBiasMeters;  // emitter별 bias 사용
        
        // sceneDepthValue(0..1) -> view-space Z로 선형화 (LH 기준)
        // Load()로 정수 픽셀 좌표 직접 읽기 (샘플러 경로 없이 정확함)
        int2 pixelCoord = int2(screenPos);
        float sceneDepthValue = sceneDepth.Load(int3(pixelCoord, 0)).r;
        float sceneViewZ = (nearZ * farZ) / (farZ - sceneDepthValue * (farZ - nearZ));
        
        // 파티클 view-space Z: clipPos.w가 view-space Z (표준 프로젝션)
        float particleViewZ = clipPos.w;
        
        if (sceneDepthValue > 0.001 && particleViewZ > sceneViewZ + bias) return;
    }
    
    int2 ip = int2(screenPos);

    float speed = length(p.vel);
    float size = clamp(baseSize * (1.0 + speed * 1.5), 2.0, 18.0);
    int r = (int)clamp(size * 0.5, 2.0, 10.0);

    float intensity = saturate(p.life * 1.5);

    for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x)
    {
        int2 q = ip + int2(x, y);
        if (q.x < 0 || q.y < 0 || q.x >= (int)resolution.x || q.y >= (int)resolution.y) continue;

        float d2 = (float)(x*x + y*y);
        float w = exp(-d2 / (size * size));

        float a = w * intensity;
        float4 stamp = float4(color * a, a);
        outTex[q] = max(outTex[q], stamp);
    }
}
)";
    };
}
