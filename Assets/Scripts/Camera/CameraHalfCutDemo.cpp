#include "CameraHalfCutDemo.h"

#include <algorithm>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraHalfCutDemo);

    namespace
    {
        float Saturate(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float EaseOutCubic(float t)
        {
            t = Saturate(t);
            const float inv = 1.0f - t;
            return 1.0f - inv * inv * inv;
        }

        float EaseInCubic(float t)
        {
            t = Saturate(t);
            return t * t * t;
        }

        float Lerp(float a, float b, float t)
        {
            t = Saturate(t);
            return a + (b - a) * t;
        }

        float LerpUnclamped(float a, float b, float t)
        {
            return a + (b - a) * t;
        }
    }

    void CameraHalfCutDemo::Start()
    {
        EnsurePostProcessVolume();
        ResolveTargetCamera();
        CaptureBaseline();
        RefreshFovCurveAsset();
        ResetAll();
        m_elapsedSec = 0.0f;
        m_playing = false;
        m_shakeTriggered = false;
        m_vfxTriggered = false;
    }

    void CameraHalfCutDemo::Update(float deltaTime)
    {
        if (m_loadedFovCurvePath != Get_m_fovCurvePath())
        {
            RefreshFovCurveAsset();
        }

        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(static_cast<KeyCode>(Get_m_triggerKey())))
        {
            if (m_playing)
            {
                m_playing = false;
                ResetAll();
            }

            World* world = GetWorld();
            if (world && m_activeCutVfx != InvalidEntityId && world->GetComponent<TransformComponent>(m_activeCutVfx))
            {
                world->DestroyEntity(m_activeCutVfx);
            }
            m_activeCutVfx = InvalidEntityId;
            m_vfxTriggered = false;

            if (EnsurePostProcessVolume() && ResolveTargetCamera())
            {
                CaptureBaseline();
                PrepareOverrides();
                m_playing = true;
                m_shakeTriggered = false;
                m_vfxTriggered = false;
                m_elapsedSec = 0.0f;
                ApplyEffectState(0.0f);
            }
        }

        if (!m_playing)
            return;

        m_elapsedSec += std::max(0.0f, deltaTime);
        const float totalSec = GetTotalDuration();

        if (totalSec <= 0.0f)
        {
            m_playing = false;
            ResetAll();
            return;
        }

        ApplyEffectState(m_elapsedSec);

        if (m_elapsedSec >= totalSec)
        {
            m_playing = false;
            ResetAll();
        }
    }

    void CameraHalfCutDemo::OnDisable()
    {
        m_playing = false;
        ResetAll();
    }

    void CameraHalfCutDemo::OnDestroy()
    {
        m_playing = false;
        ResetAll();
    }

    bool CameraHalfCutDemo::EnsurePostProcessVolume()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        auto* current = (m_volumeEntity == InvalidEntityId)
            ? nullptr
            : world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);

        if (!current)
        {
            const std::string name = Get_m_postProcessVolumeName();
            if (!name.empty())
            {
                GameObject go = world->FindGameObject(name);
                if (go.IsValid())
                {
                    auto* found = world->GetComponent<PostProcessVolumeComponent>(go.id());
                    if (found)
                    {
                        m_volumeEntity = go.id();
                        current = found;
                    }
                }
            }
        }

        if (!current && Get_m_autoCreateVolume())
        {
            const EntityId id = world->CreateEntity();
            m_volumeEntity = id;
            world->SetEntityName(id, Get_m_postProcessVolumeName().empty() ? "CameraHalfCutVolume" : Get_m_postProcessVolumeName());

            auto& tr = world->AddComponent<TransformComponent>(id);
            tr.enabled = true;
            tr.visible = true;
            tr.position = { 0.0f, 0.0f, 0.0f };
            tr.rotation = { 0.0f, 0.0f, 0.0f };
            tr.scale = { 1.0f, 1.0f, 1.0f };

            auto& ppv = world->AddComponent<PostProcessVolumeComponent>(id);
            ppv.unbound = true;
            ppv.blendWeight = 1.0f;
            ppv.priority = Get_m_volumePriority();
            ppv.blendRadius = 0.0f;
            ppv.boxSize = { 1.0f, 1.0f, 1.0f };
            current = &ppv;
        }

        if (!current)
        {
            if (!m_warnedMissingVolume)
            {
                ALICE_LOG_WARN("[CameraHalfCutDemo] PostProcessVolume '%s' not found. (autoCreate=%s)",
                    Get_m_postProcessVolumeName().c_str(),
                    Get_m_autoCreateVolume() ? "true" : "false");
                m_warnedMissingVolume = true;
            }
            return false;
        }

        m_warnedMissingVolume = false;
        current->unbound = true;
        current->blendWeight = 1.0f;
        current->priority = Get_m_volumePriority();
        return true;
    }

    bool CameraHalfCutDemo::ResolveTargetCamera()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        if (m_cameraEntity != InvalidEntityId)
        {
            if (world->GetComponent<CameraComponent>(m_cameraEntity))
                return true;
            m_cameraEntity = InvalidEntityId;
        }

        if (world->GetComponent<CameraComponent>(GetOwnerId()))
        {
            m_cameraEntity = GetOwnerId();
            return true;
        }

        const std::string cameraName = Get_m_cameraName();
        if (!cameraName.empty())
        {
            GameObject go = world->FindGameObject(cameraName);
            if (go.IsValid() && world->GetComponent<CameraComponent>(go.id()))
            {
                m_cameraEntity = go.id();
                return true;
            }
        }

        for (const auto& [id, cam] : world->GetComponents<CameraComponent>())
        {
            if (cam.primary)
            {
                m_cameraEntity = id;
                return true;
            }
        }

        for (const auto& [id, _] : world->GetComponents<CameraComponent>())
        {
            m_cameraEntity = id;
            return true;
        }

        return false;
    }

    void CameraHalfCutDemo::CaptureBaseline()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (EnsurePostProcessVolume() && m_volumeEntity != InvalidEntityId)
        {
            auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
            if (ppv)
            {
                const auto& s = ppv->settings;
                m_ppvBaseline.valid = true;

                m_ppvBaseline.bOverrideSplitAmount = s.bOverride_SplitAmount;
                m_ppvBaseline.bOverrideSplitAngleDeg = s.bOverride_SplitAngleDeg;
                m_ppvBaseline.bOverrideSplitLineOffset = s.bOverride_SplitLineOffset;
                m_ppvBaseline.bOverrideSplitFeather = s.bOverride_SplitFeather;
                m_ppvBaseline.bOverrideSplitFxIntensity = s.bOverride_SplitFxIntensity;
                m_ppvBaseline.bOverrideSplitFxWidth = s.bOverride_SplitFxWidth;
                m_ppvBaseline.bOverrideSplitFxSpeed = s.bOverride_SplitFxSpeed;
                m_ppvBaseline.bOverrideSplitFxTimeSec = s.bOverride_SplitFxTimeSec;

                m_ppvBaseline.bOverrideExposure = s.bOverride_Exposure;
                m_ppvBaseline.bOverrideBloomThreshold = s.bOverride_BloomThreshold;
                m_ppvBaseline.bOverrideBloomKnee = s.bOverride_BloomKnee;
                m_ppvBaseline.bOverrideBloomIntensity = s.bOverride_BloomIntensity;
                m_ppvBaseline.bOverrideBloomGaussianIntensity = s.bOverride_BloomGaussianIntensity;
                m_ppvBaseline.bOverrideBloomRadius = s.bOverride_BloomRadius;

                m_ppvBaseline.splitAmount = s.splitAmount;
                m_ppvBaseline.splitAngleDeg = s.splitAngleDeg;
                m_ppvBaseline.splitLineOffset = s.splitLineOffset;
                m_ppvBaseline.splitFeather = s.splitFeather;
                m_ppvBaseline.splitFxIntensity = s.splitFxIntensity;
                m_ppvBaseline.splitFxWidth = s.splitFxWidth;
                m_ppvBaseline.splitFxSpeed = s.splitFxSpeed;
                m_ppvBaseline.splitFxTimeSec = s.splitFxTimeSec;

                m_ppvBaseline.exposure = s.exposure;
                m_ppvBaseline.bloomThreshold = s.bloomThreshold;
                m_ppvBaseline.bloomKnee = s.bloomKnee;
                m_ppvBaseline.bloomIntensity = s.bloomIntensity;
                m_ppvBaseline.bloomGaussianIntensity = s.bloomGaussianIntensity;
                m_ppvBaseline.bloomRadius = s.bloomRadius;
            }
        }

        if (ResolveTargetCamera())
        {
            if (auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity))
            {
                m_baseFov = cam->GetFov();
            }
        }
    }

    void CameraHalfCutDemo::RefreshFovCurveAsset()
    {
        m_fovCurveLoaded = false;
        m_loadedFovCurvePath = Get_m_fovCurvePath();

        if (!Get_m_useFovCurveAsset())
            return;

        const std::string& logicalPath = m_loadedFovCurvePath;
        if (logicalPath.empty())
            return;

        std::filesystem::path resolved = logicalPath;
        if (auto* rm = Resources())
        {
            resolved = rm->Resolve(logicalPath);
        }

        UICurveAsset asset{};
        if (!LoadUICurveAsset(resolved, asset) || asset.keys.empty())
        {
            if (!m_fovCurveWarned)
            {
                ALICE_LOG_WARN("[CameraHalfCutDemo] Failed to load FOV curve: %s", logicalPath.c_str());
                m_fovCurveWarned = true;
            }
            return;
        }

        asset.Sort();
        asset.RecalcAutoTangents();
        m_fovCurveAsset = std::move(asset);
        m_fovCurveLoaded = true;
        m_fovCurveWarned = false;
    }

    void CameraHalfCutDemo::PrepareOverrides()
    {
        World* world = GetWorld();
        if (!world || !EnsurePostProcessVolume() || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        auto& s = ppv->settings;
        s.bOverride_SplitAmount = true;
        s.bOverride_SplitAngleDeg = true;
        s.bOverride_SplitLineOffset = true;
        s.bOverride_SplitFeather = true;
        s.bOverride_SplitFxIntensity = true;
        s.bOverride_SplitFxWidth = true;
        s.bOverride_SplitFxSpeed = true;
        s.bOverride_SplitFxTimeSec = true;

        s.bOverride_Exposure = true;
        s.bOverride_BloomThreshold = true;
        s.bOverride_BloomKnee = true;
        s.bOverride_BloomIntensity = true;
        s.bOverride_BloomGaussianIntensity = true;
        s.bOverride_BloomRadius = true;
    }

    void CameraHalfCutDemo::ApplyEffectState(float timeSec)
    {
        const float split = EvaluateSplitAmount(timeSec);
        const float flash01 = EvaluateFlash01(timeSec);
        const float fov = EvaluateFov(timeSec);

        ApplyPostProcess(split, flash01, timeSec);
        ApplyCameraFov(fov);

        const float shakeTriggerTime = std::max(0.0f, Get_m_attackSec());
        if (!m_shakeTriggered && timeSec >= shakeTriggerTime)
        {
            TriggerCameraShake();
            m_shakeTriggered = true;
        }

        TrySpawnCutVfx(timeSec);
        UpdateCutVfxState(timeSec);
    }

    void CameraHalfCutDemo::ApplyPostProcess(float splitAmount, float flash01, float timeSec)
    {
        World* world = GetWorld();
        if (!world || !EnsurePostProcessVolume() || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        PrepareOverrides();

        auto& s = ppv->settings;
        s.splitAmount = splitAmount;
        s.splitAngleDeg = Get_m_angleDeg();
        s.splitLineOffset = Get_m_lineOffset();
        s.splitFeather = std::max(0.0001f, Get_m_feather());
        s.splitFxWidth = std::max(0.0001f, Get_m_splitFxWidth());
        s.splitFxSpeed = std::max(0.0f, Get_m_splitFxSpeed());
        s.splitFxTimeSec = std::max(0.0f, timeSec) * std::max(0.0f, Get_m_splitFxTimeScale());

        const float peakAbs = std::max(std::abs(Get_m_peakAmount()), 1e-6f);
        const float split01 = Saturate(std::abs(splitAmount) / peakAbs);
        const float fx01 = std::max(split01, flash01);
        s.splitFxIntensity = Get_m_splitFxEnabled()
            ? std::max(0.0f, Get_m_splitFxIntensity()) * fx01
            : 0.0f;

        const float baseExposure = m_ppvBaseline.valid ? m_ppvBaseline.exposure : 0.0f;
        const float baseBloomThreshold = m_ppvBaseline.valid ? m_ppvBaseline.bloomThreshold : 1.0f;
        const float baseBloomKnee = m_ppvBaseline.valid ? m_ppvBaseline.bloomKnee : 0.5f;
        const float baseBloomIntensity = m_ppvBaseline.valid ? m_ppvBaseline.bloomIntensity : 0.0f;
        const float baseBloomGaussianIntensity = m_ppvBaseline.valid ? m_ppvBaseline.bloomGaussianIntensity : 1.0f;
        const float baseBloomRadius = m_ppvBaseline.valid ? m_ppvBaseline.bloomRadius : 1.0f;

        s.exposure = Lerp(baseExposure, Get_m_peakExposure(), flash01);
        s.bloomThreshold = Lerp(baseBloomThreshold, Get_m_peakBloomThreshold(), flash01);
        s.bloomKnee = Lerp(baseBloomKnee, Get_m_peakBloomKnee(), flash01);
        s.bloomIntensity = Lerp(baseBloomIntensity, Get_m_peakBloomIntensity(), flash01);
        s.bloomGaussianIntensity = Lerp(baseBloomGaussianIntensity, Get_m_peakBloomGaussianIntensity(), flash01);
        s.bloomRadius = Lerp(baseBloomRadius, Get_m_peakBloomRadius(), flash01);
    }

    void CameraHalfCutDemo::ApplyCameraFov(float fovDeg)
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (!ResolveTargetCamera() || m_cameraEntity == InvalidEntityId)
            return;

        auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity);
        if (!cam)
            return;

        cam->SetFov(std::clamp(fovDeg, 1.0f, 170.0f));
    }

    void CameraHalfCutDemo::TriggerCameraShake()
    {
        World* world = GetWorld();
        if (!world || !ResolveTargetCamera() || m_cameraEntity == InvalidEntityId)
            return;

        auto* shake = world->GetComponent<CameraShakeComponent>(m_cameraEntity);
        if (!shake)
        {
            shake = &world->AddComponent<CameraShakeComponent>(m_cameraEntity);
        }

        const float amp = std::max(0.0f, Get_m_shakeAmplitude());
        const float freq = std::max(0.0f, Get_m_shakeFrequency());
        const float dur = std::max(0.0f, Get_m_shakeDuration());
        const float decay = std::max(0.0f, Get_m_shakeDecay());

        if (amp <= 0.0f || dur <= 0.0f)
            return;

        const bool active = shake->IsActive();
        shake->enabled = true;
        shake->frequency = freq;
        shake->decay = decay;

        if (!active)
        {
            shake->amplitude = amp;
            shake->duration = dur;
            shake->elapsed = 0.0f;
            shake->prevOffset = {};
            return;
        }

        shake->amplitude += amp;
        const float endTime = shake->duration;
        const float newEnd = shake->elapsed + dur;
        shake->duration = std::max(endTime, newEnd);
    }

    void CameraHalfCutDemo::TrySpawnCutVfx(float timeSec)
    {
        if (!Get_m_spawnCutVfx() || m_vfxTriggered)
            return;

        if (timeSec < std::max(0.0f, Get_m_cutVfxTriggerSec()))
            return;

        m_activeCutVfx = SpawnCutVfx();
        m_vfxTriggered = (m_activeCutVfx != InvalidEntityId);
    }

    void CameraHalfCutDemo::UpdateCutVfxState(float timeSec)
    {
        World* world = GetWorld();
        if (!world || m_activeCutVfx == InvalidEntityId)
            return;

        if (!world->GetComponent<TransformComponent>(m_activeCutVfx))
        {
            m_activeCutVfx = InvalidEntityId;
            return;
        }

        const float total = std::max(0.0f, GetTotalDuration());
        if (total <= 0.0f)
            return;

        const float fadeSec = std::clamp(Get_m_cutVfxFadeOutSec(), 0.0f, total);
        const float fadeStart = std::max(0.0f, total - fadeSec);

        float fade01 = 1.0f;
        if (fadeSec > 0.0f && timeSec >= fadeStart)
        {
            const float t = (timeSec - fadeStart) / fadeSec;
            fade01 = 1.0f - EaseInCubic(t);
        }
        fade01 = Saturate(fade01);

        const bool forceOneShot = Get_m_cutVfxForceOneShot();
        const bool loop = !forceOneShot;
        const bool emit = forceOneShot ? true : ((fade01 > 0.02f) && (timeSec < total));
        ApplyCutVfxRuntimeRecursive(m_activeCutVfx, fade01, emit, loop);
    }

    EntityId CameraHalfCutDemo::SpawnCutVfx()
    {
        const std::string& prefabPath = Get_m_cutVfxPrefabPath();
        if (prefabPath.empty())
            return InvalidEntityId;

        World* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        Prefab::SetDefaultWorld(world);
        const EntityId id = Prefab::InstantiateFromFileAuto(prefabPath);
        if (id == InvalidEntityId)
        {
            ALICE_LOG_WARN("[CameraHalfCutDemo] Cut VFX instantiate failed: %s", prefabPath.c_str());
            return InvalidEntityId;
        }

        world->SetParent(id, InvalidEntityId, false);

        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            DirectX::XMFLOAT3 spawnPos = {};
            DirectX::XMFLOAT3 spawnRot = {};
            bool hasSpawnPos = false;

            if (ResolveTargetCamera() && m_cameraEntity != InvalidEntityId)
            {
                if (auto* camTr = world->GetComponent<TransformComponent>(m_cameraEntity))
                {
                    using namespace DirectX;
                    const XMVECTOR camPos = XMLoadFloat3(&camTr->position);
                    const XMMATRIX rotM = XMMatrixRotationRollPitchYaw(camTr->rotation.x, camTr->rotation.y, camTr->rotation.z);

                    const XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotM);
                    const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotM);
                    const XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotM);

                    const DirectX::XMFLOAT3 local = Get_m_cutVfxLocalOffset();
                    const float forwardDist = std::max(0.0f, Get_m_cutVfxForwardDistance()) + local.z;
                    const XMVECTOR p = camPos
                        + right * local.x
                        + up * local.y
                        + fwd * forwardDist;

                    XMStoreFloat3(&spawnPos, p);
                    spawnRot = camTr->rotation;
                    hasSpawnPos = true;
                }
            }

            if (Get_m_cutVfxUseAnchorMidpoint())
            {
                const std::string& aName = Get_m_cutVfxAnchorAName();
                const std::string& bName = Get_m_cutVfxAnchorBName();
                if (!aName.empty() && !bName.empty())
                {
                    GameObject aGo = world->FindGameObject(aName);
                    GameObject bGo = world->FindGameObject(bName);
                    if (aGo.IsValid() && bGo.IsValid())
                    {
                        const auto* aTr = world->GetComponent<TransformComponent>(aGo.id());
                        const auto* bTr = world->GetComponent<TransformComponent>(bGo.id());
                        if (aTr && bTr)
                        {
                            const auto off = Get_m_cutVfxMidpointOffset();
                            spawnPos.x = (aTr->position.x + bTr->position.x) * 0.5f + off.x;
                            spawnPos.y = (aTr->position.y + bTr->position.y) * 0.5f + off.y;
                            spawnPos.z = (aTr->position.z + bTr->position.z) * 0.5f + off.z;
                            hasSpawnPos = true;
                        }
                    }
                }
            }

            if (!hasSpawnPos)
            {
                spawnPos = Get_m_cutVfxLocalOffset();
            }

            const DirectX::XMFLOAT3 rotDeg = Get_m_cutVfxEulerOffsetDeg();
            spawnRot.x += DirectX::XMConvertToRadians(rotDeg.x);
            spawnRot.y += DirectX::XMConvertToRadians(rotDeg.y);
            spawnRot.z += DirectX::XMConvertToRadians(rotDeg.z);

            tr->position = spawnPos;
            tr->rotation = spawnRot;
            tr->scale = Get_m_cutVfxScale();
            world->MarkTransformDirty(id);
        }

        ConfigureCutVfxRecursive(id);

        return id;
    }

    void CameraHalfCutDemo::ConfigureCutVfxRecursive(EntityId id)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* vfx = world->GetComponent<UnityVfxComponent>(id))
        {
            vfx->enabled = true;
            vfx->overrideLoop = true;
            vfx->loop = !Get_m_cutVfxForceOneShot();
            vfx->emitNewParticles = true;
            vfx->spawnRateScale = std::max(0.0f, Get_m_cutVfxBaseSpawnRate());
            vfx->alphaScale = std::max(0.0f, Get_m_cutVfxBaseAlpha());
            vfx->intensityScale = std::max(0.0f, Get_m_cutVfxBaseIntensity());
            vfx->playId += 1;
        }

        if (auto* ce = world->GetComponent<ComputeEffectComponent>(id))
        {
            ce->enabled = true;
        }

        const auto children = world->GetChildren(id);
        for (EntityId child : children)
        {
            ConfigureCutVfxRecursive(child);
        }
    }

    void CameraHalfCutDemo::ApplyCutVfxRuntimeRecursive(EntityId id, float fade01, bool emitNewParticles, bool loop)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* vfx = world->GetComponent<UnityVfxComponent>(id))
        {
            const float fade = Saturate(fade01);
            vfx->enabled = true;
            vfx->overrideLoop = true;
            vfx->loop = loop;
            vfx->emitNewParticles = emitNewParticles;
            vfx->spawnRateScale = std::max(0.0f, Get_m_cutVfxBaseSpawnRate()) * fade;
            vfx->alphaScale = std::max(0.0f, Get_m_cutVfxBaseAlpha()) * fade;
            vfx->intensityScale = std::max(0.0f, Get_m_cutVfxBaseIntensity()) * fade;
        }

        const auto children = world->GetChildren(id);
        for (EntityId child : children)
        {
            ApplyCutVfxRuntimeRecursive(child, fade01, emitNewParticles, loop);
        }
    }

    void CameraHalfCutDemo::ResetAll()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_volumeEntity != InvalidEntityId)
        {
            if (auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity))
            {
                auto& s = ppv->settings;
                if (m_ppvBaseline.valid)
                {
                    s.bOverride_SplitAmount = m_ppvBaseline.bOverrideSplitAmount;
                    s.bOverride_SplitAngleDeg = m_ppvBaseline.bOverrideSplitAngleDeg;
                    s.bOverride_SplitLineOffset = m_ppvBaseline.bOverrideSplitLineOffset;
                    s.bOverride_SplitFeather = m_ppvBaseline.bOverrideSplitFeather;
                    s.bOverride_SplitFxIntensity = m_ppvBaseline.bOverrideSplitFxIntensity;
                    s.bOverride_SplitFxWidth = m_ppvBaseline.bOverrideSplitFxWidth;
                    s.bOverride_SplitFxSpeed = m_ppvBaseline.bOverrideSplitFxSpeed;
                    s.bOverride_SplitFxTimeSec = m_ppvBaseline.bOverrideSplitFxTimeSec;
                    s.bOverride_Exposure = m_ppvBaseline.bOverrideExposure;
                    s.bOverride_BloomThreshold = m_ppvBaseline.bOverrideBloomThreshold;
                    s.bOverride_BloomKnee = m_ppvBaseline.bOverrideBloomKnee;
                    s.bOverride_BloomIntensity = m_ppvBaseline.bOverrideBloomIntensity;
                    s.bOverride_BloomGaussianIntensity = m_ppvBaseline.bOverrideBloomGaussianIntensity;
                    s.bOverride_BloomRadius = m_ppvBaseline.bOverrideBloomRadius;

                    s.splitAmount = m_ppvBaseline.splitAmount;
                    s.splitAngleDeg = m_ppvBaseline.splitAngleDeg;
                    s.splitLineOffset = m_ppvBaseline.splitLineOffset;
                    s.splitFeather = m_ppvBaseline.splitFeather;
                    s.splitFxIntensity = m_ppvBaseline.splitFxIntensity;
                    s.splitFxWidth = m_ppvBaseline.splitFxWidth;
                    s.splitFxSpeed = m_ppvBaseline.splitFxSpeed;
                    s.splitFxTimeSec = m_ppvBaseline.splitFxTimeSec;
                    s.exposure = m_ppvBaseline.exposure;
                    s.bloomThreshold = m_ppvBaseline.bloomThreshold;
                    s.bloomKnee = m_ppvBaseline.bloomKnee;
                    s.bloomIntensity = m_ppvBaseline.bloomIntensity;
                    s.bloomGaussianIntensity = m_ppvBaseline.bloomGaussianIntensity;
                    s.bloomRadius = m_ppvBaseline.bloomRadius;
                }
                else
                {
                    s.bOverride_SplitAmount = true;
                    s.splitAmount = 0.0f;
                    s.bOverride_SplitFxIntensity = true;
                    s.bOverride_SplitFxWidth = true;
                    s.bOverride_SplitFxSpeed = true;
                    s.bOverride_SplitFxTimeSec = true;
                    s.splitFxIntensity = 0.0f;
                    s.splitFxWidth = 0.01f;
                    s.splitFxSpeed = 30.0f;
                    s.splitFxTimeSec = 0.0f;
                }
            }
        }

        if (ResolveTargetCamera() && m_cameraEntity != InvalidEntityId)
        {
            if (auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity))
            {
                cam->SetFov(m_baseFov);
            }
        }

        if (m_activeCutVfx != InvalidEntityId && world->GetComponent<TransformComponent>(m_activeCutVfx))
        {
            ApplyCutVfxRuntimeRecursive(m_activeCutVfx, 0.0f, false, false);
            const float despawn = std::max(0.0f, Get_m_cutVfxDespawnDelaySec());
            if (despawn > 0.0f)
                world->ScheduleDelayedDestruction(m_activeCutVfx, despawn);
            else
                world->DestroyEntity(m_activeCutVfx);
        }

        m_shakeTriggered = false;
        m_vfxTriggered = false;
        m_elapsedSec = 0.0f;
    }

    float CameraHalfCutDemo::GetTotalDuration() const
    {
        return
            std::max(0.0f, Get_m_attackSec()) +
            std::max(0.0f, Get_m_holdSec()) +
            std::max(0.0f, Get_m_releaseSec());
    }

    float CameraHalfCutDemo::EvaluateSplitAmount(float timeSec) const
    {
        const float peak = Get_m_peakAmount();
        const float attack = std::max(0.0f, Get_m_attackSec());
        const float hold = std::max(0.0f, Get_m_holdSec());
        const float release = std::max(0.0f, Get_m_releaseSec());

        float t = std::max(0.0f, timeSec);
        if (attack > 0.0f && t < attack)
            return peak * EaseOutCubic(t / attack);

        t -= attack;
        if (t < hold)
            return peak;

        t -= hold;
        if (release <= 0.0f)
            return 0.0f;

        return peak * (1.0f - EaseInCubic(t / release));
    }

    float CameraHalfCutDemo::EvaluateFlash01(float timeSec) const
    {
        const float attack = std::max(0.0f, Get_m_attackSec());
        const float hold = std::max(0.0f, Get_m_holdSec());
        const float release = std::max(0.0f, Get_m_releaseSec());

        float t = std::max(0.0f, timeSec);
        if (t < attack)
            return 0.0f;

        t -= attack;
        if (t < hold)
            return 1.0f;

        t -= hold;
        if (release <= 0.0f)
            return 0.0f;

        return 1.0f - EaseInCubic(t / release);
    }

    float CameraHalfCutDemo::EvaluateFovCurveValue(float timeSec) const
    {
        if (!Get_m_useFovCurveAsset() || !m_fovCurveLoaded || m_fovCurveAsset.keys.empty())
            return -1.0f;

        const float effectTotal = std::max(GetTotalDuration(), 1e-6f);
        const float normalized = Saturate(std::max(0.0f, timeSec) / effectTotal);
        const float scaledNormalized = Saturate(normalized * std::max(0.0f, Get_m_fovCurveTimeScale()));

        const float curveDuration = m_fovCurveAsset.GetDuration();
        const float curveTime = (curveDuration > 0.0f) ? (scaledNormalized * curveDuration) : 0.0f;

        float v = m_fovCurveAsset.Evaluate(curveTime);
        v = v * Get_m_fovCurveValueScale() + Get_m_fovCurveValueBias();
        if (Get_m_fovCurveClamp01())
            v = Saturate(v);

        return v;
    }

    float CameraHalfCutDemo::EvaluateFov(float timeSec) const
    {
        const float baseFov = m_baseFov;
        const float zoomFov = std::clamp(Get_m_zoomFov(), 1.0f, 170.0f);

        const float curveV = EvaluateFovCurveValue(timeSec);
        if (curveV >= 0.0f)
        {
            return std::clamp(LerpUnclamped(baseFov, zoomFov, curveV), 1.0f, 170.0f);
        }

        const float attack = std::max(0.0f, Get_m_attackSec());
        const float hold = std::max(0.0f, Get_m_holdSec());
        const float release = std::max(0.0f, Get_m_releaseSec());

        float t = std::max(0.0f, timeSec);
        if (attack > 0.0f && t < attack)
            return Lerp(baseFov, zoomFov, EaseOutCubic(t / attack));

        t -= attack;
        if (t < hold)
            return zoomFov;

        t -= hold;
        if (release <= 0.0f)
            return baseFov;

        return Lerp(zoomFov, baseFov, EaseInCubic(t / release));
    }
}
