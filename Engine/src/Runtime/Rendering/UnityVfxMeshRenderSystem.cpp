#include "Runtime/Rendering/UnityVfxMeshRenderSystem.h"

#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/Foundation/Logger.h"

#include "ThirdParty/json/json.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using Json = nlohmann::json;
using EmitterDef = Alice::UnityVfxMeshRenderSystem::EmitterDef;
using EmitterRuntime = Alice::UnityVfxMeshRenderSystem::EmitterRuntime;
using Particle = Alice::UnityVfxMeshRenderSystem::Particle;
using BurstDef = Alice::UnityVfxMeshRenderSystem::BurstDef;
using BurstRuntime = Alice::UnityVfxMeshRenderSystem::BurstRuntime;
using Curve = Alice::UnityVfxMeshRenderSystem::Curve;
using CurveKey = Alice::UnityVfxMeshRenderSystem::CurveKey;
using MinMaxCurve = Alice::UnityVfxMeshRenderSystem::MinMaxCurve;
using Gradient = Alice::UnityVfxMeshRenderSystem::Gradient;
using GradientKey = Alice::UnityVfxMeshRenderSystem::GradientKey;
using AlphaKey = Alice::UnityVfxMeshRenderSystem::AlphaKey;
using MinMaxGradient = Alice::UnityVfxMeshRenderSystem::MinMaxGradient;

namespace Alice
{
    namespace
    {
        struct VertexPT
        {
            XMFLOAT3 pos;
            XMFLOAT2 uv;
        };

        struct VertexPTC
        {
            XMFLOAT3 pos;
            XMFLOAT2 uv;
            XMFLOAT4 color;
        };

        struct TrailVertex
        {
            XMFLOAT3 pos;
            XMFLOAT2 uv;
            XMFLOAT4 color;
            float birth;
        };

        static bool TryGetVec3(const Json& j, XMFLOAT3& out)
        {
            if (!j.is_array() || j.size() < 3) return false;
            out.x = j[0].get<float>();
            out.y = j[1].get<float>();
            out.z = j[2].get<float>();
            return true;
        }

        static bool TryGetVec4(const Json& j, XMFLOAT4& out)
        {
            if (!j.is_array() || j.size() < 4) return false;
            out.x = j[0].get<float>();
            out.y = j[1].get<float>();
            out.z = j[2].get<float>();
            out.w = j[3].get<float>();
            return true;
        }

        static bool TryGetVec2(const Json& j, XMFLOAT2& out)
        {
            if (!j.is_array() || j.size() < 2) return false;
            out.x = j[0].get<float>();
            out.y = j[1].get<float>();
            return true;
        }

        static std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        static bool StartsWithInsensitive(const std::string& s, const char* prefix)
        {
            const size_t n = std::strlen(prefix);
            if (s.size() < n) return false;
            for (size_t i = 0; i < n; ++i)
            {
                if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
                    return false;
            }
            return true;
        }

        static float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        static float Clamp01(float v)
        {
            if (v < 0.0f) return 0.0f;
            if (v > 1.0f) return 1.0f;
            return v;
        }

        static void ApplyHdrClamp(XMFLOAT4& color, float clamp)
        {
            if (clamp <= 0.0f) return;
            const float maxC = std::max({ color.x, color.y, color.z });
            if (maxC > clamp && maxC > 0.0f)
            {
                const float s = clamp / maxC;
                color.x *= s;
                color.y *= s;
                color.z *= s;
            }
        }

        static Curve ParseCurve(const Json& j)
        {
            Curve c;
            auto itKeys = j.find("keys");
            if (itKeys == j.end() || !itKeys->is_array())
                return c;
            for (const auto& k : *itKeys)
            {
                CurveKey key;
                key.time = k.value("time", 0.0f);
                key.value = k.value("value", 0.0f);
                c.keys.push_back(key);
            }
            std::sort(c.keys.begin(), c.keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
            return c;
        }

        static float EvalCurve(const Curve& c, float t, float def)
        {
            if (c.keys.empty()) return def;
            if (t <= c.keys.front().time) return c.keys.front().value;
            if (t >= c.keys.back().time) return c.keys.back().value;
            for (size_t i = 1; i < c.keys.size(); ++i)
            {
                if (t <= c.keys[i].time)
                {
                    const CurveKey& a = c.keys[i - 1];
                    const CurveKey& b = c.keys[i];
                    const float denom = (b.time - a.time);
                    const float tt = denom > 0.0f ? (t - a.time) / denom : 0.0f;
                    return Lerp(a.value, b.value, tt);
                }
            }
            return c.keys.back().value;
        }

        static MinMaxCurve ParseMinMaxCurve(const Json& j, float defaultValue)
        {
            MinMaxCurve out;
            out.constant = defaultValue;
            out.constantMin = defaultValue;
            out.constantMax = defaultValue;
            if (!j.is_object()) return out;

            std::string mode = ToLower(j.value("mode", "constant"));
            out.multiplier = j.value("multiplier", 1.0f);

            if (mode == "constant")
            {
                out.mode = MinMaxCurve::Mode::Constant;
                out.constant = j.value("constant", defaultValue);
            }
            else if (mode == "twoconstants")
            {
                out.mode = MinMaxCurve::Mode::TwoConstants;
                out.constantMin = j.value("min", defaultValue);
                out.constantMax = j.value("max", defaultValue);
            }
            else if (mode == "curve")
            {
                out.mode = MinMaxCurve::Mode::Curve;
                out.curve = ParseCurve(j.value("curve", Json{}));
            }
            else if (mode == "twocurves")
            {
                out.mode = MinMaxCurve::Mode::TwoCurves;
                out.curveMin = ParseCurve(j.value("curveMin", Json{}));
                out.curveMax = ParseCurve(j.value("curveMax", Json{}));
            }
            return out;
        }

        static float EvalMinMaxCurve(const MinMaxCurve& c, float t01)
        {
            const float t = Clamp01(t01);
            switch (c.mode)
            {
            case MinMaxCurve::Mode::Constant:
                return c.constant * c.multiplier;
            case MinMaxCurve::Mode::TwoConstants:
                return ((c.constantMin + c.constantMax) * 0.5f) * c.multiplier;
            case MinMaxCurve::Mode::Curve:
                return EvalCurve(c.curve, t, c.constant) * c.multiplier;
            case MinMaxCurve::Mode::TwoCurves:
            {
                const float a = EvalCurve(c.curveMin, t, c.constant);
                const float b = EvalCurve(c.curveMax, t, c.constant);
                return ((a + b) * 0.5f) * c.multiplier;
            }
            }
            return c.constant * c.multiplier;
        }

        static Gradient ParseGradient(const Json& j)
        {
            Gradient g;
            auto itColor = j.find("colorKeys");
            if (itColor != j.end() && itColor->is_array())
            {
                for (const auto& k : *itColor)
                {
                    GradientKey key;
                    key.time = k.value("time", 0.0f);
                    if (TryGetVec4(k.value("color", Json{}), key.color))
                        g.colorKeys.push_back(key);
                    else
                    {
                        XMFLOAT3 c3{};
                        if (TryGetVec3(k.value("color", Json{}), c3))
                        {
                            key.color = XMFLOAT4(c3.x, c3.y, c3.z, 1.0f);
                            g.colorKeys.push_back(key);
                        }
                    }
                }
            }
            auto itAlpha = j.find("alphaKeys");
            if (itAlpha != j.end() && itAlpha->is_array())
            {
                for (const auto& k : *itAlpha)
                {
                    AlphaKey key;
                    key.time = k.value("time", 0.0f);
                    key.alpha = k.value("alpha", 1.0f);
                    g.alphaKeys.push_back(key);
                }
            }
            std::sort(g.colorKeys.begin(), g.colorKeys.end(), [](const GradientKey& a, const GradientKey& b) { return a.time < b.time; });
            std::sort(g.alphaKeys.begin(), g.alphaKeys.end(), [](const AlphaKey& a, const AlphaKey& b) { return a.time < b.time; });
            return g;
        }

        static MinMaxGradient ParseMinMaxGradient(const Json& j, const XMFLOAT4& defColor)
        {
            MinMaxGradient out;
            out.color = defColor;
            out.colorMin = defColor;
            out.colorMax = defColor;
            if (!j.is_object()) return out;

            std::string mode = ToLower(j.value("mode", "color"));
            if (mode == "color")
            {
                out.mode = MinMaxGradient::Mode::Color;
                XMFLOAT4 c = defColor;
                if (TryGetVec4(j.value("color", Json{}), c))
                    out.color = c;
                else
                {
                    XMFLOAT3 c3{};
                    if (TryGetVec3(j.value("color", Json{}), c3))
                        out.color = XMFLOAT4(c3.x, c3.y, c3.z, 1.0f);
                }
            }
            else if (mode == "twocolors")
            {
                out.mode = MinMaxGradient::Mode::TwoColors;
                TryGetVec4(j.value("colorMin", Json{}), out.colorMin);
                TryGetVec4(j.value("colorMax", Json{}), out.colorMax);
            }
            else if (mode == "gradient")
            {
                out.mode = MinMaxGradient::Mode::Gradient;
                out.gradient = ParseGradient(j.value("gradient", Json{}));
            }
            else if (mode == "twogradients")
            {
                out.mode = MinMaxGradient::Mode::TwoGradients;
                out.gradientMin = ParseGradient(j.value("gradientMin", Json{}));
                out.gradientMax = ParseGradient(j.value("gradientMax", Json{}));
            }
            return out;
        }

        static XMFLOAT4 EvalGradient(const Gradient& g, float t, const XMFLOAT4& defColor)
        {
            XMFLOAT4 color = defColor;
            if (!g.colorKeys.empty())
            {
                if (t <= g.colorKeys.front().time)
                    color = g.colorKeys.front().color;
                else if (t >= g.colorKeys.back().time)
                    color = g.colorKeys.back().color;
                else
                {
                    for (size_t i = 1; i < g.colorKeys.size(); ++i)
                    {
                        if (t <= g.colorKeys[i].time)
                        {
                            const auto& a = g.colorKeys[i - 1];
                            const auto& b = g.colorKeys[i];
                            const float denom = (b.time - a.time);
                            const float tt = denom > 0.0f ? (t - a.time) / denom : 0.0f;
                            color.x = Lerp(a.color.x, b.color.x, tt);
                            color.y = Lerp(a.color.y, b.color.y, tt);
                            color.z = Lerp(a.color.z, b.color.z, tt);
                            color.w = Lerp(a.color.w, b.color.w, tt);
                            break;
                        }
                    }
                }
            }

            float alpha = color.w;
            if (!g.alphaKeys.empty())
            {
                if (t <= g.alphaKeys.front().time)
                    alpha = g.alphaKeys.front().alpha;
                else if (t >= g.alphaKeys.back().time)
                    alpha = g.alphaKeys.back().alpha;
                else
                {
                    for (size_t i = 1; i < g.alphaKeys.size(); ++i)
                    {
                        if (t <= g.alphaKeys[i].time)
                        {
                            const auto& a = g.alphaKeys[i - 1];
                            const auto& b = g.alphaKeys[i];
                            const float denom = (b.time - a.time);
                            const float tt = denom > 0.0f ? (t - a.time) / denom : 0.0f;
                            alpha = Lerp(a.alpha, b.alpha, tt);
                            break;
                        }
                    }
                }
            }
            color.w = alpha;
            return color;
        }

        static XMFLOAT4 EvalMinMaxGradient(const MinMaxGradient& g, float t, const XMFLOAT4& defColor)
        {
            const float tt = Clamp01(t);
            switch (g.mode)
            {
            case MinMaxGradient::Mode::Color:
                return g.color;
            case MinMaxGradient::Mode::TwoColors:
                return XMFLOAT4(
                    (g.colorMin.x + g.colorMax.x) * 0.5f,
                    (g.colorMin.y + g.colorMax.y) * 0.5f,
                    (g.colorMin.z + g.colorMax.z) * 0.5f,
                    (g.colorMin.w + g.colorMax.w) * 0.5f);
            case MinMaxGradient::Mode::Gradient:
                return EvalGradient(g.gradient, tt, defColor);
            case MinMaxGradient::Mode::TwoGradients:
            {
                XMFLOAT4 a = EvalGradient(g.gradientMin, tt, defColor);
                XMFLOAT4 b = EvalGradient(g.gradientMax, tt, defColor);
                return XMFLOAT4((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f, (a.w + b.w) * 0.5f);
            }
            }
            return defColor;
        }

        static XMMATRIX BuildLocalMatrix(const Json& t, float sizeMul)
        {
            XMFLOAT3 pos{ 0,0,0 };
            XMFLOAT3 scale{ 1,1,1 };
            XMFLOAT4 rot{ 0,0,0,1 };

            TryGetVec3(t.value("pos", Json{}), pos);
            TryGetVec3(t.value("scale", Json{}), scale);
            TryGetVec4(t.value("rot", Json{}), rot);

            XMMATRIX S = XMMatrixScaling(scale.x * sizeMul, scale.y * sizeMul, scale.z * sizeMul);
            XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&rot));
            XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
            return S * R * T;
        }

        static std::string JoinPath(const std::string& base, const std::string& rel)
        {
            if (rel.empty()) return rel;
            std::filesystem::path p(base);
            p /= rel;
            return p.generic_string();
        }

        static XMMATRIX BuildTransformMatrix(const Json& t)
        {
            XMFLOAT3 pos{ 0,0,0 };
            XMFLOAT3 scale{ 1,1,1 };
            XMFLOAT4 rot{ 0,0,0,1 };

            TryGetVec3(t.value("pos", Json{}), pos);
            TryGetVec3(t.value("scale", Json{}), scale);
            TryGetVec4(t.value("rot", Json{}), rot);

            XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
            XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&rot));
            XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
            return S * R * T;
        }

        static float Rand01()
        {
            return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        }

        static XMFLOAT3 RandomInBox(const XMFLOAT3& size)
        {
            return XMFLOAT3(
                (Rand01() - 0.5f) * size.x,
                (Rand01() - 0.5f) * size.y,
                (Rand01() - 0.5f) * size.z);
        }

        static XMFLOAT3 RandomInSphere(float radius)
        {
            const float u = Rand01();
            const float v = Rand01();
            const float theta = 2.0f * XM_PI * u;
            const float phi = std::acos(2.0f * v - 1.0f);
            const float r = radius * std::cbrt(Rand01());
            const float sinPhi = std::sin(phi);
            return XMFLOAT3(
                r * sinPhi * std::cos(theta),
                r * std::cos(phi),
                r * sinPhi * std::sin(theta));
        }

        static XMVECTOR RandomDirectionInCone(float angleDeg)
        {
            const float angleRad = XMConvertToRadians(angleDeg);
            const float u = Rand01();
            const float v = Rand01();
            const float cosTheta = Lerp(std::cos(angleRad), 1.0f, u);
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            const float phi = 2.0f * XM_PI * v;
            return XMVectorSet(
                sinTheta * std::cos(phi),
                cosTheta,
                sinTheta * std::sin(phi),
                0.0f);
        }

        static int RandomRangeInt(int minV, int maxV)
        {
            if (maxV <= minV) return minV;
            const int span = maxV - minV + 1;
            return minV + (std::rand() % span);
        }

        constexpr size_t kMaxParticlesPerEmitter = 2048;
        constexpr size_t kMaxParticlesPerEffect = 20000;

        struct CBTrailVS
        {
            XMMATRIX viewProj;
            float currentTime{ 0.0f };
            XMFLOAT3 padding{ 0.0f, 0.0f, 0.0f };
        };

        struct CBTrailPS
        {
            XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
            float fadeDuration{ 1.0f };
            XMFLOAT3 padding{ 0.0f, 0.0f, 0.0f };
        };
    }

    UnityVfxMeshRenderSystem::UnityVfxMeshRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    bool UnityVfxMeshRenderSystem::Initialize()
    {
        if (!m_device || !m_context) return false;
        if (!CreateShadersAndInputLayout()) return false;
        if (!CreateTrailShaders()) return false;
        if (!CreateStates()) return false;
        if (!CreateDefaultTexture()) return false;
        if (!CreateQuadMesh()) return false;

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CBPerObject);
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    void UnityVfxMeshRenderSystem::Render(const World& world, const Camera& camera, float dtSec)
    {
        if (!m_vs || !m_ps || !m_inputLayout) return;

        m_timeSec += dtSec;

        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();
        XMMATRIX viewProj = view * proj;
        XMMATRIX invView = XMMatrixInverse(nullptr, view);
        XMVECTOR camRight = XMVector3Normalize(invView.r[0]);
        XMVECTOR camUp = XMVector3Normalize(invView.r[1]);
        XMVECTOR camForward = XMVector3Normalize(invView.r[2]);
        const XMFLOAT3& camPosF = camera.GetPosition();
        XMVECTOR camPos = XMLoadFloat3(&camPosF);

        auto BindParticlePipeline = [&]()
        {
            m_context->IASetInputLayout(m_inputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_vs.Get(), nullptr, 0);
            m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        };

        BindParticlePipeline();

        float blendFactor[4] = { 0,0,0,0 };
        m_context->OMSetBlendState(m_blendAdd.Get(), blendFactor, 0xffffffff);
        m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
        m_context->RSSetState(m_rasterState.Get());

        ID3D11SamplerState* samplers[] = { m_sampler.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        const auto& vfxMap = world.GetComponents<UnityVfxComponent>();
        std::unordered_set<uint64_t> aliveIds;
        aliveIds.reserve(vfxMap.size());
        for (const auto& [entityId, vfx] : vfxMap)
        {
            aliveIds.insert(static_cast<uint64_t>(entityId));
            if (!vfx.enabled || vfx.effectPath.empty())
                continue;
            if (!vfx.useMeshRenderer)
                continue;

            const auto* tr = world.GetComponent<TransformComponent>(entityId);
            if (!tr || !tr->enabled || !tr->visible)
                continue;

            const EffectCache* cache = GetEffectCache(vfx.effectPath);
            if (!cache || !cache->valid)
                continue;

            XMMATRIX entityWorld = world.ComputeWorldMatrix(entityId);
            XMMATRIX entityWorldNoTrans = entityWorld;
            entityWorldNoTrans.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
            const float entityScale =
                (XMVectorGetX(XMVector3Length(entityWorld.r[0])) +
                 XMVectorGetX(XMVector3Length(entityWorld.r[1])) +
                 XMVectorGetX(XMVector3Length(entityWorld.r[2]))) / 3.0f;
            std::filesystem::path baseDir = std::filesystem::path(vfx.effectPath).parent_path();
            std::string baseDirStr = baseDir.generic_string();

            EffectRuntime& runtime = m_runtimeCache[static_cast<uint64_t>(entityId)];
            if (runtime.effectPath != vfx.effectPath ||
                runtime.emitters.size() != cache->emitters.size() ||
                runtime.playId != vfx.playId)
            {
                runtime.effectPath = vfx.effectPath;
                runtime.playId = vfx.playId;
                runtime.emitters.clear();
                runtime.emitters.resize(cache->emitters.size());
                for (size_t i = 0; i < cache->emitters.size(); ++i)
                {
                    EmitterRuntime& rt = runtime.emitters[i];
                    rt.time = 0.0f;
                    rt.spawnAccum = 0.0f;
                    rt.particles.clear();
                    rt.bursts.clear();
                    for (const auto& b : cache->emitters[i].bursts)
                    {
                        BurstRuntime br;
                        br.nextTime = b.time;
                        br.remaining = b.cycleCount;
                        br.repeatInterval = b.repeatInterval;
                        rt.bursts.push_back(br);
                    }
                }
            }

            size_t totalParticles = 0;
            for (const auto& er : runtime.emitters)
                totalParticles += er.particles.size();

            const float dtComponent = std::max(0.0f, dtSec * std::max(0.0f, vfx.timeScale));
            const float timeSec = m_timeSec * std::max(0.0f, vfx.timeScale);

            for (size_t i = 0; i < cache->emitters.size(); ++i)
            {
                const EmitterDef& def = cache->emitters[i];
                EmitterRuntime& rt = runtime.emitters[i];
                const bool trailsEnabled = def.trails.enabled && vfx.enableTrails;
                const float trailLifetime = trailsEnabled
                    ? std::max(0.001f, def.trails.lifetime * std::max(0.01f, vfx.trailLifeScale))
                    : 0.0f;

                const float scaleAvg = (def.localScale.x + def.localScale.y + def.localScale.z) / 3.0f;
                const float dt = dtComponent;

                const bool loopEnabled = vfx.overrideLoop ? vfx.loop : def.loop;
                const bool canSpawn = loopEnabled || rt.time < def.duration;
                bool looped = false;
                rt.time += dt;
                if (def.duration > 0.0f && rt.time >= def.duration)
                {
                    if (loopEnabled)
                    {
                        rt.time = std::fmod(rt.time, def.duration);
                        looped = true;
                    }
                    else
                    {
                        rt.time = def.duration;
                    }
                }

                if (looped)
                {
                    rt.spawnAccum = 0.0f;
                    rt.bursts.clear();
                    for (const auto& b : def.bursts)
                    {
                        BurstRuntime br;
                        br.nextTime = b.time;
                        br.remaining = b.cycleCount;
                        br.repeatInterval = b.repeatInterval;
                        rt.bursts.push_back(br);
                    }
                }

                auto spawnOne = [&]()
                {
                    if (rt.particles.size() >= kMaxParticlesPerEmitter) return;
                    if (totalParticles >= kMaxParticlesPerEffect) return;

                    Particle p;
                    p.lifetime = std::max(0.01f, EvalMinMaxCurve(def.startLifetime, 0.0f) * std::max(0.01f, vfx.lifetimeScale));
                    p.age = 0.0f;
                    p.baseSize = EvalMinMaxCurve(def.startSize, 0.0f) * vfx.sizeScale * scaleAvg;
                    p.size = p.baseSize;
                    p.baseColor = EvalMinMaxGradient(def.startColor, 0.0f, XMFLOAT4(1, 1, 1, 1));
                    p.baseColor.x *= vfx.colorScale;
                    p.baseColor.y *= vfx.colorScale;
                    p.baseColor.z *= vfx.colorScale;
                    p.baseColor.w *= vfx.alphaScale;
                    p.color = p.baseColor;
                    p.baseRotation3 = XMFLOAT3(0.0f, 0.0f, 0.0f);
                    if (def.startRotation3D)
                    {
                        p.baseRotation3.x = EvalMinMaxCurve(def.startRotationX, 0.0f);
                        p.baseRotation3.y = EvalMinMaxCurve(def.startRotationY, 0.0f);
                        p.baseRotation3.z = EvalMinMaxCurve(def.startRotationZ, 0.0f);
                    }
                    else
                    {
                        p.baseRotation3.z = EvalMinMaxCurve(def.startRotation, 0.0f);
                    }
                    p.rotation3 = p.baseRotation3;

                    XMVECTOR dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                    XMVECTOR pos = XMVectorZero();
                    if (def.shapeEnabled)
                    {
                        switch (def.shape.type)
                        {
                        case ShapeType::Box:
                        {
                            XMFLOAT3 rnd = RandomInBox(def.shape.box);
                            pos = XMLoadFloat3(&rnd);
                            dir = XMVector3Normalize(pos);
                            break;
                        }
                        case ShapeType::Sphere:
                        {
                            XMFLOAT3 rnd = RandomInSphere(def.shape.radius);
                            pos = XMLoadFloat3(&rnd);
                            dir = XMVector3Normalize(pos);
                            break;
                        }
                        case ShapeType::Cone:
                        default:
                        {
                            XMVECTOR coneDir = RandomDirectionInCone(def.shape.angleDeg);
                            const float h = def.shape.length * Rand01();
                            const float r = def.shape.radius * (def.shape.length > 0.0f ? (h / def.shape.length) : 1.0f);
                            const float theta = 2.0f * XM_PI * Rand01();
                            pos = XMVectorSet(std::cos(theta) * r, h, std::sin(theta) * r, 0.0f);
                            dir = def.shape.alignToDirection ? XMVector3Normalize(pos) : coneDir;
                            break;
                        }
                        }
                    }

                    XMVECTOR shapeOffset = XMLoadFloat3(&def.shape.position);
                    pos = pos + shapeOffset;

                    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6f)
                        dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

                    XMMATRIX shapeRot = XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(def.shape.rotation.x),
                        XMConvertToRadians(def.shape.rotation.y),
                        XMConvertToRadians(def.shape.rotation.z));
                    pos = XMVector3TransformCoord(pos, shapeRot);
                    dir = XMVector3Normalize(XMVector3TransformNormal(dir, shapeRot));

                    XMFLOAT3 pos3{};
                    XMStoreFloat3(&pos3, pos);
                    pos3.x *= def.localScale.x;
                    pos3.y *= def.localScale.y;
                    pos3.z *= def.localScale.z;
                    pos = XMLoadFloat3(&pos3);

                    XMMATRIX emitterRot = XMMatrixRotationQuaternion(XMLoadFloat4(&def.localRot));
                    pos = XMVector3TransformCoord(pos, emitterRot);
                    dir = XMVector3Normalize(XMVector3TransformNormal(dir, emitterRot));

                    XMVECTOR emitterPos = XMLoadFloat3(&def.localPos);
                    pos = pos + emitterPos;

                    if (def.space == SimulationSpace::World)
                    {
                        pos = XMVector3TransformCoord(pos, entityWorld);
                        dir = XMVector3Normalize(XMVector3TransformNormal(dir, entityWorldNoTrans));
                    }

                    XMStoreFloat3(&p.pos, pos);
                    const float speed = EvalMinMaxCurve(def.startSpeed, 0.0f) * vfx.speedScale;
                    XMStoreFloat3(&p.baseVel, dir * speed);
                    p.vel = p.baseVel;
                    if (trailsEnabled)
                    {
                        Particle::TrailPoint tp;
                        tp.pos = p.pos;
                        tp.age = 0.0f;
                        p.trail.clear();
                        p.trail.push_back(tp);
                        p.trailAccumDist = 0.0f;
                    }

                    rt.particles.push_back(p);
                    totalParticles++;
                };

                if (canSpawn)
                {
                    const float rate = def.rateOverTime * vfx.spawnRateScale;
                    if (rate > 0.0f)
                    {
                        rt.spawnAccum += rate * dt;
                        int spawnCount = static_cast<int>(rt.spawnAccum);
                        if (spawnCount > 0)
                        {
                            rt.spawnAccum -= spawnCount;
                            for (int s = 0; s < spawnCount; ++s)
                                spawnOne();
                        }
                    }

                    for (size_t bi = 0; bi < rt.bursts.size(); ++bi)
                    {
                        BurstRuntime& br = rt.bursts[bi];
                        const BurstDef& bd = def.bursts[bi];
                        while (br.remaining > 0 && rt.time >= br.nextTime)
                        {
                            if (bd.probability < 1.0f && Rand01() > bd.probability)
                            {
                                br.remaining--;
                                if (br.repeatInterval <= 0.0f)
                                    break;
                                br.nextTime += br.repeatInterval;
                                continue;
                            }

                            int count = 0;
                            if (bd.minCount == 0 && bd.maxCount <= 1)
                                count = bd.maxCount;
                            else
                                count = RandomRangeInt(bd.minCount, bd.maxCount);
                            count = static_cast<int>(count * vfx.spawnRateScale);
                            for (int s = 0; s < count; ++s)
                                spawnOne();
                            br.remaining--;
                            if (br.repeatInterval <= 0.0f)
                                break;
                            br.nextTime += br.repeatInterval;
                        }
                    }
                }

                for (size_t pIndex = 0; pIndex < rt.particles.size();)
                {
                    Particle& p = rt.particles[pIndex];
                    p.age += dt;
                    const bool expired = p.age >= p.lifetime;

                    if (trailsEnabled)
                    {
                        for (auto& tp : p.trail)
                            tp.age += dt;
                        if (!p.trail.empty())
                        {
                            p.trail.erase(
                                std::remove_if(p.trail.begin(), p.trail.end(),
                                    [&](const Particle::TrailPoint& tp) { return tp.age > trailLifetime; }),
                                p.trail.end());
                        }
                    }

                    if (expired)
                    {
                        if (!trailsEnabled || p.trail.empty())
                        {
                            rt.particles[pIndex] = rt.particles.back();
                            rt.particles.pop_back();
                            continue;
                        }
                        ++pIndex;
                        continue;
                    }

                    const float age01 = p.lifetime > 0.0f ? (p.age / p.lifetime) : 0.0f;

                    XMFLOAT3 velAdd{ 0.0f, 0.0f, 0.0f };
                    if (def.velocityOverLifetime.enabled)
                    {
                        velAdd.x = EvalMinMaxCurve(def.velocityOverLifetime.x, age01);
                        velAdd.y = EvalMinMaxCurve(def.velocityOverLifetime.y, age01);
                        velAdd.z = EvalMinMaxCurve(def.velocityOverLifetime.z, age01);
                        XMVECTOR v = XMLoadFloat3(&velAdd);
                        if (def.velocityOverLifetime.space == SimulationSpace::Local)
                        {
                            XMMATRIX rot = XMMatrixRotationQuaternion(XMLoadFloat4(&def.localRot));
                            if (def.space == SimulationSpace::World)
                                rot = rot * entityWorldNoTrans;
                            v = XMVector3TransformNormal(v, rot);
                        }
                        else if (def.space == SimulationSpace::Local)
                        {
                            XMMATRIX inv = XMMatrixInverse(nullptr, entityWorldNoTrans);
                            v = XMVector3TransformNormal(v, inv);
                        }
                        XMStoreFloat3(&velAdd, v);
                    }

                    p.vel.x = p.baseVel.x + velAdd.x;
                    p.vel.y = p.baseVel.y + velAdd.y;
                    p.vel.z = p.baseVel.z + velAdd.z;

                    p.pos.x += p.vel.x * dt;
                    p.pos.y += p.vel.y * dt;
                    p.pos.z += p.vel.z * dt;

                    float sizeMul = 1.0f;
                    if (def.sizeOverLifetimeEnabled)
                        sizeMul = EvalMinMaxCurve(def.sizeOverLifetime, age01);
                    p.size = p.baseSize * sizeMul;

                    XMFLOAT4 col = p.baseColor;
                    if (def.colorOverLifetimeEnabled)
                    {
                        XMFLOAT4 over = EvalMinMaxGradient(def.colorOverLifetime, age01, XMFLOAT4(1, 1, 1, 1));
                        col.x *= over.x;
                        col.y *= over.y;
                        col.z *= over.z;
                        col.w *= over.w;
                    }
                    col.x *= vfx.colorTint.x;
                    col.y *= vfx.colorTint.y;
                    col.z *= vfx.colorTint.z;
                    p.color = col;

                    if (def.rotationOverLifetime.enabled)
                    {
                        if (def.rotationOverLifetime.separateAxes)
                        {
                            p.rotation3.x = p.baseRotation3.x + EvalMinMaxCurve(def.rotationOverLifetime.x, age01);
                            p.rotation3.y = p.baseRotation3.y + EvalMinMaxCurve(def.rotationOverLifetime.y, age01);
                            p.rotation3.z = p.baseRotation3.z + EvalMinMaxCurve(def.rotationOverLifetime.z, age01);
                        }
                        else
                        {
                            p.rotation3 = p.baseRotation3;
                            p.rotation3.z = p.baseRotation3.z + EvalMinMaxCurve(def.rotationOverLifetime.z, age01);
                        }
                    }
                    else
                    {
                        p.rotation3 = p.baseRotation3;
                    }

                    if (trailsEnabled)
                    {
                        if (p.trail.empty())
                        {
                            Particle::TrailPoint tp;
                            tp.pos = p.pos;
                            tp.age = 0.0f;
                            p.trail.push_back(tp);
                            p.trailAccumDist = 0.0f;
                        }
                        else
                        {
                            XMVECTOR cur = XMLoadFloat3(&p.pos);
                            XMVECTOR last = XMLoadFloat3(&p.trail.back().pos);
                            const float dist = XMVectorGetX(XMVector3Length(cur - last));
                            const float minDist = std::max(0.001f, def.trails.minVertexDistance);
                            p.trailAccumDist += dist;
                            if (p.trailAccumDist >= minDist)
                            {
                                Particle::TrailPoint tp;
                                tp.pos = p.pos;
                                tp.age = 0.0f;
                                p.trail.push_back(tp);
                                p.trailAccumDist = 0.0f;
                            }
                        }
                    }

                    ++pIndex;
                }

                if (rt.particles.empty())
                    continue;

                MaterialGpu mat{};
                if (!def.materialPath.empty())
                {
                    if (!EnsureMaterialLoaded(baseDirStr, def.materialPath, mat))
                        mat.texture = m_defaultTexture;
                }
                else
                {
                    mat.texture = m_defaultTexture;
                }

                XMFLOAT4 matColor = mat.color;
                matColor.x *= vfx.colorScale * vfx.intensityScale;
                matColor.y *= vfx.colorScale * vfx.intensityScale;
                matColor.z *= vfx.colorScale * vfx.intensityScale;
                matColor.w *= vfx.alphaScale;
                ApplyHdrClamp(matColor, vfx.hdrColorClamp);

                const float dissolve = mat.dissolve + vfx.dissolveOffset;
                const float noiseStrength = mat.noiseStrength * vfx.noiseScale;
                const float rampStrength = mat.rampStrength * vfx.rampScale;
                const XMFLOAT2 uvScroll{ mat.uvScroll.x * vfx.uvScrollScale, mat.uvScroll.y * vfx.uvScrollScale };

                if (mat.blendMode == BlendMode::Alpha)
                    m_context->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xffffffff);
                else
                    m_context->OMSetBlendState(m_blendAdd.Get(), blendFactor, 0xffffffff);

                ID3D11ShaderResourceView* srvs[] =
                {
                    mat.texture.Get() ? mat.texture.Get() : m_defaultTexture.Get(),
                    mat.noiseTexture.Get() ? mat.noiseTexture.Get() : m_defaultTexture.Get(),
                    mat.rampTexture.Get() ? mat.rampTexture.Get() : m_defaultTexture.Get()
                };
                m_context->PSSetShaderResources(0, 3, srvs);

                const bool useMesh = def.meshRenderer && !def.meshPath.empty();
                MeshGpu mesh{};
                if (useMesh && !EnsureMeshLoaded(def.meshPath, mesh))
                    continue;

                if (!useMesh)
                {
                    if (!m_billboardVS || !m_billboardLayout)
                        continue;

                    std::vector<VertexPTC> verts;
                    verts.reserve(rt.particles.size() * 6);

                    const XMFLOAT2 baseUv[4] =
                    {
                        { 0.0f, 1.0f },
                        { 0.0f, 0.0f },
                        { 1.0f, 0.0f },
                        { 1.0f, 1.0f }
                    };
                    const int quadIdx[6] = { 0,1,2, 0,2,3 };

                    for (const Particle& p : rt.particles)
                    {
                        XMVECTOR worldPos = XMLoadFloat3(&p.pos);
                        if (def.space == SimulationSpace::Local)
                            worldPos = XMVector3TransformCoord(worldPos, entityWorld);

                        XMFLOAT2 uvScale = mat.uvScale;
                        XMFLOAT2 uvOffset = mat.uvOffset;
                        if (def.texSheet.enabled && def.texSheet.tilesX > 0 && def.texSheet.tilesY > 0)
                        {
                            const int totalFrames = def.texSheet.tilesX * def.texSheet.tilesY;
                            float frame01 = EvalMinMaxCurve(def.texSheet.frameOverTime, p.lifetime > 0.0f ? (p.age / p.lifetime) : 0.0f);
                            frame01 = frame01 - std::floor(frame01);
                            float startFrame = EvalMinMaxCurve(def.texSheet.startFrame, 0.0f);
                            const int cycleCount = def.texSheet.cycleCount > 0 ? def.texSheet.cycleCount : 1;
                            int frame = static_cast<int>((frame01 * cycleCount * totalFrames) + startFrame);
                            if (totalFrames > 0)
                                frame = (frame % totalFrames + totalFrames) % totalFrames;
                            int x = frame % def.texSheet.tilesX;
                            int y = frame / def.texSheet.tilesX;
                            const float tileW = 1.0f / static_cast<float>(def.texSheet.tilesX);
                            const float tileH = 1.0f / static_cast<float>(def.texSheet.tilesY);
                            uvScale.x *= tileW;
                            uvScale.y *= tileH;
                            uvOffset.x += tileW * static_cast<float>(x);
                            uvOffset.y += tileH * static_cast<float>(y);
                        }

                        const float renderSize = p.size * ((def.space == SimulationSpace::Local) ? entityScale : 1.0f);

                        XMVECTOR right = camRight;
                        XMVECTOR up = camUp;
                        XMVECTOR forward = camForward;

                        if (def.renderMode == BillboardMode::Horizontal)
                        {
                            XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                            right = XMVector3Normalize(XMVector3Cross(worldUp, camForward));
                            up = worldUp;
                            forward = XMVector3Normalize(XMVector3Cross(right, up));
                        }
                        else if (def.renderMode == BillboardMode::Stretch)
                        {
                            XMVECTOR vel = XMLoadFloat3(&p.vel);
                            const float velLenSq = XMVectorGetX(XMVector3LengthSq(vel));
                            if (velLenSq > 1e-6f)
                                up = XMVector3Normalize(vel);
                            right = XMVector3Normalize(XMVector3Cross(up, camForward));
                            forward = XMVector3Normalize(XMVector3Cross(right, up));
                        }

                        if (std::abs(p.rotation3.z) > 1e-5f)
                        {
                            XMMATRIX rot = XMMatrixRotationAxis(forward, p.rotation3.z);
                            right = XMVector3TransformNormal(right, rot);
                            up = XMVector3TransformNormal(up, rot);
                        }

                        float sizeX = renderSize;
                        float sizeY = renderSize;
                        if (def.renderMode == BillboardMode::Stretch)
                            sizeY *= (def.stretchScale > 0.0f ? def.stretchScale : 2.0f);

                        XMVECTOR rightS = right * sizeX;
                        XMVECTOR upS = up * sizeY;

                        XMVECTOR corner[4] =
                        {
                            worldPos - rightS - upS,
                            worldPos - rightS + upS,
                            worldPos + rightS + upS,
                            worldPos + rightS - upS
                        };

                        XMFLOAT4 color(
                            p.color.x,
                            p.color.y,
                            p.color.z,
                            p.color.w);

                        XMFLOAT3 cPos[4];
                        for (int i = 0; i < 4; ++i)
                            XMStoreFloat3(&cPos[i], corner[i]);

                        for (int i = 0; i < 6; ++i)
                        {
                            const int qi = quadIdx[i];
                            XMFLOAT2 uv(
                                baseUv[qi].x * uvScale.x + uvOffset.x,
                                baseUv[qi].y * uvScale.y + uvOffset.y);
                            verts.push_back({ cPos[qi], uv, color });
                        }
                    }

                    if (!verts.empty() && EnsureBillboardVertexBuffer(verts.size()))
                    {
                        D3D11_MAPPED_SUBRESOURCE mapped = {};
                        if (SUCCEEDED(m_context->Map(m_billboardVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                        {
                            std::memcpy(mapped.pData, verts.data(), verts.size() * sizeof(VertexPTC));
                            m_context->Unmap(m_billboardVB.Get(), 0);

                            CBPerObject cb{};
                            cb.world = XMMatrixIdentity();
                            cb.viewProj = XMMatrixTranspose(viewProj);
                            cb.color = matColor;
                            cb.uvParams = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
                            cb.customParams = XMFLOAT4(dissolve, noiseStrength, rampStrength, timeSec);
                            cb.uvAnim = XMFLOAT4(uvScroll.x, uvScroll.y, 0.0f, 0.0f);

                            m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &cb, 0, 0);
                            ID3D11Buffer* cbuffers[] = { m_cbPerObject.Get() };
                            m_context->VSSetConstantBuffers(0, 1, cbuffers);
                            m_context->PSSetConstantBuffers(0, 1, cbuffers);

                            m_context->IASetInputLayout(m_billboardLayout.Get());
                            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                            m_context->VSSetShader(m_billboardVS.Get(), nullptr, 0);
                            m_context->PSSetShader(m_ps.Get(), nullptr, 0);

                            UINT stride = sizeof(VertexPTC);
                            UINT offset = 0;
                            ID3D11Buffer* vb = m_billboardVB.Get();
                            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                            m_context->Draw(static_cast<UINT>(verts.size()), 0);

                            BindParticlePipeline();
                        }
                    }
                }
                else
                {
                    for (const Particle& p : rt.particles)
                    {
                        XMVECTOR worldPos = XMLoadFloat3(&p.pos);
                        if (def.space == SimulationSpace::Local)
                            worldPos = XMVector3TransformCoord(worldPos, entityWorld);

                        XMFLOAT2 uvScale = mat.uvScale;
                        XMFLOAT2 uvOffset = mat.uvOffset;
                        if (def.texSheet.enabled && def.texSheet.tilesX > 0 && def.texSheet.tilesY > 0)
                        {
                            const int totalFrames = def.texSheet.tilesX * def.texSheet.tilesY;
                            float frame01 = EvalMinMaxCurve(def.texSheet.frameOverTime, p.lifetime > 0.0f ? (p.age / p.lifetime) : 0.0f);
                            frame01 = frame01 - std::floor(frame01);
                            float startFrame = EvalMinMaxCurve(def.texSheet.startFrame, 0.0f);
                            const int cycleCount = def.texSheet.cycleCount > 0 ? def.texSheet.cycleCount : 1;
                            int frame = static_cast<int>((frame01 * cycleCount * totalFrames) + startFrame);
                            if (totalFrames > 0)
                                frame = (frame % totalFrames + totalFrames) % totalFrames;
                            int x = frame % def.texSheet.tilesX;
                            int y = frame / def.texSheet.tilesX;
                            const float tileW = 1.0f / static_cast<float>(def.texSheet.tilesX);
                            const float tileH = 1.0f / static_cast<float>(def.texSheet.tilesY);
                            uvScale.x *= tileW;
                            uvScale.y *= tileH;
                            uvOffset.x += tileW * static_cast<float>(x);
                            uvOffset.y += tileH * static_cast<float>(y);
                        }

                        CBPerObject cb{};
                        cb.viewProj = XMMatrixTranspose(viewProj);
                        cb.color = XMFLOAT4(
                            p.color.x * matColor.x,
                            p.color.y * matColor.y,
                            p.color.z * matColor.z,
                            p.color.w * matColor.w);
                        cb.uvParams = XMFLOAT4(uvScale.x, uvScale.y, uvOffset.x, uvOffset.y);
                        cb.customParams = XMFLOAT4(dissolve, noiseStrength, rampStrength, timeSec);
                        cb.uvAnim = XMFLOAT4(uvScroll.x, uvScroll.y, 0.0f, 0.0f);

                        const float renderSize = p.size * ((def.space == SimulationSpace::Local) ? entityScale : 1.0f);

                        XMMATRIX S = XMMatrixScaling(renderSize, renderSize, renderSize);
                        XMMATRIX RParticle = XMMatrixRotationRollPitchYaw(p.rotation3.x, p.rotation3.y, p.rotation3.z);
                        XMMATRIX REmitter = XMMatrixRotationQuaternion(XMLoadFloat4(&def.localRot));
                        XMMATRIX R = RParticle * REmitter;
                        XMMATRIX T = XMMatrixTranslationFromVector(worldPos);
                        cb.world = XMMatrixTranspose(S * R * T);

                        m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &cb, 0, 0);
                        ID3D11Buffer* cbuffers[] = { m_cbPerObject.Get() };
                        m_context->VSSetConstantBuffers(0, 1, cbuffers);
                        m_context->PSSetConstantBuffers(0, 1, cbuffers);

                        UINT stride = sizeof(VertexPT);
                        UINT offset = 0;
                        ID3D11Buffer* vb = mesh.vb.Get();
                        ID3D11Buffer* ib = mesh.ib.Get();
                        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                        m_context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
                        m_context->DrawIndexed(mesh.indexCount, 0, 0);
                    }
                }

                if (trailsEnabled && m_trailVS && m_trailPS && m_trailLayout && m_cbTrailVS && m_cbTrailPS)
                {
                    bool hasTrail = false;
                    for (const Particle& p : rt.particles)
                    {
                        if (p.trail.size() >= 2)
                        {
                            hasTrail = true;
                            break;
                        }
                    }

                    if (hasTrail)
                    {
                        m_context->IASetInputLayout(m_trailLayout.Get());
                        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                        m_context->VSSetShader(m_trailVS.Get(), nullptr, 0);
                        m_context->PSSetShader(m_trailPS.Get(), nullptr, 0);

                        if (mat.blendMode == BlendMode::Alpha)
                            m_context->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xffffffff);
                        else
                            m_context->OMSetBlendState(m_blendAdd.Get(), blendFactor, 0xffffffff);

                        ID3D11ShaderResourceView* trailSrv = mat.texture ? mat.texture.Get() : m_defaultTexture.Get();
                        m_context->PSSetShaderResources(0, 1, &trailSrv);

                        CBTrailVS cbVs{};
                        cbVs.viewProj = XMMatrixTranspose(viewProj);
                        cbVs.currentTime = timeSec;
                        m_context->UpdateSubresource(m_cbTrailVS.Get(), 0, nullptr, &cbVs, 0, 0);
                        ID3D11Buffer* cbVsBuffers[] = { m_cbTrailVS.Get() };
                        m_context->VSSetConstantBuffers(0, 1, cbVsBuffers);

                        CBTrailPS cbPs{};
                        cbPs.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
                        cbPs.fadeDuration = trailLifetime;
                        m_context->UpdateSubresource(m_cbTrailPS.Get(), 0, nullptr, &cbPs, 0, 0);
                        ID3D11Buffer* cbPsBuffers[] = { m_cbTrailPS.Get() };
                        m_context->PSSetConstantBuffers(1, 1, cbPsBuffers);

                        for (const Particle& p : rt.particles)
                        {
                            const size_t count = p.trail.size();
                            if (count < 2)
                                continue;

                            std::vector<XMFLOAT3> worldPts;
                            worldPts.reserve(count);
                            for (const auto& tp : p.trail)
                            {
                                XMVECTOR wp = XMLoadFloat3(&tp.pos);
                                if (def.space == SimulationSpace::Local)
                                    wp = XMVector3TransformCoord(wp, entityWorld);
                                XMFLOAT3 out{};
                                XMStoreFloat3(&out, wp);
                                worldPts.push_back(out);
                            }

                            std::vector<TrailVertex> verts;
                            verts.reserve(count * 2);

                            const float widthScale = (def.space == SimulationSpace::Local) ? entityScale : 1.0f;

                            for (size_t ti = 0; ti < count; ++ti)
                            {
                                const auto& tp = p.trail[ti];
                                const float t = trailLifetime > 0.0f ? Clamp01(tp.age / trailLifetime) : 0.0f;
                                const float u = t;
                                const float width = EvalMinMaxCurve(def.trails.widthOverTrail, t) * p.baseSize * widthScale * std::max(0.01f, vfx.trailWidthScale);

                                XMFLOAT4 grad = EvalMinMaxGradient(def.trails.colorOverTrail, t, XMFLOAT4(1, 1, 1, 1));
                                XMFLOAT4 col{
                                    p.color.x * matColor.x * grad.x,
                                    p.color.y * matColor.y * grad.y,
                                    p.color.z * matColor.z * grad.z,
                                    p.color.w * matColor.w * grad.w
                                };

                                XMVECTOR pos = XMLoadFloat3(&worldPts[ti]);
                                XMVECTOR prev = XMLoadFloat3(&worldPts[(ti > 0) ? (ti - 1) : ti]);
                                XMVECTOR next = XMLoadFloat3(&worldPts[(ti + 1 < count) ? (ti + 1) : ti]);
                                XMVECTOR dir = next - prev;
                                if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6f)
                                    dir = camForward;
                                else
                                    dir = XMVector3Normalize(dir);

                                XMVECTOR viewDir = XMVector3Normalize(camPos - pos);
                                XMVECTOR side = XMVector3Cross(dir, viewDir);
                                if (XMVectorGetX(XMVector3LengthSq(side)) < 1e-6f)
                                    side = XMVector3Cross(dir, camForward);
                                side = XMVector3Normalize(side);

                                XMVECTOR offset = side * (width * 0.5f);
                                XMFLOAT3 left{};
                                XMFLOAT3 right{};
                                XMStoreFloat3(&left, pos - offset);
                                XMStoreFloat3(&right, pos + offset);

                                const float birth = timeSec - tp.age;
                                verts.push_back({ left, XMFLOAT2(u, 0.0f), col, birth });
                                verts.push_back({ right, XMFLOAT2(u, 1.0f), col, birth });
                            }

                            if (!EnsureTrailVertexBuffer(verts.size()))
                                continue;

                            D3D11_MAPPED_SUBRESOURCE mapped = {};
                            if (SUCCEEDED(m_context->Map(m_trailVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                            {
                                std::memcpy(mapped.pData, verts.data(), verts.size() * sizeof(TrailVertex));
                                m_context->Unmap(m_trailVB.Get(), 0);

                                UINT stride = sizeof(TrailVertex);
                                UINT offset = 0;
                                ID3D11Buffer* vb = m_trailVB.Get();
                                m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                                m_context->Draw(static_cast<UINT>(verts.size()), 0);
                            }
                        }

                        BindParticlePipeline();
                    }
                }
            }
        }

        if (!m_runtimeCache.empty())
        {
            for (auto it = m_runtimeCache.begin(); it != m_runtimeCache.end(); )
            {
                if (aliveIds.find(it->first) == aliveIds.end())
                    it = m_runtimeCache.erase(it);
                else
                    ++it;
            }
        }

        // restore defaults
        m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        m_context->OMSetDepthStencilState(nullptr, 0);
        m_context->RSSetState(nullptr);
    }

    bool UnityVfxMeshRenderSystem::CreateShadersAndInputLayout()
    {
        static const char* vsSrc = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4   gColor;
    float4   gUvParams; // xy = scale, zw = offset
    float4   gCustomParams; // x=dissolve y=noiseStrength z=rampStrength w=time
    float4   gUvAnim; // xy = scroll
};

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    float4 worldPos = mul(float4(input.position, 1.0f), gWorld);
    o.position = mul(worldPos, gViewProj);
    o.uv = input.uv;
    o.color = gColor;
    return o;
}
)";

        static const char* psSrc = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4   gColor;
    float4   gUvParams; // xy = scale, zw = offset
    float4   gCustomParams; // x=dissolve y=noiseStrength z=rampStrength w=time
    float4   gUvAnim; // xy = scroll
};

Texture2D gTex : register(t0);
Texture2D gNoiseTex : register(t1);
Texture2D gRampTex : register(t2);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.uv * gUvParams.xy + gUvParams.zw;
    uv += gUvAnim.xy * gCustomParams.w;
    float4 tex = gTex.Sample(gSampler, uv);
    float4 noise = gNoiseTex.Sample(gSampler, uv);
    float4 ramp = gRampTex.Sample(gSampler, float2(tex.r, 0.5f));

    float dissolve = gCustomParams.x;
    float noiseStrength = gCustomParams.y;
    float rampStrength = gCustomParams.z;

    float noiseVal = lerp(1.0f, noise.r, noiseStrength);
    float alpha = tex.a * input.color.a;
    alpha *= saturate(noiseVal + dissolve);

    float3 color = tex.rgb * input.color.rgb;
    color *= lerp(1.0f, ramp.rgb, rampStrength);
    return float4(color, alpha);
}
)";

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> vsBatchBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errBlob;

        if (FAILED(D3DCompile(vsSrc, std::strlen(vsSrc), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf())))
        {
            if (errBlob) ALICE_LOG_ERRORF("[UnityVfxMesh] VS compile error: %s", (char*)errBlob->GetBufferPointer());
            return false;
        }

        static const char* vsBatchSrc = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4   gColor;
    float4   gUvParams; // xy = scale, zw = offset
    float4   gCustomParams; // x=dissolve y=noiseStrength z=rampStrength w=time
    float4   gUvAnim; // xy = scroll
};

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    float4 worldPos = mul(float4(input.position, 1.0f), gWorld);
    o.position = mul(worldPos, gViewProj);
    o.uv = input.uv;
    o.color = input.color * gColor;
    return o;
}
)";

        if (FAILED(D3DCompile(vsBatchSrc, std::strlen(vsBatchSrc), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBatchBlob.GetAddressOf(), errBlob.ReleaseAndGetAddressOf())))
        {
            if (errBlob) ALICE_LOG_ERRORF("[UnityVfxMesh] Batch VS compile error: %s", (char*)errBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(D3DCompile(psSrc, std::strlen(psSrc), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errBlob.ReleaseAndGetAddressOf())))
        {
            if (errBlob) ALICE_LOG_ERRORF("[UnityVfxMesh] PS compile error: %s", (char*)errBlob->GetBufferPointer());
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.ReleaseAndGetAddressOf())))
            return false;
        if (FAILED(m_device->CreateVertexShader(vsBatchBlob->GetBufferPointer(), vsBatchBlob->GetBufferSize(), nullptr, m_billboardVS.ReleaseAndGetAddressOf())))
            return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.ReleaseAndGetAddressOf())))
            return false;

        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(m_device->CreateInputLayout(layout, (UINT)std::size(layout),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf())))
            return false;

        D3D11_INPUT_ELEMENT_DESC layoutBatch[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(m_device->CreateInputLayout(layoutBatch, (UINT)std::size(layoutBatch),
            vsBatchBlob->GetBufferPointer(), vsBatchBlob->GetBufferSize(), m_billboardLayout.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool UnityVfxMeshRenderSystem::CreateTrailShaders()
    {
        static const char* trailVsSrc = R"(
cbuffer CBPerTrailEffectVS : register(b0)
{
    float4x4 gViewProj;
    float    gCurrentTime;
    float3   gPadding;
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    float  BirthTime : TEXCOORD1;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    float  Age : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = float4(input.Position, 1.0f);
    output.Position = mul(worldPos, gViewProj);
    output.Age = gCurrentTime - input.BirthTime;
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
)";

        static const char* trailPsSrc = R"(
Texture2D gTrailTex : register(t0);
SamplerState gTrailSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    float  Age : TEXCOORD1;
};

cbuffer CBPerTrailEffectPS : register(b1)
{
    float4   gColor;
    float    gFadeDuration;
    float3   gPadding;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 texColor = gTrailTex.Sample(gTrailSampler, input.TexCoord);
    float fade = 1.0f - saturate(input.Age / gFadeDuration);
    float4 col = texColor * input.Color * gColor;
    col.a *= fade;
    return col;
}
)";

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errBlob;

        if (FAILED(D3DCompile(trailVsSrc, std::strlen(trailVsSrc), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
            vsBlob.GetAddressOf(), errBlob.GetAddressOf())))
        {
            if (errBlob) ALICE_LOG_ERRORF("[UnityVfxMesh] Trail VS compile error: %s", (char*)errBlob->GetBufferPointer());
            return false;
        }

        if (FAILED(D3DCompile(trailPsSrc, std::strlen(trailPsSrc), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
            psBlob.GetAddressOf(), errBlob.ReleaseAndGetAddressOf())))
        {
            if (errBlob) ALICE_LOG_ERRORF("[UnityVfxMesh] Trail PS compile error: %s", (char*)errBlob->GetBufferPointer());
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_trailVS.ReleaseAndGetAddressOf())))
            return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_trailPS.ReleaseAndGetAddressOf())))
            return false;

        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(m_device->CreateInputLayout(layout, (UINT)std::size(layout),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_trailLayout.ReleaseAndGetAddressOf())))
            return false;

        D3D11_BUFFER_DESC cbVsDesc = {};
        cbVsDesc.ByteWidth = sizeof(CBTrailVS);
        cbVsDesc.Usage = D3D11_USAGE_DEFAULT;
        cbVsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_device->CreateBuffer(&cbVsDesc, nullptr, m_cbTrailVS.ReleaseAndGetAddressOf())))
            return false;

        D3D11_BUFFER_DESC cbPsDesc = {};
        cbPsDesc.ByteWidth = sizeof(CBTrailPS);
        cbPsDesc.Usage = D3D11_USAGE_DEFAULT;
        cbPsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_device->CreateBuffer(&cbPsDesc, nullptr, m_cbTrailPS.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool UnityVfxMeshRenderSystem::CreateStates()
    {
        D3D11_SAMPLER_DESC sdesc = {};
        sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sdesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_device->CreateSamplerState(&sdesc, m_sampler.ReleaseAndGetAddressOf())))
            return false;

        D3D11_BLEND_DESC bdesc = {};
        bdesc.RenderTarget[0].BlendEnable = TRUE;
        bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&bdesc, m_blendAdd.ReleaseAndGetAddressOf())))
            return false;

        D3D11_BLEND_DESC bdescAlpha = {};
        bdescAlpha.RenderTarget[0].BlendEnable = TRUE;
        bdescAlpha.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bdescAlpha.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bdescAlpha.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bdescAlpha.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bdescAlpha.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bdescAlpha.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bdescAlpha.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&bdescAlpha, m_blendAlpha.ReleaseAndGetAddressOf())))
            return false;

        D3D11_DEPTH_STENCIL_DESC ddesc = {};
        ddesc.DepthEnable = TRUE;
        ddesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ddesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(m_device->CreateDepthStencilState(&ddesc, m_depthState.ReleaseAndGetAddressOf())))
            return false;

        D3D11_RASTERIZER_DESC rdesc = {};
        rdesc.FillMode = D3D11_FILL_SOLID;
        rdesc.CullMode = D3D11_CULL_NONE;
        rdesc.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&rdesc, m_rasterState.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool UnityVfxMeshRenderSystem::CreateDefaultTexture()
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        UINT32 white = 0xFFFFFFFF;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = &white;
        initData.SysMemPitch = 4;

        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(m_device->CreateTexture2D(&texDesc, &initData, tex.GetAddressOf())))
            return false;

        if (FAILED(m_device->CreateShaderResourceView(tex.Get(), nullptr, m_defaultTexture.ReleaseAndGetAddressOf())))
            return false;
        return true;
    }

    bool UnityVfxMeshRenderSystem::CreateQuadMesh()
    {
        VertexPT verts[4] =
        {
            { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 0.5f,  0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 0.5f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        };
        unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(verts);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = verts;

        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, m_quadMesh.vb.ReleaseAndGetAddressOf())))
            return false;

        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = sizeof(indices);
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices;

        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, m_quadMesh.ib.ReleaseAndGetAddressOf())))
            return false;

        m_quadMesh.indexCount = 6;
        return true;
    }

    bool UnityVfxMeshRenderSystem::EnsureTrailVertexBuffer(size_t vertexCount)
    {
        if (vertexCount == 0) return false;
        const size_t required = vertexCount * sizeof(TrailVertex);
        if (m_trailVB && m_trailVBSize >= required)
            return true;

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(required);
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(m_device->CreateBuffer(&vbDesc, nullptr, m_trailVB.ReleaseAndGetAddressOf())))
            return false;

        m_trailVBSize = required;
        return true;
    }

    bool UnityVfxMeshRenderSystem::EnsureBillboardVertexBuffer(size_t vertexCount)
    {
        if (vertexCount == 0) return false;
        const size_t required = vertexCount * sizeof(VertexPTC);
        if (m_billboardVB && m_billboardVBSize >= required)
            return true;

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(required);
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(m_device->CreateBuffer(&vbDesc, nullptr, m_billboardVB.ReleaseAndGetAddressOf())))
            return false;

        m_billboardVBSize = required;
        return true;
    }

    bool UnityVfxMeshRenderSystem::EnsureMeshLoaded(const std::string& path, MeshGpu& outMesh)
    {
        auto it = m_meshCache.find(path);
        if (it != m_meshCache.end())
        {
            outMesh = it->second;
            return true;
        }

        auto jsonPtr = ResourceManager::Get().Load<nlohmann::json>(path);
        if (!jsonPtr)
        {
            ALICE_LOG_WARN("[UnityVfxMesh] Mesh load failed: %s", path.c_str());
            return false;
        }

        const Json& j = *jsonPtr;
        auto itVerts = j.find("vertices");
        auto itUV = j.find("uv");
        auto itIndices = j.find("indices");
        if (itVerts == j.end() || !itVerts->is_array() || itIndices == j.end() || !itIndices->is_array())
        {
            ALICE_LOG_WARN("[UnityVfxMesh] Invalid mesh json: %s", path.c_str());
            return false;
        }

        const size_t vcount = itVerts->size();
        std::vector<VertexPT> vertices;
        vertices.resize(vcount);

        for (size_t i = 0; i < vcount; ++i)
        {
            const auto& v = (*itVerts)[i];
            if (v.is_array() && v.size() >= 3)
            {
                vertices[i].pos = XMFLOAT3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
            }
            else
            {
                vertices[i].pos = XMFLOAT3(0, 0, 0);
            }
        }

        if (itUV != j.end() && itUV->is_array())
        {
            for (size_t i = 0; i < vcount && i < itUV->size(); ++i)
            {
                const auto& uv = (*itUV)[i];
                if (uv.is_array() && uv.size() >= 2)
                    vertices[i].uv = XMFLOAT2(uv[0].get<float>(), uv[1].get<float>());
                else
                    vertices[i].uv = XMFLOAT2(0, 0);
            }
        }
        else
        {
            for (size_t i = 0; i < vcount; ++i)
                vertices[i].uv = XMFLOAT2(0, 0);
        }

        std::vector<unsigned int> indices;
        indices.reserve(itIndices->size());
        for (const auto& idx : *itIndices)
            indices.push_back(idx.get<unsigned int>());

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(VertexPT));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = vertices.data();

        ComPtr<ID3D11Buffer> vb;
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, vb.GetAddressOf())))
            return false;

        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(unsigned int));
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices.data();

        ComPtr<ID3D11Buffer> ib;
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, ib.GetAddressOf())))
            return false;

        MeshGpu mesh;
        mesh.vb = vb;
        mesh.ib = ib;
        mesh.indexCount = static_cast<unsigned int>(indices.size());

        m_meshCache.emplace(path, mesh);
        outMesh = mesh;
        return true;
    }

    bool UnityVfxMeshRenderSystem::EnsureMaterialLoaded(const std::string& effectBaseDir, const std::string& path, MaterialGpu& outMat)
    {
        auto it = m_materialCache.find(path);
        if (it != m_materialCache.end())
        {
            outMat = it->second;
            return true;
        }

        auto jsonPtr = ResourceManager::Get().Load<nlohmann::json>(path);
        if (!jsonPtr)
        {
            ALICE_LOG_WARN("[UnityVfxMesh] Material load failed: %s", path.c_str());
            return false;
        }

        MaterialGpu mat;
        mat.texture = m_defaultTexture;
        mat.noiseTexture = m_defaultTexture;
        mat.rampTexture = m_defaultTexture;
        int blendSrc = -1;
        int blendDst = -1;

        const Json& j = *jsonPtr;
        const std::string matDir = std::filesystem::path(path).parent_path().generic_string();
        const std::string shaderName = ToLower(j.value("shader", ""));
        auto itProps = j.find("properties");
        static std::unordered_set<std::string> s_loggedMaterials;
        const std::string materialKey = path;
        const bool logMat = s_loggedMaterials.insert(materialKey).second;

        if (itProps != j.end() && itProps->is_array())
        {
            for (const auto& prop : *itProps)
            {
                const std::string type = ToLower(prop.value("type", ""));
                const std::string pname = ToLower(prop.value("name", ""));
                if (type == "texenv" && prop.contains("texture"))
                {
                    std::string texRel = prop.value("texture", "");
                    if (!texRel.empty())
                    {
                        std::string texRelLower = ToLower(texRel);
                        if (texRelLower.find("unity_builtin_extra") == std::string::npos)
                        {
                            std::vector<std::string> candidates;
                            if (StartsWithInsensitive(texRel, "resource/") || StartsWithInsensitive(texRel, "assets/"))
                            {
                                candidates.push_back(texRel);
                            }
                            else
                            {
                                candidates.push_back(JoinPath(matDir, texRel));
                                if (matDir != effectBaseDir)
                                    candidates.push_back(JoinPath(effectBaseDir, texRel));
                            }

                            bool loaded = false;
                            for (const auto& texPath : candidates)
                            {
                                auto srv = ResourceManager::Get().LoadData<ID3D11ShaderResourceView>(texPath, m_device.Get());
                                if (!srv)
                                    continue;

                                if (pname.find("noise") != std::string::npos || pname.find("mask") != std::string::npos || pname.find("dissolve") != std::string::npos)
                                    mat.noiseTexture = srv;
                                else if (pname.find("ramp") != std::string::npos || pname.find("gradient") != std::string::npos)
                                    mat.rampTexture = srv;
                                else
                                    mat.texture = srv;
                                loaded = true;
                                if (logMat)
                                    ALICE_LOG_INFO("[UnityVfxMesh] Texture OK material=\"%s\" prop=\"%s\" path=\"%s\"",
                                        path.c_str(), pname.c_str(), texPath.c_str());
                                break;
                            }
                            if (!loaded && logMat)
                            {
                                std::string joined;
                                for (size_t i = 0; i < candidates.size(); ++i)
                                {
                                    if (i > 0) joined += "; ";
                                    joined += candidates[i];
                                }
                                ALICE_LOG_ERRORF("[UnityVfxMesh] Texture FAIL material=\"%s\" prop=\"%s\" candidates=\"%s\"",
                                    path.c_str(), pname.c_str(), joined.c_str());
                            }
                        }
                    }
                    XMFLOAT2 scale{ 1.0f, 1.0f };
                    XMFLOAT2 offset{ 0.0f, 0.0f };
                    if (TryGetVec2(prop.value("scale", Json{}), scale))
                        mat.uvScale = scale;
                    if (TryGetVec2(prop.value("offset", Json{}), offset))
                        mat.uvOffset = offset;
                }
                else if (type == "color")
                {
                    XMFLOAT4 c{ 1,1,1,1 };
                    if (TryGetVec4(prop.value("value", Json{}), c))
                        mat.color = c;
                }
                else if (type == "float")
                {
                    if (pname == "_blendsrc" || pname == "blendsrc" || pname == "_srcblend")
                        blendSrc = static_cast<int>(prop.value("value", 0.0f));
                    else if (pname == "_blenddst" || pname == "blenddst" || pname == "_dstblend")
                        blendDst = static_cast<int>(prop.value("value", 0.0f));
                    else if (pname.find("dissolve") != std::string::npos || pname.find("cutoff") != std::string::npos || pname.find("threshold") != std::string::npos || pname.find("vector1") != std::string::npos)
                        mat.dissolve = static_cast<float>(prop.value("value", 0.0f));
                    else if (pname.find("noise") != std::string::npos || pname.find("distort") != std::string::npos)
                        mat.noiseStrength = static_cast<float>(prop.value("value", 0.0f));
                    else if (pname.find("ramp") != std::string::npos)
                        mat.rampStrength = static_cast<float>(prop.value("value", 0.0f));
                }
                else if (type == "range")
                {
                    if (pname.find("dissolve") != std::string::npos || pname.find("cutoff") != std::string::npos || pname.find("threshold") != std::string::npos || pname.find("vector1") != std::string::npos)
                        mat.dissolve = static_cast<float>(prop.value("value", 0.0f));
                    else if (pname.find("noise") != std::string::npos || pname.find("distort") != std::string::npos)
                        mat.noiseStrength = static_cast<float>(prop.value("value", 0.0f));
                    else if (pname.find("ramp") != std::string::npos)
                        mat.rampStrength = static_cast<float>(prop.value("value", 0.0f));
                }
                else if (type == "vector")
                {
                    XMFLOAT4 v{ 0,0,0,0 };
                    if (TryGetVec4(prop.value("value", Json{}), v))
                    {
                        if (pname.find("scroll") != std::string::npos || pname.find("uv") != std::string::npos || pname.find("vector2") != std::string::npos)
                        {
                            mat.uvScroll = XMFLOAT2(v.x, v.y);
                        }
                    }
                }
            }
        }

        if (blendDst == 10 || blendDst == 6 || (blendSrc == 5 && blendDst == 10) ||
            shaderName.find("alpha") != std::string::npos || shaderName.find("transparent") != std::string::npos)
            mat.blendMode = BlendMode::Alpha;
        else if (blendDst == 1 || (blendSrc == 5 && blendDst == 1) || shaderName.find("add") != std::string::npos)
            mat.blendMode = BlendMode::Additive;

        m_materialCache.emplace(path, mat);
        outMat = mat;
        return true;
    }

    const UnityVfxMeshRenderSystem::EffectCache* UnityVfxMeshRenderSystem::GetEffectCache(const std::string& effectPath)
    {
        auto& cache = m_effectCache[effectPath];
        if (cache.loaded) return &cache;
        cache.error.clear();

        auto jsonPtr = ResourceManager::Get().Load<nlohmann::json>(effectPath);
        if (!jsonPtr)
        {
            cache.error = "failed to load effect.json";
            cache.valid = false;
            return &cache;
        }

        cache.meshNodes.clear();
        cache.billboardNodes.clear();
        cache.emitters.clear();

        const Json& root = *jsonPtr;
        auto itNodes = root.find("nodes");
        if (itNodes == root.end() || !itNodes->is_array())
        {
            cache.error = "effect.json missing nodes[]";
            cache.valid = false;
            return &cache;
        }

        const std::string baseDir = std::filesystem::path(effectPath).parent_path().generic_string();

        std::unordered_map<std::string, XMMATRIX> localMatrices;
        std::unordered_map<std::string, XMMATRIX> worldMatrices;
        for (const auto& node : *itNodes)
        {
            if (!node.is_object()) continue;
            const std::string path = node.value("path", "");
            if (path.empty()) continue;
            auto itT = node.find("transform");
            if (itT != node.end() && itT->is_object())
                localMatrices[path] = BuildTransformMatrix(*itT);
            else
                localMatrices[path] = XMMatrixIdentity();
        }

        auto getWorld = [&](const std::string& path, const auto& self) -> XMMATRIX
        {
            auto itW = worldMatrices.find(path);
            if (itW != worldMatrices.end())
                return itW->second;
            auto itL = localMatrices.find(path);
            XMMATRIX local = (itL != localMatrices.end()) ? itL->second : XMMatrixIdentity();
            const size_t slash = path.find_last_of('/');
            if (slash == std::string::npos)
            {
                worldMatrices[path] = local;
                return local;
            }
            const std::string parent = path.substr(0, slash);
            XMMATRIX world = self(parent, self) * local;
            worldMatrices[path] = world;
            return world;
        };

        for (const auto& node : *itNodes)
        {
            if (!node.is_object()) continue;

            auto itParticle = node.find("particle");
            auto itRenderer = node.find("renderer");
            if (itParticle == node.end() || itRenderer == node.end() || !itRenderer->is_object())
                continue;

            std::string renderMode = ToLower(itRenderer->value("renderMode", ""));
            const Json& particle = *itParticle;
            const Json& main = particle.value("main", Json{});
            const Json& emission = particle.value("emission", Json{});
            const Json& shape = particle.value("shape", Json{});
            const Json& colOver = particle.value("colorOverLifetime", Json{});
            const Json& sizeOver = particle.value("sizeOverLifetime", Json{});
            const Json& rotOver = particle.value("rotationOverLifetime", Json{});
            const Json& velOver = particle.value("velocityOverLifetime", Json{});
            const Json& trails = particle.value("trails", Json{});

            EmitterDef def;
            def.name = node.value("path", "");

            if (!def.name.empty())
            {
                XMMATRIX worldM = getWorld(def.name, getWorld);
                XMVECTOR s, r, t;
                if (XMMatrixDecompose(&s, &r, &t, worldM))
                {
                    XMStoreFloat3(&def.localScale, s);
                    XMStoreFloat4(&def.localRot, r);
                    XMStoreFloat3(&def.localPos, t);
                }
            }

            def.duration = main.value("duration", 1.0f);
            def.loop = main.value("loop", true);
            const std::string simSpace = ToLower(main.value("simulationSpace", "local"));
            def.space = (simSpace == "world") ? SimulationSpace::World : SimulationSpace::Local;

            def.startLifetime = ParseMinMaxCurve(main.value("startLifetime", Json{}), 1.0f);
            def.startSpeed = ParseMinMaxCurve(main.value("startSpeed", Json{}), 0.0f);
            def.startSize = ParseMinMaxCurve(main.value("startSize", Json{}), 1.0f);
            def.startColor = ParseMinMaxGradient(main.value("startColor", Json{}), XMFLOAT4(1, 1, 1, 1));
            def.startRotation = ParseMinMaxCurve(main.value("startRotation", Json{}), 0.0f);
            if (main.contains("startRotation3D") && main.value("startRotation3D", Json{}).is_object())
            {
                const Json& r3 = main.value("startRotation3D", Json{});
                def.startRotation3D = true;
                def.startRotationX = ParseMinMaxCurve(r3.value("x", Json{}), 0.0f);
                def.startRotationY = ParseMinMaxCurve(r3.value("y", Json{}), 0.0f);
                def.startRotationZ = ParseMinMaxCurve(r3.value("z", Json{}), 0.0f);
            }

            if (colOver.is_object() && colOver.value("enabled", false))
            {
                def.colorOverLifetimeEnabled = true;
                def.colorOverLifetime = ParseMinMaxGradient(colOver.value("color", Json{}), XMFLOAT4(1, 1, 1, 1));
            }

            if (sizeOver.is_object() && sizeOver.value("enabled", false))
            {
                def.sizeOverLifetimeEnabled = true;
                def.sizeOverLifetime = ParseMinMaxCurve(sizeOver.value("size", Json{}), 1.0f);
            }

            if (rotOver.is_object() && rotOver.value("enabled", false))
            {
                def.rotationOverLifetime.enabled = true;
                def.rotationOverLifetime.separateAxes = rotOver.value("separateAxes", false);
                if (def.rotationOverLifetime.separateAxes)
                {
                    def.rotationOverLifetime.x = ParseMinMaxCurve(rotOver.value("x", Json{}), 0.0f);
                    def.rotationOverLifetime.y = ParseMinMaxCurve(rotOver.value("y", Json{}), 0.0f);
                    def.rotationOverLifetime.z = ParseMinMaxCurve(rotOver.value("z", Json{}), 0.0f);
                }
                else
                {
                    def.rotationOverLifetime.z = ParseMinMaxCurve(rotOver.value("z", Json{}), 0.0f);
                }
            }

            if (velOver.is_object() && velOver.value("enabled", false))
            {
                def.velocityOverLifetime.enabled = true;
                def.velocityOverLifetime.separateAxes = velOver.value("separateAxes", false);
                const std::string velSpace = ToLower(velOver.value("space", "local"));
                def.velocityOverLifetime.space = (velSpace == "world") ? SimulationSpace::World : SimulationSpace::Local;
                if (def.velocityOverLifetime.separateAxes)
                {
                    def.velocityOverLifetime.x = ParseMinMaxCurve(velOver.value("x", Json{}), 0.0f);
                    def.velocityOverLifetime.y = ParseMinMaxCurve(velOver.value("y", Json{}), 0.0f);
                    def.velocityOverLifetime.z = ParseMinMaxCurve(velOver.value("z", Json{}), 0.0f);
                }
                else
                {
                    def.velocityOverLifetime.x = ParseMinMaxCurve(velOver.value("x", Json{}), 0.0f);
                    def.velocityOverLifetime.y = ParseMinMaxCurve(velOver.value("y", Json{}), 0.0f);
                    def.velocityOverLifetime.z = ParseMinMaxCurve(velOver.value("z", Json{}), 0.0f);
                }
            }

            def.rateOverTime = 0.0f;
            if (emission.is_object() && emission.value("enabled", false))
            {
                def.rateOverTime = EvalMinMaxCurve(ParseMinMaxCurve(emission.value("rateOverTime", Json{}), 0.0f), 0.0f);
                auto itBursts = emission.find("bursts");
                if (itBursts != emission.end() && itBursts->is_array())
                {
                    for (const auto& b : *itBursts)
                    {
                        BurstDef bd;
                        bd.time = b.value("time", 0.0f);
                        bd.minCount = b.value("minCount", 0);
                        bd.maxCount = b.value("maxCount", bd.minCount);
                        bd.cycleCount = b.value("cycleCount", 1);
                        bd.repeatInterval = b.value("repeatInterval", 0.0f);
                        bd.probability = b.value("probability", 1.0f);
                        def.bursts.push_back(bd);
                    }
                }
            }

            def.shapeEnabled = shape.value("enabled", true);
            const std::string shapeType = ToLower(shape.value("type", "cone"));
            if (shapeType == "box")
                def.shape.type = ShapeType::Box;
            else if (shapeType == "sphere")
                def.shape.type = ShapeType::Sphere;
            else
                def.shape.type = ShapeType::Cone;
            def.shape.radius = shape.value("radius", 1.0f);
            def.shape.angleDeg = shape.value("angle", 25.0f);
            def.shape.length = shape.value("length", 5.0f);
            TryGetVec3(shape.value("box", Json{}), def.shape.box);
            TryGetVec3(shape.value("position", Json{}), def.shape.position);
            TryGetVec3(shape.value("rotation", Json{}), def.shape.rotation);
            def.shape.alignToDirection = shape.value("alignToDirection", false);

            def.meshRenderer = false;
            def.renderMode = BillboardMode::Billboard;
            if (renderMode == "mesh")
            {
                def.meshRenderer = true;
            }
            else if (renderMode == "horizontalbillboard")
            {
                def.renderMode = BillboardMode::Horizontal;
            }
            else if (renderMode == "stretch" || renderMode == "stretchedbillboard")
            {
                def.renderMode = BillboardMode::Stretch;
            }

            std::string matRel = itRenderer->value("material", "");
            if (!matRel.empty() && (StartsWithInsensitive(matRel, "resource/") || StartsWithInsensitive(matRel, "assets/")))
                def.materialPath = matRel;
            else
                def.materialPath = matRel.empty() ? "" : JoinPath(baseDir, matRel);

            std::string meshRel = itRenderer->value("mesh", "");
            if (!meshRel.empty() && (StartsWithInsensitive(meshRel, "resource/") || StartsWithInsensitive(meshRel, "assets/")))
                def.meshPath = meshRel;
            else
                def.meshPath = JoinPath(baseDir, meshRel);
            def.stretchScale = itRenderer->value("maxParticleSize", 1.0f);
            if (def.renderMode != BillboardMode::Stretch)
                def.stretchScale = 1.0f;

            const Json& texSheet = particle.value("textureSheetAnimation", Json{});
            if (texSheet.is_object() && texSheet.value("enabled", false))
            {
                def.texSheet.enabled = true;
                def.texSheet.tilesX = texSheet.value("numTilesX", 1);
                def.texSheet.tilesY = texSheet.value("numTilesY", 1);
                def.texSheet.frameOverTime = ParseMinMaxCurve(texSheet.value("frameOverTime", Json{}), 1.0f);
                def.texSheet.startFrame = ParseMinMaxCurve(texSheet.value("startFrame", Json{}), 0.0f);
                def.texSheet.cycleCount = texSheet.value("cycleCount", 1);
            }

            if (trails.is_object() && trails.value("enabled", false))
            {
                def.trails.enabled = true;
                def.trails.lifetime = trails.value("lifetime", 0.5f);
                def.trails.minVertexDistance = trails.value("minVertexDistance", 0.05f);
                def.trails.widthOverTrail = ParseMinMaxCurve(trails.value("widthOverTrail", Json{}), 1.0f);
                if (trails.contains("colorOverTrail"))
                    def.trails.colorOverTrail = ParseMinMaxGradient(trails.value("colorOverTrail", Json{}), XMFLOAT4(1, 1, 1, 1));
            }

            cache.emitters.push_back(def);
        }

        cache.loaded = true;
        cache.valid = !cache.emitters.empty();
        if (!cache.valid && cache.error.empty())
            cache.error = "no particle render nodes";
        return &cache;
    }
}
