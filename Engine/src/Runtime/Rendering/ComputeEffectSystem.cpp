#include "ComputeEffectSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <d3dcompiler.h>
#include "Runtime/Rendering/ShaderCode/ComputeEffectShader.h"
#include "Runtime/Foundation/Logger.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        // 전역 정보만 담는 CB (멀티 이미터 지원)
        struct CBParams
        {
            XMFLOAT4 time;        // (timeSec, dtSec, particleCount, spawnJitter)
            XMFLOAT4 resolution;  // (w, h, invW, invH)
            XMFLOAT4 emitterInfo; // (emitterCount, 0, 0, 0)
            XMFLOAT4X4 viewProj;  // View * Projection 행렬
            XMFLOAT4 cameraPos;   // (cameraX, cameraY, cameraZ, 0)
        };

        // EmitterGPU: HLSL과 동일한 레이아웃 (float4 5개 = 80바이트)
        struct EmitterGPU
        {
            XMFLOAT4 p0; // xyz = pos, w = radius
            XMFLOAT4 p1; // xyz = color, w = sizePx
            XMFLOAT4 p2; // xyz = gravity, w = drag
            XMFLOAT4 p3; // x = lifeMin, y = lifeMax, z = intensity, w = depthBiasMeters
            XMFLOAT4 p4; // x = depthTest (1.0 or 0.0), yzw unused
        };
        static_assert(sizeof(EmitterGPU) == 80, "EmitterGPU must be 80 bytes");

        // ParticleInit: emitterIndex 추가
        struct ParticleInit
        {
            XMFLOAT3 pos;   // 월드 좌표
            float life;
            XMFLOAT3 vel;   // 월드 좌표 기준 속도
            float seed;
            std::uint32_t emitterIndex; // 이미터 인덱스
            XMFLOAT3 pad;   // 16바이트 정렬용
        };
        static_assert(sizeof(ParticleInit) == 48, "ParticleInit must match HLSL Particle layout");

        static float ClampFloat(float v, float lo, float hi)
        {
            return std::max(lo, std::min(hi, v));
        }
    }

    static HRESULT CompileCSBlob(
        const char* source,
        const char* entry,
        const char* target,
        ID3DBlob** outBlob,
        ID3DBlob** outError)
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        return D3DCompile(
            source,
            std::strlen(source),
            nullptr,
            nullptr,
            nullptr,
            entry,
            target,
            flags,
            0,
            outBlob,
            outError
        );
    }

    ComputeEffectSystem::ComputeEffectSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    ComputeEffectSystem::~ComputeEffectSystem()
    {
    }

    bool ComputeEffectSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        ALICE_LOG_INFO("ComputeEffectSystem::Initialize: begin (width=%u, height=%u)", width, height);

        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: invalid device/context (device=%p, context=%p)", m_device.Get(), m_context.Get());
            return false;
        }

        // 기본 파티클 셰이더 세트 등록
        ALICE_LOG_INFO("ComputeEffectSystem::Initialize: registering shader set 'Particle'...");
        if (!RegisterParticleShaderSet("Particle",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::ParticleUpdateCS,
                                       ComputeEffectShader::ParticleDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet('Particle') failed. Check shader compilation errors above.");
            return false;
        }

        // 추가 파티클 프리셋 등록
        if (!RegisterParticleShaderSet("Sparks",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::SparksUpdateCS,
                                       ComputeEffectShader::SparksDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(Sparks) failed.");
            return false;
        }

        if (!RegisterParticleShaderSet("Smoke",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::SmokeUpdateCS,
                                       ComputeEffectShader::SmokeDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(Smoke) failed.");
            return false;
        }

        if (!RegisterParticleShaderSet("Vortex",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::VortexUpdateCS,
                                       ComputeEffectShader::VortexDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(Vortex) failed.");
            return false;
        }

        if (!RegisterParticleShaderSet("Snow",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::SnowUpdateCS,
                                       ComputeEffectShader::SnowDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(Snow) failed.");
            return false;
        }

        if (!RegisterParticleShaderSet("Explosion",
                                       ComputeEffectShader::ParticleClearCS,
                                       ComputeEffectShader::ExplosionUpdateCS,
                                       ComputeEffectShader::ExplosionDrawCS))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: RegisterParticleShaderSet(Explosion) failed.");
            return false;
        }

        // Clear: 모든 프리셋이 m_outputUAV 공유 → 시스템 전역 공통 Clear 1회.
        // 고정 m_clearShader 사용 (unordered_map.begin() 비결정적 선택 제거)
        m_clearShader = m_presets["Particle"].shaders.clearShader;

        if (!CreateConstantBuffer())
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateConstantBuffer failed.");
            return false;
        }

        if (!CreateUnorderedAccessViews(width, height))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateUnorderedAccessViews failed.");
            return false;
        }

        if (!CreatePointSampler())
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreatePointSampler failed.");
            return false;
        }

        if (!CreateDummyDepthTexture())
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Initialize: CreateDummyDepthTexture failed.");
            return false;
        }

        m_width  = width;
        m_height = height;

        ALICE_LOG_INFO("ComputeEffectSystem::Initialize: success");
        return true;
    }

    void ComputeEffectSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (m_width == width && m_height == height)
            return;

        m_outputTexture.Reset();
        m_outputUAV.Reset();
        m_outputSRV.Reset();

        if (CreateUnorderedAccessViews(width, height))
        {
            m_width  = width;
            m_height = height;
        }
    }

    void ComputeEffectSystem::SetParticleCount(std::uint32_t particleCount)
    {
        particleCount = std::max<std::uint32_t>(particleCount, 1);
        if (particleCount == m_particleCount)
            return;

        m_particleCount = particleCount;

        // 모든 프리셋의 파티클 풀 재생성 (다음 Execute에서 EnsureParticlePool이 호출됨)
        for (auto& [name, preset] : m_presets)
        {
            preset.pool.buffer.Reset();
            preset.pool.uav.Reset();
            preset.pool.srv.Reset();
            preset.pool.count = 0;
        }
    }

    void ComputeEffectSystem::SetEmitterNormalized(float x, float y, float radius)
    {
        // 레거시 함수: 멀티 이미터 구조에서는 사용되지 않음
        // 컴포넌트별로 개별 설정하는 것을 권장
        ALICE_LOG_WARN("[ComputeEffectSystem] SetEmitterNormalized is deprecated. Use ComputeEffectComponent to set emitter parameters per entity.");
    }

    void ComputeEffectSystem::SetParticleColor(float r, float g, float b)
    {
        // 레거시 함수: 멀티 이미터 구조에서는 사용되지 않음
        // 컴포넌트별로 개별 설정하는 것을 권장
        ALICE_LOG_WARN("[ComputeEffectSystem] SetParticleColor is deprecated. Use ComputeEffectComponent to set particle color per entity.");
    }
    
    void ComputeEffectSystem::SetParticleSizePx(float sizePx)
    {
        // 레거시 함수: 멀티 이미터 구조에서는 사용되지 않음
        // 컴포넌트별로 개별 설정하는 것을 권장
        ALICE_LOG_WARN("[ComputeEffectSystem] SetParticleSizePx is deprecated. Use ComputeEffectComponent to set particle size per entity.");
    }

    bool ComputeEffectSystem::HasActiveEffect() const
    {
        return m_hasActiveEffect;
    }

    void ComputeEffectSystem::Execute(const World& world, const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& cameraPos, ID3D11ShaderResourceView* sceneDepthSRV, float nearPlane, float farPlane, float dtSec)
    {
        if (!m_constantBuffer || !m_outputUAV)
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::Execute: resources not initialized");
            m_hasActiveEffect = false;
            return;
        }

        // 카메라 정보 저장
        m_viewProj = viewProj;
        m_cameraPos = cameraPos;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;

        // 여러 이펙트 동시 지원: 프리셋별로 그룹핑
        std::unordered_map<std::string, std::vector<std::pair<EntityId, const ComputeEffectComponent*>>> emittersByPreset;
        
        for (auto&& [entityId, effect] : world.GetComponents<ComputeEffectComponent>())
        {
            if (!effect.enabled || effect.shaderName.empty())
                continue;
            if (const auto* tr = world.GetComponent<TransformComponent>(entityId); tr && (!tr->enabled || !tr->visible))
                continue;

            // 등록된 프리셋이 있는지 확인
            if (m_presets.find(effect.shaderName) == m_presets.end())
                continue;

            emittersByPreset[effect.shaderName].push_back({ entityId, &effect });
        }

        if (emittersByPreset.empty())
        {
            m_hasActiveEffect = false;
            return;
        }

        m_hasActiveEffect = true;

        // dtSec를 사용하여 시간 업데이트
        m_dtSec = ClampFloat(dtSec, 0.0f, 1.0f / 20.0f);
        m_timeSec += m_dtSec;

        // depthSRV가 nullptr이면 더미 depth 사용
        ID3D11ShaderResourceView* depthSRVToUse = sceneDepthSRV;
        if (!depthSRVToUse)
        {
            depthSRVToUse = m_dummyDepthSRV.Get();
        }

        // Clear: 모든 프리셋이 m_outputUAV(출력 텍스처)를 공유하므로,
        // 시스템 전역 공통 m_clearShader로 1회만 Clear.
        // (프리셋별 타겟이 있었다면 순회 시 프리셋마다 개별 Clear 필요)
        if (m_clearShader)
            DispatchClear(m_clearShader.Get());

        // 프리셋별 Update/Draw — 순서 보장을 위해 이름 기준 정렬 후 순회
        std::vector<std::string> presetOrder;
        presetOrder.reserve(emittersByPreset.size());
        for (const auto& [k, v] : emittersByPreset)
            presetOrder.push_back(k);
        std::sort(presetOrder.begin(), presetOrder.end());

        for (const auto& presetName : presetOrder)
        {
            const auto& emitters = emittersByPreset[presetName];
            auto presetIt = m_presets.find(presetName);
            if (presetIt == m_presets.end())
                continue;

            PresetRuntime& preset = presetIt->second;
            if (!preset.shaders.updateShader || !preset.shaders.drawShader)
                continue;

            // 프리셋별 파티클 풀 확보
            if (!EnsureParticlePool(presetName, m_particleCount))
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::Execute: EnsureParticlePool failed (preset=%s)", presetName.c_str());
                continue;
            }

            // Emitter 리스트를 EmitterGPU로 변환
            std::vector<EmitterGPU> emitterData;
            emitterData.reserve(emitters.size());
            
            for (const auto& [entityId, effect] : emitters)
            {
                // 월드 위치 결정 (Transform 연동)
                XMFLOAT3 emitterPos{};
                if (effect->useTransform)
                {
                    if (auto* t = world.GetComponent<TransformComponent>(entityId))
                    {
                        emitterPos = t->position;
                        emitterPos.x += effect->localOffset.x;
                        emitterPos.y += effect->localOffset.y;
                        emitterPos.z += effect->localOffset.z;
                    }
                    else
                    {
                        emitterPos = effect->effectParams; // fallback
                    }
                }
                else
                {
                    emitterPos = effect->effectParams; // 기존 방식 호환
                }

                EmitterGPU emitter;
                emitter.p0 = XMFLOAT4(emitterPos.x, emitterPos.y, emitterPos.z, ClampFloat(effect->radius, 0.01f, 5.0f));
                // 파라미터 Clamp: 안전한 범위로 제한
                emitter.p1 = XMFLOAT4(
                    ClampFloat(effect->color.x, 0.0f, 10.0f),
                    ClampFloat(effect->color.y, 0.0f, 10.0f),
                    ClampFloat(effect->color.z, 0.0f, 10.0f),
                    ClampFloat(effect->sizePx, 0.1f, 100.0f)
                );
                emitter.p2 = XMFLOAT4(
                    ClampFloat(effect->gravity.x, -50.0f, 50.0f),
                    ClampFloat(effect->gravity.y, -50.0f, 50.0f),
                    ClampFloat(effect->gravity.z, -50.0f, 50.0f),
                    ClampFloat(effect->drag, 0.0f, 1.0f)  // drag는 0~1 범위
                );
                float intensityValue = ClampFloat(effect->intensity, 0.0f, 10.0f);
                emitter.p3 = XMFLOAT4(
                    ClampFloat(effect->lifeMin, 0.01f, 100.0f),
                    ClampFloat(effect->lifeMax, 0.01f, 100.0f),
                    intensityValue,
                    ClampFloat(effect->depthBiasMeters, 0.0f, 1.0f)
                );
                emitter.p4 = XMFLOAT4(effect->depthTest ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                
                emitterData.push_back(emitter);
            }

            // Emitter 버퍼 확보 및 업로드
            std::uint32_t emitterCount = static_cast<std::uint32_t>(emitterData.size());
            if (!EnsureEmitterBufferCapacity(presetName, emitterCount))
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::Execute: EnsureEmitterBufferCapacity failed (preset=%s)", presetName.c_str());
                continue;
            }

            // Emitter 데이터 업로드 (Map/Unmap)
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = m_context->Map(preset.emitters.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (SUCCEEDED(hr))
            {
                std::memcpy(mapped.pData, emitterData.data(), sizeof(EmitterGPU) * emitterCount);
                m_context->Unmap(preset.emitters.buffer.Get(), 0);
            }
            else
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::Execute: Map failed (preset=%s, hr=0x%08X)", presetName.c_str(), (unsigned)hr);
                continue;
            }

            // CB 업데이트 (emitterCount, near/far 포함, bias는 emitter별로 사용)
            UpdateConstantBuffer(emitterCount);

            // Update/Draw 실행
            DispatchParticlesUpdate(preset.shaders.updateShader.Get(), 
                                   preset.pool.uav.Get(),
                                   preset.emitters.srv.Get());
            
            // depth SRV는 항상 진짜 depth SRV를 바인딩 (shader에서 per-particle depthTest로 분기)
            // CPU에서 첫 emitter만 확인해서 dummy로 바꾸면, 다른 emitter의 depthTest가 깨짐
            ID3D11ShaderResourceView* depthForDraw = depthSRVToUse;
            
            DispatchParticlesDraw(preset.shaders.drawShader.Get(),
                                preset.pool.srv.Get(),
                                preset.emitters.srv.Get(),
                                depthForDraw);
        }

        UnbindCS();

        // 모든 UAV slot을 확실히 unbind (UAV와 SRV 동시 바인딩 충돌 방지)
        // DirectX11에서는 같은 리소스를 UAV와 SRV로 동시에 바인딩할 수 없음
        // D3D11_1_UAV_SLOT_COUNT = 64이지만, 일반적으로 8개면 충분
        ID3D11UnorderedAccessView* nullUAVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
    }

    bool ComputeEffectSystem::RegisterParticleShaderSet(const std::string& name, 
                                                        const char* clearCS, 
                                                        const char* updateCS, 
                                                        const char* drawCS)
    {
        auto createOne = [&](const char* code, ComPtr<ID3D11ComputeShader>& outShader, const char* label) -> bool
        {
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;

            HRESULT hr = CompileCSBlob(code, "main", "cs_5_0", blob.GetAddressOf(), err.GetAddressOf());
            if (FAILED(hr))
            {
                if (err)
                {
                    ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): %s", name.c_str(), label,
                        static_cast<const char*>(err->GetBufferPointer()));
                }
                else
                {
                    ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): D3DCompile failed (0x%08X)", name.c_str(), label, (unsigned)hr);
                }
                return false;
            }

            hr = m_device->CreateComputeShader(
                blob->GetBufferPointer(),
                blob->GetBufferSize(),
                nullptr,
                outShader.GetAddressOf());

            if (FAILED(hr))
            {
                ALICE_LOG_ERRORF("ComputeEffectSystem::RegisterParticleShaderSet(%s, %s): CreateComputeShader failed (0x%08X)", name.c_str(), label, (unsigned)hr);
                return false;
            }

            return true;
        };

        PresetRuntime preset;
        if (!createOne(clearCS,  preset.shaders.clearShader,  "ClearCS"))  return false;
        if (!createOne(updateCS, preset.shaders.updateShader, "UpdateCS")) return false;
        if (!createOne(drawCS,   preset.shaders.drawShader,    "DrawCS"))   return false;

        m_presets[name] = std::move(preset);
        ALICE_LOG_INFO("ComputeEffectSystem::RegisterParticleShaderSet: registered '%s'", name.c_str());
        return true;
    }

    bool ComputeEffectSystem::CreateConstantBuffer()
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = sizeof(CBParams);
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateConstantBuffer: CreateBuffer failed.");
            return false;
        }

        return true;
    }

    std::vector<std::string> ComputeEffectSystem::GetRegisteredShaderNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_presets.size());
        for (const auto& [name, preset] : m_presets)
        {
            names.push_back(name);
        }
        return names;
    }

    bool ComputeEffectSystem::EnsureParticlePool(const std::string& presetName, std::uint32_t particleCount)
    {
        auto it = m_presets.find(presetName);
        if (it == m_presets.end())
            return false;

        ParticlePool& pool = it->second.pool;
        
        // 이미 생성되어 있고 크기가 같으면 재사용
        if (pool.buffer && pool.count == particleCount)
            return true;

        // 기존 리소스 해제
        pool.buffer.Reset();
        pool.uav.Reset();
        pool.srv.Reset();

        // 초기화 데이터 생성
        std::vector<ParticleInit> init(particleCount);
        for (std::uint32_t i = 0; i < particleCount; ++i)
        {
            float s = (float)i * 0.6180339887f;
            s -= std::floor(s);

            init[i].pos  = XMFLOAT3(0.0f, 0.0f, 0.0f);
            init[i].life = 0.0f;
            init[i].vel  = XMFLOAT3(0.0f, 0.0f, 0.0f);
            init[i].seed = s;
            init[i].emitterIndex = 0;
            init[i].pad = XMFLOAT3(0.0f, 0.0f, 0.0f);
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = sizeof(ParticleInit) * particleCount;
        desc.Usage               = D3D11_USAGE_DEFAULT;
        desc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags      = 0;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ParticleInit);

        D3D11_SUBRESOURCE_DATA srd = {};
        srd.pSysMem = init.data();

        HRESULT hr = m_device->CreateBuffer(&desc, &srd, pool.buffer.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::EnsureParticlePool: CreateBuffer failed (preset=%s, count=%u)", presetName.c_str(), particleCount);
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format              = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements  = particleCount;

        hr = m_device->CreateUnorderedAccessView(pool.buffer.Get(), &uavDesc, pool.uav.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::EnsureParticlePool: CreateUnorderedAccessView failed");
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = particleCount;

        hr = m_device->CreateShaderResourceView(pool.buffer.Get(), &srvDesc, pool.srv.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::EnsureParticlePool: CreateShaderResourceView failed");
            return false;
        }

        pool.count = particleCount;
        return true;
    }

    bool ComputeEffectSystem::EnsureEmitterBufferCapacity(const std::string& presetName, std::uint32_t emitterCount)
    {
        auto it = m_presets.find(presetName);
        if (it == m_presets.end())
            return false;

        EmitterBuffer& emitters = it->second.emitters;
        
        // 이미 생성되어 있고 용량이 충분하면 재사용
        if (emitters.buffer && emitters.capacity >= emitterCount)
            return true;

        // 기존 리소스 해제
        emitters.buffer.Reset();
        emitters.srv.Reset();

        // 최소 용량 보장 (동적 증가)
        std::uint32_t capacity = std::max(emitterCount, 16u);

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = sizeof(EmitterGPU) * capacity;
        desc.Usage               = D3D11_USAGE_DYNAMIC;
        desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(EmitterGPU);

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, emitters.buffer.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::EnsureEmitterBufferCapacity: CreateBuffer failed (preset=%s, count=%u)", presetName.c_str(), emitterCount);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = capacity;

        hr = m_device->CreateShaderResourceView(emitters.buffer.Get(), &srvDesc, emitters.srv.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::EnsureEmitterBufferCapacity: CreateShaderResourceView failed");
            return false;
        }

        emitters.capacity = capacity;
        return true;
    }

    bool ComputeEffectSystem::CreatePointSampler()
    {
        D3D11_SAMPLER_DESC desc = {};
        // Depth는 Point 샘플러 사용 (Linear는 깊이 경계를 흐리게 만들어 오클루전 아티팩트 발생)
        // Point 샘플링으로 경계에서 보간 없이 정확한 depth 값을 얻어 파티클 깜빡임 방지
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.BorderColor[0] = 0.0f;
        desc.BorderColor[1] = 0.0f;
        desc.BorderColor[2] = 0.0f;
        desc.BorderColor[3] = 0.0f;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = m_device->CreateSamplerState(&desc, m_pointSampler.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreatePointSampler: CreateSamplerState failed.");
            return false;
        }

        return true;
    }

    bool ComputeEffectSystem::CreateDummyDepthTexture()
    {
        // 1x1 R32_FLOAT 텍스처 생성 (값=1.0, 즉 far plane)
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        float depthValue = 1.0f; // far plane
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = &depthValue;
        initData.SysMemPitch = sizeof(float);
        initData.SysMemSlicePitch = sizeof(float);

        HRESULT hr = m_device->CreateTexture2D(&desc, &initData, m_dummyDepthTexture.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateDummyDepthTexture: CreateTexture2D failed (0x%08X)", (unsigned)hr);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        hr = m_device->CreateShaderResourceView(m_dummyDepthTexture.Get(), &srvDesc, m_dummyDepthSRV.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateDummyDepthTexture: CreateShaderResourceView failed (0x%08X)", (unsigned)hr);
            return false;
        }

        return true;
    }

    void ComputeEffectSystem::UpdateConstantBuffer(std::uint32_t emitterCount)
    {
        float w = (float)std::max<std::uint32_t>(m_width, 1);
        float h = (float)std::max<std::uint32_t>(m_height, 1);

        CBParams cb{};
        cb.time       = XMFLOAT4(m_timeSec, m_dtSec, (float)m_particleCount, 0.25f);  // spawnJitter를 time.w로 이동
        cb.resolution = XMFLOAT4(w, h, 1.0f / w, 1.0f / h);
        // emitterInfo: x=emitterCount, y=nearZ, z=farZ, w=0 (bias는 emitter별로 EmitterGPU.p3.w에 저장됨)
        cb.emitterInfo = XMFLOAT4((float)emitterCount, m_nearPlane, m_farPlane, 0.0f);
        XMStoreFloat4x4(&cb.viewProj, XMMatrixTranspose(m_viewProj));
        cb.cameraPos  = XMFLOAT4(m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, 0.0f);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::UpdateConstantBuffer: Map failed (0x%08X)", (unsigned)hr);
            return;
        }

        std::memcpy(mapped.pData, &cb, sizeof(CBParams));
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    void ComputeEffectSystem::DispatchClear(ID3D11ComputeShader* clearShader)
    {
        if (!clearShader) return;
        
        m_context->CSSetShader(clearShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        ID3D11UnorderedAccessView* uav = m_outputUAV.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tgX = (m_width + 7) / 8;
        UINT tgY = (m_height + 7) / 8;
        m_context->Dispatch(tgX, tgY, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    void ComputeEffectSystem::DispatchParticlesUpdate(ID3D11ComputeShader* updateShader,
                                                      ID3D11UnorderedAccessView* particleUAV,
                                                      ID3D11ShaderResourceView* emitterSRV)
    {
        if (!updateShader || !particleUAV) return;
        
        m_context->CSSetShader(updateShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        // Emitter SRV 바인딩 (t2)
        if (emitterSRV)
        {
            ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr, emitterSRV };
            m_context->CSSetShaderResources(0, 3, srvs);
        }

        ID3D11UnorderedAccessView* uav = particleUAV;
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tg = (m_particleCount + 255) / 256;
        m_context->Dispatch(tg, 1, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

        if (emitterSRV)
        {
            ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
            m_context->CSSetShaderResources(0, 3, nullSRVs);
        }
    }

    void ComputeEffectSystem::DispatchParticlesDraw(ID3D11ComputeShader* drawShader,
                                                    ID3D11ShaderResourceView* particleSRV,
                                                    ID3D11ShaderResourceView* emitterSRV,
                                                    ID3D11ShaderResourceView* sceneDepthSRV)
    {
        if (!drawShader || !particleSRV) return;
        
        m_context->CSSetShader(drawShader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        
        if (m_pointSampler)
        {
            ID3D11SamplerState* samplers[] = { m_pointSampler.Get() };
            m_context->CSSetSamplers(0, 1, samplers);
        }

        // SRV 바인딩: t0=particles, t1=depth, t2=emitters
        ID3D11ShaderResourceView* srvs[3] = { particleSRV, sceneDepthSRV, emitterSRV };
        m_context->CSSetShaderResources(0, 3, srvs);

        ID3D11UnorderedAccessView* uav = m_outputUAV.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        UINT tg = (m_particleCount + 255) / 256;
        m_context->Dispatch(tg, 1, 1);

        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        m_context->CSSetShaderResources(0, 3, nullSRVs);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    void ComputeEffectSystem::UnbindCS()
    {
        m_context->CSSetShader(nullptr, nullptr, 0);

        ID3D11Buffer* nullCB = nullptr;
        m_context->CSSetConstantBuffers(0, 1, &nullCB);

        ID3D11SamplerState* nullSampler = nullptr;
        m_context->CSSetSamplers(0, 1, &nullSampler);

        // SRV 3개 unbind (particleSRV, sceneDepthSRV, emitterSRV)
        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        m_context->CSSetShaderResources(0, 3, nullSRVs);

        // 모든 UAV slot을 확실히 unbind (UAV와 SRV 동시 바인딩 충돌 방지)
        // DirectX11에서는 같은 리소스를 UAV와 SRV로 동시에 바인딩할 수 없음
        // D3D11_1_UAV_SLOT_COUNT = 64이지만, 일반적으로 8개면 충분하지만 안전을 위해 전체 슬롯 unbind
        // 참고: Compute Shader는 최대 8개 UAV slot을 지원 (D3D11_FEATURE_D3D11_OPTIONS::ComputeShadersPlusRawAndStructuredBuffersViaShader4X)
        ID3D11UnorderedAccessView* nullUAVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
        
        // 추가 보장: OMSetRenderTargetsAndUnorderedAccessViews도 확인
        // (Compute shader에서는 사용하지 않지만, 혹시 모를 충돌 방지)
    }

    bool ComputeEffectSystem::CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height)
    {
        // 출력 텍스처 생성
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width              = width;
        texDesc.Height             = height;
        texDesc.MipLevels          = 1;
        texDesc.ArraySize          = 1;
        texDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count   = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags      = 0;

        HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_outputTexture.GetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateTexture2D failed.");
            return false;
        }

        // UAV 생성
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        hr = m_device->CreateUnorderedAccessView(
            m_outputTexture.Get(),
            &uavDesc,
            m_outputUAV.GetAddressOf()
        );
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateUnorderedAccessView failed.");
            return false;
        }

        // SRV 생성 (결과를 읽기 위해)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        hr = m_device->CreateShaderResourceView(
            m_outputTexture.Get(),
            &srvDesc,
            m_outputSRV.GetAddressOf()
        );
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ComputeEffectSystem::CreateUnorderedAccessViews: CreateShaderResourceView failed.");
            return false;
        }

        return true;
    }
}
