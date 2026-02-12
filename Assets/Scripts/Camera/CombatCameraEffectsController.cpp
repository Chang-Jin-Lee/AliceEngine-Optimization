#include "CombatCameraEffectsController.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Resources/ResourceManager.h"

#include "../Combat/C_CombatContracts.h"
#include "../Combat/C_CombatResolver.h"
#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CombatCameraEffectsController);

    namespace
    {
        float Saturate(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float LerpUnclamped(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float EaseOutCubic(float t)
        {
            t = Saturate(t);
            const float inv = 1.0f - t;
            return 1.0f - (inv * inv * inv);
        }

        float ExpAlpha(float tauSec, float dt)
        {
            if (tauSec <= 1e-6f)
                return 1.0f;
            return 1.0f - std::exp(-dt / tauSec);
        }

        C_CombatSessionComponent* FindSession(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;

            const GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "C_CombatSessionComponent" && sc.instance)
                    return static_cast<C_CombatSessionComponent*>(sc.instance.get());
            }
            return nullptr;
        }

        CombatCameraEffectsController* FindCombatCameraEffectsController(World& world, EntityId ownerId)
        {
            if (ownerId == InvalidEntityId)
                return nullptr;

            auto* scripts = world.GetScripts(ownerId);
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "CombatCameraEffectsController" && sc.instance)
                    return static_cast<CombatCameraEffectsController*>(sc.instance.get());
            }
            return nullptr;
        }
    }

    void CombatCameraEffectsController::Start()
    {
        m_runtimeSec = 0.0f;
        m_lastAppliedFovOffsetDeg = 0.0f;
        m_chargeZoomDeg = 0.0f;
        m_chargeCurrentMaxZoomDeg = std::max(Get_m_chargeMaxZoomInDeg(), 0.001f);
        m_prevStateValid = false;
        m_prevChargeActive = false;
        m_prevChargeLevel = 0;
        m_rollOverrideActive = false;
        m_groggyModeOverrideActive = false;
        m_groggyModeRemainingSec = 0.0f;
        m_parryPauseActive = false;
        m_savedParryCameraTimeScale = 1.0f;
        m_parryPauseRemainingSec = 0.0f;
        m_activePulses.clear();
        std::fill(m_lastPulseTriggerSec.begin(), m_lastPulseTriggerSec.end(), -1000.0f);
        m_lastGlobalPulseTriggerSec = -1000.0f;

        ResolveSessionAndActors(true);
        ResolveCameraEntity();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();
        RefreshCurveAssets();
        TryBindResolveDelegate();
    }

    void CombatCameraEffectsController::OnDisable()
    {
        UnbindResolveDelegateSafe();

        if (World* world = GetWorld())
        {
            if (m_cameraEntity != InvalidEntityId)
            {
                if (auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity))
                {
                    const float restoredFov = std::clamp(cam->GetFov() - m_lastAppliedFovOffsetDeg, 1.0f, 170.0f);
                    cam->SetFov(restoredFov);
                }

                if (auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity))
                {
                    if (m_rollOverrideActive)
                    {
                        follow->positionDamping = m_savedRollPositionDamping;
                        follow->rotationDamping = m_savedRollRotationDamping;
                    }

                    if (m_groggyModeOverrideActive)
                    {
                        follow->mode = m_savedFollowMode;
                    }

                    if (m_parryPauseActive)
                    {
                        follow->cameraTimeScale = m_savedParryCameraTimeScale;
                    }
                }
            }
        }

        m_rollOverrideActive = false;
        m_groggyModeOverrideActive = false;
        m_groggyModeRemainingSec = 0.0f;
        m_parryPauseActive = false;
        m_parryPauseRemainingSec = 0.0f;

        RestorePostProcessBaseline();

        m_lastAppliedFovOffsetDeg = 0.0f;
        m_chargeZoomDeg = 0.0f;
        m_activePulses.clear();
    }

    void CombatCameraEffectsController::OnDestroy()
    {
        OnDisable();
    }

    void CombatCameraEffectsController::Update(float deltaTime)
    {
        const float dt = std::max(0.0f, deltaTime);
        m_runtimeSec += dt;

        ResolveSessionAndActors(false);
        TryBindResolveDelegate();
        ResolveCameraEntity();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();
        RefreshCurveAssets();

        UpdateStateDrivenEffects(dt);
        const PulseAccumulation pulseAccumulation = UpdateActivePulses(dt);
        ApplyOutputs(dt, pulseAccumulation);
    }

    void CombatCameraEffectsController::ResolveSessionAndActors(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
            return;

        C_CombatSessionComponent* foundSession = FindSession(*world, Get_m_sessionEntityName());
        if (foundSession != m_session)
        {
            UnbindResolveDelegateSafe();
            m_session = foundSession;
        }

        m_playerId = InvalidEntityId;
        m_bossId = InvalidEntityId;

        if (!Get_m_playerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerEntityName());
            if (go.IsValid())
                m_playerId = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[CombatCameraEffectsController] Player entity '%s' not found.", Get_m_playerEntityName().c_str());
        }

        if (!Get_m_bossEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossEntityName());
            if (go.IsValid())
                m_bossId = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[CombatCameraEffectsController] Boss entity '%s' not found.", Get_m_bossEntityName().c_str());
        }
    }

    void CombatCameraEffectsController::TryBindResolveDelegate()
    {
        if (!m_session)
            return;

        if (m_resolveListenerId == 0)
        {
            World* const boundWorld = GetWorld();
            const EntityId ownerId = GetOwnerId();
            m_resolveListenerId = m_session->AddOnCombatResolvedCameraListener(
                [boundWorld, ownerId](EntityId victimId,
                                      EntityId attackerId,
                                      std::uint8_t resolveResult,
                                      float damage,
                                      const DirectX::XMFLOAT3& hitPosWS)
                {
                    if (!boundWorld)
                        return;

                    CombatCameraEffectsController* self = FindCombatCameraEffectsController(*boundWorld, ownerId);
                    if (!self)
                        return;

                    self->OnCombatResolve(victimId, attackerId, resolveResult, damage, hitPosWS);
                });
        }
    }

    void CombatCameraEffectsController::UnbindResolveDelegateSafe()
    {
        if (m_resolveListenerId != 0)
        {
            if (World* world = GetWorld())
            {
                if (C_CombatSessionComponent* session = FindSession(*world, Get_m_sessionEntityName()))
                {
                    session->RemoveOnCombatResolvedCameraListener(m_resolveListenerId);
                }
            }
        }
        m_resolveListenerId = 0;
    }

    void CombatCameraEffectsController::ResolveCameraEntity()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_cameraEntity != InvalidEntityId)
        {
            if (world->GetComponent<CameraComponent>(m_cameraEntity))
                return;
            m_cameraEntity = InvalidEntityId;
        }

        if (!Get_m_cameraName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_cameraName());
            if (go.IsValid() && world->GetComponent<CameraComponent>(go.id()))
            {
                m_cameraEntity = go.id();
                return;
            }
        }

        if (world->GetComponent<CameraComponent>(GetOwnerId()))
        {
            m_cameraEntity = GetOwnerId();
            return;
        }

        for (const auto& [id, cam] : world->GetComponents<CameraComponent>())
        {
            if (cam.primary)
            {
                m_cameraEntity = id;
                return;
            }
        }

        for (const auto& [id, _] : world->GetComponents<CameraComponent>())
        {
            m_cameraEntity = id;
            return;
        }
    }

    bool CombatCameraEffectsController::EnsurePostProcessVolume()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        auto* ppv = (m_volumeEntity == InvalidEntityId)
            ? nullptr
            : world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);

        if (!ppv && !Get_m_postProcessVolumeName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_postProcessVolumeName());
            if (go.IsValid())
            {
                ppv = world->GetComponent<PostProcessVolumeComponent>(go.id());
                if (ppv)
                    m_volumeEntity = go.id();
            }
        }

        if (!ppv && Get_m_autoCreatePostProcessVolume())
        {
            const EntityId id = world->CreateEntity();
            world->SetEntityName(id, Get_m_postProcessVolumeName().empty() ? "CombatCameraImpactVolume" : Get_m_postProcessVolumeName());
            m_volumeEntity = id;

            auto& tr = world->AddComponent<TransformComponent>(id);
            tr.enabled = true;
            tr.visible = true;
            tr.position = { 0.0f, 0.0f, 0.0f };
            tr.rotation = { 0.0f, 0.0f, 0.0f };
            tr.scale = { 1.0f, 1.0f, 1.0f };

            ppv = &world->AddComponent<PostProcessVolumeComponent>(id);
            ppv->shape = PostProcessVolumeShape::Box;
            ppv->boxSize = { 1.0f, 1.0f, 1.0f };
            ppv->blendRadius = 0.0f;
            ppv->blendWeight = 1.0f;
        }

        if (!ppv)
            return false;

        if (m_volumeEntity != InvalidEntityId)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(m_volumeEntity))
            {
                tr->enabled = true;
                tr->visible = true;
            }
        }

        ppv->unbound = true;
        ppv->blendWeight = 1.0f;
        ppv->priority = Get_m_postProcessPriority();
        return true;
    }

    void CombatCameraEffectsController::CapturePostProcessBaseline()
    {
        if (m_ppvBaseline.valid)
            return;

        World* world = GetWorld();
        if (!world || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        const auto& s = ppv->settings;
        m_ppvBaseline.valid = true;
        m_ppvBaseline.bOverrideImpactBlurIntensity = s.bOverride_ImpactBlurIntensity;
        m_ppvBaseline.bOverrideImpactBlurRadius = s.bOverride_ImpactBlurRadius;
        m_ppvBaseline.bOverrideImpactBlurCenterX = s.bOverride_ImpactBlurCenterX;
        m_ppvBaseline.bOverrideImpactBlurCenterY = s.bOverride_ImpactBlurCenterY;
        m_ppvBaseline.impactBlurIntensity = s.impactBlurIntensity;
        m_ppvBaseline.impactBlurRadius = s.impactBlurRadius;
        m_ppvBaseline.impactBlurCenterX = s.impactBlurCenterX;
        m_ppvBaseline.impactBlurCenterY = s.impactBlurCenterY;
    }

    void CombatCameraEffectsController::RestorePostProcessBaseline()
    {
        if (!m_ppvBaseline.valid)
            return;

        World* world = GetWorld();
        if (!world || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        auto& s = ppv->settings;
        s.bOverride_ImpactBlurIntensity = m_ppvBaseline.bOverrideImpactBlurIntensity;
        s.bOverride_ImpactBlurRadius = m_ppvBaseline.bOverrideImpactBlurRadius;
        s.bOverride_ImpactBlurCenterX = m_ppvBaseline.bOverrideImpactBlurCenterX;
        s.bOverride_ImpactBlurCenterY = m_ppvBaseline.bOverrideImpactBlurCenterY;
        s.impactBlurIntensity = m_ppvBaseline.impactBlurIntensity;
        s.impactBlurRadius = m_ppvBaseline.impactBlurRadius;
        s.impactBlurCenterX = m_ppvBaseline.impactBlurCenterX;
        s.impactBlurCenterY = m_ppvBaseline.impactBlurCenterY;
    }

    void CombatCameraEffectsController::RefreshCurveAssets()
    {
        auto loadCurveAsset = [this](const std::string& logicalPath, UICurveAsset& outAsset, bool& outLoaded, bool& outWarned)
        {
            outLoaded = false;
            if (logicalPath.empty())
                return;

            std::filesystem::path resolvedPath = logicalPath;
            if (auto* rm = Resources())
                resolvedPath = rm->Resolve(logicalPath);

            UICurveAsset asset{};
            if (!LoadUICurveAsset(resolvedPath, asset) || asset.keys.empty())
            {
                if (!outWarned && Get_m_enableLogs())
                {
                    ALICE_LOG_WARN("[CombatCameraEffectsController] Failed to load curve: %s", logicalPath.c_str());
                    outWarned = true;
                }
                return;
            }

            asset.Sort();
            asset.RecalcAutoTangents();
            outAsset = std::move(asset);
            outLoaded = true;
            outWarned = false;
        };

        if (!Get_m_usePulseCurveAsset())
        {
            m_pulseCurveLoaded = false;
        }
        else if (m_loadedPulseCurvePath != Get_m_pulseCurvePath())
        {
            m_loadedPulseCurvePath = Get_m_pulseCurvePath();
            loadCurveAsset(m_loadedPulseCurvePath, m_pulseCurveAsset, m_pulseCurveLoaded, m_pulseCurveWarned);
        }

        if (!Get_m_useChargeCurveAsset())
        {
            m_chargeCurveLoaded = false;
        }
        else if (m_loadedChargeCurvePath != Get_m_chargeCurvePath())
        {
            m_loadedChargeCurvePath = Get_m_chargeCurvePath();
            loadCurveAsset(m_loadedChargeCurvePath, m_chargeCurveAsset, m_chargeCurveLoaded, m_chargeCurveWarned);
        }
    }

    float CombatCameraEffectsController::EvaluatePulseEnvelope01(float t01) const
    {
        if (Get_m_usePulseCurveAsset() && m_pulseCurveLoaded)
        {
            const float curveTime = std::max(0.0f, t01) * std::max(0.0f, Get_m_pulseCurveTimeScale());
            float value = m_pulseCurveAsset.Evaluate(curveTime);
            value = value * Get_m_pulseCurveValueScale() + Get_m_pulseCurveValueBias();
            return Get_m_pulseCurveClamp01() ? Saturate(value) : std::max(0.0f, value);
        }

        return 1.0f - EaseOutCubic(t01);
    }

    float CombatCameraEffectsController::EvaluateChargeEnvelope01(float t01) const
    {
        if (Get_m_useChargeCurveAsset() && m_chargeCurveLoaded)
        {
            const float curveTime = std::max(0.0f, t01) * std::max(0.0f, Get_m_chargeCurveTimeScale());
            float value = m_chargeCurveAsset.Evaluate(curveTime);
            value = value * Get_m_chargeCurveValueScale() + Get_m_chargeCurveValueBias();
            return Get_m_chargeCurveClamp01() ? Saturate(value) : std::max(0.0f, value);
        }

        return Saturate(t01);
    }

    void CombatCameraEffectsController::UpdateRollAssist(bool dodgeActive)
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        if (!follow)
            return;

        if (!Get_m_rollAssistEnabled())
        {
            if (m_rollOverrideActive)
            {
                follow->positionDamping = m_savedRollPositionDamping;
                follow->rotationDamping = m_savedRollRotationDamping;
                m_rollOverrideActive = false;
            }
            return;
        }

        if (dodgeActive)
        {
            if (!m_rollOverrideActive)
            {
                m_savedRollPositionDamping = follow->positionDamping;
                m_savedRollRotationDamping = follow->rotationDamping;
                m_rollOverrideActive = true;
            }

            follow->positionDamping = std::max(0.0f, Get_m_rollPositionDamping());
            follow->rotationDamping = std::max(0.0f, Get_m_rollRotationDamping());
        }
        else if (m_rollOverrideActive)
        {
            follow->positionDamping = m_savedRollPositionDamping;
            follow->rotationDamping = m_savedRollRotationDamping;
            m_rollOverrideActive = false;
        }
    }

    void CombatCameraEffectsController::UpdateGroggyFollowMode(float deltaTime)
    {
        if (!m_groggyModeOverrideActive)
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
        {
            m_groggyModeOverrideActive = false;
            m_groggyModeRemainingSec = 0.0f;
            return;
        }

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        if (!follow)
        {
            m_groggyModeOverrideActive = false;
            m_groggyModeRemainingSec = 0.0f;
            return;
        }

        m_groggyModeRemainingSec -= std::max(0.0f, deltaTime);
        if (m_groggyModeRemainingSec <= 0.0f)
        {
            follow->mode = m_savedFollowMode;
            m_groggyModeOverrideActive = false;
            m_groggyModeRemainingSec = 0.0f;
        }
    }

    void CombatCameraEffectsController::UpdateStateDrivenEffects(float deltaTime)
    {
        if (!m_session)
        {
            const float alpha = ExpAlpha(std::max(Get_m_chargeReleaseSec(), 1e-6f), deltaTime);
            m_chargeZoomDeg += (0.0f - m_chargeZoomDeg) * alpha;
            UpdateRollAssist(false);
            UpdateGroggyFollowMode(deltaTime);
            UpdateParryPause(deltaTime);
            m_prevStateValid = false;
            m_prevChargeActive = false;
            m_prevChargeLevel = 0;
            return;
        }

        const Combat::ActionState playerState = m_session->GetPlayerState();
        const Combat::ActionState bossState = m_session->GetBossState();
        const Combat::ActionFlags playerFlags = m_session->GetPlayerFlags();

        if (m_prevStateValid)
        {
            if (Get_m_guardEnterEnabled() &&
                playerState == Combat::ActionState::Guard &&
                m_prevPlayerState != Combat::ActionState::Guard)
            {
                TriggerGuardEnterPulse();
            }

            if (Get_m_breakEnabled() &&
                playerState == Combat::ActionState::GuardBreakWeak &&
                m_prevPlayerState != Combat::ActionState::GuardBreakWeak)
            {
                TriggerBreakPulse(nullptr);
            }

            if (Get_m_groggyEnabled() &&
                bossState == Combat::ActionState::Groggy &&
                m_prevBossState != Combat::ActionState::Groggy)
            {
                TriggerGroggyPulse(nullptr);
            }
        }

        const bool chargeActive = Get_m_chargeEnabled() && playerFlags.chargeActive && (playerFlags.chargeLevel > 0);
        const int chargeLevel = std::max(0, playerFlags.chargeLevel);
        if (chargeActive && (!m_prevChargeActive || chargeLevel > m_prevChargeLevel))
            TriggerGamepadRumble(Get_m_gamepadRumbleChargeDurationSec());
        const float levelScale = 1.0f + std::max(0, chargeLevel - 1) * std::max(0.0f, Get_m_chargePerLevelScale());
        const float targetZoom = chargeActive
            ? std::max(0.0f, Get_m_chargeMaxZoomInDeg()) * levelScale
            : 0.0f;

        m_chargeCurrentMaxZoomDeg = std::max(std::max(targetZoom, m_chargeCurrentMaxZoomDeg), 0.001f);

        const float tau = chargeActive
            ? std::max(Get_m_chargeResponseSec(), 1e-6f)
            : std::max(Get_m_chargeReleaseSec(), 1e-6f);
        const float alpha = ExpAlpha(tau, deltaTime);
        m_chargeZoomDeg += (targetZoom - m_chargeZoomDeg) * alpha;
        if (!chargeActive && std::abs(m_chargeZoomDeg) < 1e-4f)
            m_chargeZoomDeg = 0.0f;

        m_prevChargeActive = chargeActive;
        m_prevChargeLevel = chargeActive ? chargeLevel : 0;

        UpdateRollAssist(playerState == Combat::ActionState::Dodge);
        UpdateGroggyFollowMode(deltaTime);
        UpdateParryPause(deltaTime);

        m_prevPlayerState = playerState;
        m_prevBossState = bossState;
        m_prevStateValid = true;
    }

    void CombatCameraEffectsController::UpdateParryPause(float deltaTime)
    {
        if (!m_parryPauseActive)
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
        {
            m_parryPauseActive = false;
            m_parryPauseRemainingSec = 0.0f;
            return;
        }

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        if (!follow)
        {
            m_parryPauseActive = false;
            m_parryPauseRemainingSec = 0.0f;
            return;
        }

        m_parryPauseRemainingSec -= std::max(0.0f, deltaTime);
        if (m_parryPauseRemainingSec <= 0.0f)
        {
            follow->cameraTimeScale = m_savedParryCameraTimeScale;
            m_parryPauseActive = false;
            m_parryPauseRemainingSec = 0.0f;
            return;
        }

        follow->cameraTimeScale = std::clamp(Get_m_parryPauseCameraTimeScale(), 0.0f, 1.0f);
    }

    float CombatCameraEffectsController::EvaluatePulseFovOffsetDeg(const ActivePulse& pulse) const
    {
        const float inSec = std::max(0.0f, pulse.fovInSec);
        const float holdSec = std::max(0.0f, pulse.fovHoldSec);
        const float outSec = std::max(0.0f, pulse.fovOutSec);
        const float zoom = pulse.fovZoomInDeg;

        float t = pulse.elapsedSec;

        if (inSec > 0.0f && t < inSec)
        {
            const float u = Saturate(t / inSec);
            return LerpUnclamped(0.0f, -zoom, EaseOutCubic(u));
        }

        t -= inSec;
        if (t < holdSec)
            return -zoom;

        t -= holdSec;
        if (outSec > 0.0f && t < outSec)
        {
            const float u = Saturate(t / outSec);
            if (u < 0.5f)
                return LerpUnclamped(-zoom, pulse.fovOvershootDeg, u * 2.0f);
            return LerpUnclamped(pulse.fovOvershootDeg, 0.0f, (u - 0.5f) * 2.0f);
        }

        return 0.0f;
    }

    CombatCameraEffectsController::PulseAccumulation CombatCameraEffectsController::UpdateActivePulses(float deltaTime)
    {
        PulseAccumulation result{};
        result.blurRadius = std::max(Get_m_defaultBlurRadius(), 0.001f);
        result.blurCenterX = Get_m_defaultBlurCenterX();
        result.blurCenterY = Get_m_defaultBlurCenterY();

        std::vector<ActivePulse> alive;
        alive.reserve(m_activePulses.size());

        float weightedCenterX = 0.0f;
        float weightedCenterY = 0.0f;
        float weightedRadius = 0.0f;
        float blurWeightSum = 0.0f;

        for (auto pulse : m_activePulses)
        {
            pulse.elapsedSec += std::max(0.0f, deltaTime);
            result.fovOffsetDeg += EvaluatePulseFovOffsetDeg(pulse);

            if (pulse.blurDurationSec > 0.0f && pulse.elapsedSec <= pulse.blurDurationSec)
            {
                const float blurT = Saturate(pulse.elapsedSec / pulse.blurDurationSec);
                const float envelope = EvaluatePulseEnvelope01(blurT);
                const float contribution = std::max(0.0f, pulse.blurIntensity * envelope);
                if (contribution > 1e-6f)
                {
                    result.blurIntensity += contribution;
                    weightedCenterX += pulse.centerX * contribution;
                    weightedCenterY += pulse.centerY * contribution;
                    weightedRadius += pulse.blurRadius * contribution;
                    blurWeightSum += contribution;
                }
            }

            if (pulse.elapsedSec < pulse.totalSec)
                alive.push_back(pulse);
        }

        m_activePulses.swap(alive);

        if (blurWeightSum > 1e-6f)
        {
            result.blurCenterX = Saturate(weightedCenterX / blurWeightSum);
            result.blurCenterY = Saturate(weightedCenterY / blurWeightSum);
            result.blurRadius = std::max(0.001f, weightedRadius / blurWeightSum);
        }

        result.blurIntensity = std::clamp(result.blurIntensity, 0.0f, std::max(0.0f, Get_m_maxBlurIntensity()));
        return result;
    }

    void CombatCameraEffectsController::ApplyOutputs(float deltaTime, const PulseAccumulation& pulseAccumulation)
    {
        World* world = GetWorld();
        if (!world)
            return;

        ResolveCameraEntity();
        if (m_cameraEntity == InvalidEntityId)
            return;

        auto* camera = world->GetComponent<CameraComponent>(m_cameraEntity);
        if (!camera)
            return;

        const float maxCharge = std::max(m_chargeCurrentMaxZoomDeg, 0.001f);
        const float chargeNorm = Saturate(m_chargeZoomDeg / maxCharge);
        const float chargeWeight = EvaluateChargeEnvelope01(chargeNorm);

        const bool suspendFov = Get_m_suspendFovWhenFatalActive()
            && m_session
            && m_session->IsFatalActive();

        if (suspendFov)
        {
            if (std::abs(m_lastAppliedFovOffsetDeg) > 1e-6f)
            {
                const float restoredFov = std::clamp(camera->GetFov() - m_lastAppliedFovOffsetDeg, 1.0f, 170.0f);
                camera->SetFov(restoredFov);
            }
            m_lastAppliedFovOffsetDeg = 0.0f;
        }
        else
        {
            const float observedFov = camera->GetFov();
            const float baseFov = std::clamp(observedFov - m_lastAppliedFovOffsetDeg, 1.0f, 170.0f);

            const float chargeFovOffsetDeg = -m_chargeZoomDeg * chargeWeight;
            const float totalFovOffsetDeg = pulseAccumulation.fovOffsetDeg + chargeFovOffsetDeg;
            camera->SetFov(std::clamp(baseFov + totalFovOffsetDeg, 1.0f, 170.0f));
            m_lastAppliedFovOffsetDeg = totalFovOffsetDeg;
        }

        const float chargeBlurIntensity = std::max(0.0f, Get_m_chargeBlurIntensity()) * chargeNorm * chargeWeight;
        const float chargeBlurRadius = std::max(0.001f, Get_m_chargeBlurRadius());

        const float blurFromPulse = pulseAccumulation.blurIntensity;
        const float blurTotal = std::clamp(blurFromPulse + chargeBlurIntensity, 0.0f, std::max(0.0f, Get_m_maxBlurIntensity()));

        float blurCenterX = pulseAccumulation.blurCenterX;
        float blurCenterY = pulseAccumulation.blurCenterY;
        float blurRadius = pulseAccumulation.blurRadius;

        if (chargeBlurIntensity > 1e-6f)
        {
            const float w0 = std::max(blurFromPulse, 1e-6f);
            const float w1 = chargeBlurIntensity;
            const float wSum = w0 + w1;

            blurCenterX = Saturate((blurCenterX * w0 + Get_m_chargeCenterX() * w1) / wSum);
            blurCenterY = Saturate((blurCenterY * w0 + Get_m_chargeCenterY() * w1) / wSum);
            blurRadius = (blurRadius * w0 + chargeBlurRadius * w1) / wSum;
        }

        ApplyImpactBlur(blurTotal, blurRadius, blurCenterX, blurCenterY);

        (void)deltaTime;
    }

    void CombatCameraEffectsController::ApplyImpactBlur(float intensity, float radius, float centerX, float centerY)
    {
        World* world = GetWorld();
        if (!world || !EnsurePostProcessVolume() || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        auto& s = ppv->settings;
        if (intensity > 1e-6f)
        {
            s.bOverride_ImpactBlurIntensity = true;
            s.bOverride_ImpactBlurRadius = true;
            s.bOverride_ImpactBlurCenterX = true;
            s.bOverride_ImpactBlurCenterY = true;
            s.impactBlurIntensity = std::clamp(intensity, 0.0f, std::max(0.0f, Get_m_maxBlurIntensity()));
            s.impactBlurRadius = std::max(0.001f, radius);
            s.impactBlurCenterX = Saturate(centerX);
            s.impactBlurCenterY = Saturate(centerY);
            return;
        }

        RestorePostProcessBaseline();
    }

    void CombatCameraEffectsController::ApplyShake(const PulseConfig& config)
    {
        if (config.shakeAmplitude <= 0.0f || config.shakeDurationSec <= 0.0f)
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* shake = world->GetComponent<CameraShakeComponent>(m_cameraEntity);
        if (!shake)
            shake = &world->AddComponent<CameraShakeComponent>(m_cameraEntity);

        const bool wasActive = shake->IsActive();
        shake->enabled = true;
        shake->frequency = std::max(0.0f, Get_m_baseShakeFrequency());
        shake->decay = std::max(0.0f, Get_m_baseShakeDecay());

        if (!wasActive)
        {
            shake->amplitude = std::max(0.0f, config.shakeAmplitude);
            shake->duration = std::max(0.0f, config.shakeDurationSec);
            shake->elapsed = 0.0f;
            shake->prevOffset = {};
            return;
        }

        shake->amplitude += std::max(0.0f, config.shakeAmplitude);
        const float newEnd = shake->elapsed + std::max(0.0f, config.shakeDurationSec);
        shake->duration = std::max(shake->duration, newEnd);
    }

    void CombatCameraEffectsController::TriggerGamepadRumble(float durationSec)
    {
        if (!Get_m_gamepadRumbleEnabled())
            return;
        if (m_session && m_session->Get_m_disableLegacyCameraRumble())
            return;

        auto* input = Input();
        if (!input)
            return;

        const int playerIndex = std::clamp(Get_m_gamepadRumblePlayerIndex(), 0, 3);
        if (!input->GetGamepadConnected(playerIndex))
            return;

        const float duration = std::max(0.0f, durationSec);
        if (duration <= 0.0f)
            return;

        const float leftMotor = std::clamp(Get_m_gamepadRumbleLeftMotor(), 0.0f, 1.0f);
        const float rightMotor = std::clamp(Get_m_gamepadRumbleRightMotor(), 0.0f, 1.0f);
        if (leftMotor <= 0.0f && rightMotor <= 0.0f)
            return;

        input->PlayGamepadVibration(
            playerIndex,
            leftMotor,
            rightMotor,
            duration,
            GamepadVibrationBlend::Override);
    }

    DirectX::XMFLOAT2 CombatCameraEffectsController::ResolvePulseCenter(const DirectX::XMFLOAT3* hitPosWS) const
    {
        DirectX::XMFLOAT2 center(Get_m_defaultBlurCenterX(), Get_m_defaultBlurCenterY());
        center.x = Saturate(center.x);
        center.y = Saturate(center.y);

        if (!Get_m_useHitPosCenter() || !hitPosWS)
            return center;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return center;

        const auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity);
        if (!tr || !tr->enabled)
            return center;

        const DirectX::XMFLOAT3 toHit{
            hitPosWS->x - tr->position.x,
            hitPosWS->y - tr->position.y,
            hitPosWS->z - tr->position.z
        };

        using namespace DirectX;
        const XMVECTOR to = XMLoadFloat3(&toHit);
        const float lenSq = XMVectorGetX(XMVector3LengthSq(to));
        if (lenSq <= 1e-8f)
            return center;

        const XMMATRIX rot = XMMatrixRotationRollPitchYaw(tr->rotation.x, tr->rotation.y, tr->rotation.z);
        XMFLOAT3 right{};
        XMFLOAT3 up{};
        XMStoreFloat3(&right, XMVector3Normalize(rot.r[0]));
        XMStoreFloat3(&up, XMVector3Normalize(rot.r[1]));
        XMFLOAT3 toN{};
        XMStoreFloat3(&toN, XMVector3Normalize(to));

        const float x = toN.x * right.x + toN.y * right.y + toN.z * right.z;
        const float y = toN.x * up.x + toN.y * up.y + toN.z * up.z;

        center.x = Saturate(center.x + x * Get_m_hitCenterScaleX());
        center.y = Saturate(center.y - y * Get_m_hitCenterScaleY());
        return center;
    }

    void CombatCameraEffectsController::TriggerPulse(PulseKind kind, const PulseConfig& config, const DirectX::XMFLOAT3* hitPosWS)
    {
        if (!config.enabled)
            return;

        const std::size_t idx = static_cast<std::size_t>(kind);
        const float localCooldown = std::max(0.0f, config.cooldownSec);
        const float globalCooldown = std::max(0.0f, Get_m_globalPulseCooldownSec());

        if ((m_runtimeSec - m_lastPulseTriggerSec[idx]) < localCooldown)
            return;
        if ((m_runtimeSec - m_lastGlobalPulseTriggerSec) < globalCooldown)
            return;

        ActivePulse pulse{};
        pulse.elapsedSec = 0.0f;
        pulse.blurDurationSec = config.overrideTiming
            ? std::max(0.001f, config.blurDurationSec)
            : std::max(0.001f, Get_m_baseBlurDurationSec());
        pulse.fovInSec = config.overrideTiming
            ? std::max(0.0f, config.fovInSec)
            : std::max(0.0f, Get_m_baseFovInSec());
        pulse.fovHoldSec = config.overrideTiming
            ? std::max(0.0f, config.fovHoldSec)
            : std::max(0.0f, Get_m_baseFovHoldSec());
        pulse.fovOutSec = config.overrideTiming
            ? std::max(0.0f, config.fovOutSec)
            : std::max(0.0f, Get_m_baseFovOutSec());
        pulse.totalSec = std::max(
            pulse.blurDurationSec,
            pulse.fovInSec + pulse.fovHoldSec + pulse.fovOutSec);
        pulse.totalSec = std::max(pulse.totalSec, 0.001f);

        pulse.blurIntensity = std::max(0.0f, config.blurIntensity);
        pulse.blurRadius = std::max(0.001f, config.blurRadius);
        pulse.fovZoomInDeg = config.fovZoomInDeg;
        pulse.fovOvershootDeg = config.fovOvershootDeg;

        const DirectX::XMFLOAT2 center = ResolvePulseCenter(hitPosWS);
        pulse.centerX = center.x;
        pulse.centerY = center.y;

        m_activePulses.push_back(pulse);
        m_lastPulseTriggerSec[idx] = m_runtimeSec;
        m_lastGlobalPulseTriggerSec = m_runtimeSec;

        ApplyShake(config);
        TriggerGamepadRumble(Get_m_gamepadRumblePulseDurationSec());
    }

    void CombatCameraEffectsController::TriggerImpactPulse(const DirectX::XMFLOAT3& hitPosWS, bool heavyHit, float heavyScaleOverride)
    {
        PulseConfig config{};
        config.enabled = Get_m_impactEnabled();
        config.blurIntensity = Get_m_impactBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_impactFovZoomInDeg();
        config.fovOvershootDeg = Get_m_impactFovOvershootDeg();
        config.shakeAmplitude = Get_m_impactShakeAmplitude();
        config.shakeDurationSec = Get_m_impactShakeDurationSec();
        config.cooldownSec = Get_m_impactCooldownSec();

        if (heavyHit)
        {
            const float mul = std::max(0.0f, heavyScaleOverride);
            config.blurIntensity *= std::max(0.0f, Get_m_impactHeavyBlurMul()) * mul;
            config.fovZoomInDeg *= std::max(0.0f, Get_m_impactHeavyFovMul()) * mul;
            config.fovOvershootDeg *= std::max(0.0f, Get_m_impactHeavyFovMul()) * mul;
            config.shakeAmplitude *= std::max(0.0f, Get_m_impactHeavyShakeMul()) * mul;
            config.shakeDurationSec *= std::max(0.0f, Get_m_impactHeavyShakeMul()) * mul;
        }

        TriggerPulse(PulseKind::Impact, config, &hitPosWS);
    }

    void CombatCameraEffectsController::TriggerBlockPulse(const DirectX::XMFLOAT3& hitPosWS)
    {
        PulseConfig config{};
        config.enabled = Get_m_blockEnabled();
        config.blurIntensity = Get_m_blockBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_blockFovZoomInDeg();
        config.fovOvershootDeg = Get_m_blockFovOvershootDeg();
        config.shakeAmplitude = Get_m_blockShakeAmplitude();
        config.shakeDurationSec = Get_m_blockShakeDurationSec();
        config.cooldownSec = Get_m_blockCooldownSec();
        TriggerPulse(PulseKind::Block, config, &hitPosWS);
    }

    void CombatCameraEffectsController::TriggerDamagePulse(const DirectX::XMFLOAT3& hitPosWS, float damage)
    {
        PulseConfig config{};
        config.enabled = Get_m_damageEnabled();
        config.blurIntensity = Get_m_damageBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_damageFovZoomInDeg();
        config.fovOvershootDeg = Get_m_damageFovOvershootDeg();
        config.shakeAmplitude = Get_m_damageShakeAmplitude();
        config.shakeDurationSec = Get_m_damageShakeDurationSec();
        config.cooldownSec = Get_m_damageCooldownSec();

        if (damage >= Get_m_damageHeavyThreshold())
        {
            const float mul = std::max(0.0f, Get_m_damageHeavyMul());
            config.blurIntensity *= mul;
            config.fovZoomInDeg *= mul;
            config.fovOvershootDeg *= mul;
            config.shakeAmplitude *= mul;
            config.shakeDurationSec *= mul;
        }

        TriggerPulse(PulseKind::Damage, config, &hitPosWS);
    }

    void CombatCameraEffectsController::TriggerBreakPulse(const DirectX::XMFLOAT3* hitPosWS)
    {
        PulseConfig config{};
        config.enabled = Get_m_breakEnabled();
        config.blurIntensity = Get_m_breakBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_breakFovZoomInDeg();
        config.fovOvershootDeg = Get_m_breakFovOvershootDeg();
        config.shakeAmplitude = Get_m_breakShakeAmplitude();
        config.shakeDurationSec = Get_m_breakShakeDurationSec();
        config.cooldownSec = Get_m_breakCooldownSec();
        TriggerPulse(PulseKind::Break, config, hitPosWS);
    }

    void CombatCameraEffectsController::TriggerParryPulse(const DirectX::XMFLOAT3& hitPosWS)
    {
        PulseConfig config{};
        config.enabled = Get_m_parryEnabled();
        config.blurIntensity = Get_m_parryBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_parryFovZoomInDeg() * std::max(0.0f, Get_m_parryFovZoomScale());
        config.fovOvershootDeg = Get_m_parryFovOvershootDeg() * std::max(0.0f, Get_m_parryFovOvershootScale());
        config.shakeAmplitude = Get_m_parryShakeAmplitude();
        config.shakeDurationSec = Get_m_parryShakeDurationSec();
        config.cooldownSec = Get_m_parryCooldownSec();
        config.overrideTiming = Get_m_parryOverrideTiming();
        config.blurDurationSec = Get_m_parryBlurDurationSec();
        config.fovInSec = Get_m_parryFovInSec();
        config.fovHoldSec = Get_m_parryFovHoldSec();
        config.fovOutSec = Get_m_parryFovOutSec();
        TriggerPulse(PulseKind::Parry, config, &hitPosWS);
        ActivateParryPause();
    }

    void CombatCameraEffectsController::TriggerGuardEnterPulse()
    {
        PulseConfig config{};
        config.enabled = Get_m_guardEnterEnabled();
        config.blurIntensity = Get_m_guardEnterBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_guardEnterFovZoomInDeg();
        config.fovOvershootDeg = Get_m_guardEnterFovOvershootDeg();
        config.shakeAmplitude = Get_m_guardEnterShakeAmplitude();
        config.shakeDurationSec = Get_m_guardEnterShakeDurationSec();
        config.cooldownSec = Get_m_guardEnterCooldownSec();
        TriggerPulse(PulseKind::GuardEnter, config, nullptr);
    }

    void CombatCameraEffectsController::TriggerGroggyPulse(const DirectX::XMFLOAT3* hitPosWS)
    {
        PulseConfig config{};
        config.enabled = Get_m_groggyEnabled();
        config.blurIntensity = Get_m_groggyBlurIntensity();
        config.blurRadius = Get_m_defaultBlurRadius();
        config.fovZoomInDeg = Get_m_groggyFovZoomInDeg();
        config.fovOvershootDeg = Get_m_groggyFovOvershootDeg();
        config.shakeAmplitude = Get_m_groggyShakeAmplitude();
        config.shakeDurationSec = Get_m_groggyShakeDurationSec();
        config.cooldownSec = Get_m_groggyCooldownSec();
        TriggerPulse(PulseKind::Groggy, config, hitPosWS);

        if (!Get_m_groggyForceFollowModeEnabled())
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        if (!follow)
            return;

        if (!m_groggyModeOverrideActive)
        {
            m_savedFollowMode = follow->mode;
            m_groggyModeOverrideActive = true;
        }

        const int forcedMode = std::clamp(Get_m_groggyFollowMode(), 0, 5);
        follow->mode = forcedMode;
        m_groggyModeRemainingSec = std::max(m_groggyModeRemainingSec, std::max(0.0f, Get_m_groggyFollowModeDurationSec()));
    }

    void CombatCameraEffectsController::ActivateParryPause()
    {
        if (!Get_m_parryPauseEnabled())
            return;

        World* world = GetWorld();
        if (!world)
            return;

        ResolveCameraEntity();
        if (m_cameraEntity == InvalidEntityId)
            return;

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        if (!follow)
            return;

        if (!m_parryPauseActive)
            m_savedParryCameraTimeScale = follow->cameraTimeScale;

        m_parryPauseActive = true;
        m_parryPauseRemainingSec = std::max(m_parryPauseRemainingSec, std::max(0.0f, Get_m_parryPauseSec()));
        follow->cameraTimeScale = std::clamp(Get_m_parryPauseCameraTimeScale(), 0.0f, 1.0f);
    }

    void CombatCameraEffectsController::OnCombatResolve(EntityId victimId,
                                                        EntityId attackerId,
                                                        std::uint8_t resolveResult,
                                                        float damage,
                                                        const DirectX::XMFLOAT3& hitPosWS)
    {
        const Combat::ResolveResult result = static_cast<Combat::ResolveResult>(resolveResult);

        switch (result)
        {
        case Combat::ResolveResult::Parry:
            TriggerParryPulse(hitPosWS);
            break;
        case Combat::ResolveResult::Guard:
            TriggerBlockPulse(hitPosWS);
            break;
        case Combat::ResolveResult::GuardBreak:
            TriggerBreakPulse(&hitPosWS);
            break;
        case Combat::ResolveResult::Hit:
        {
            if (IsPlayerEntity(victimId))
                TriggerDamagePulse(hitPosWS, damage);

            if (IsPlayerEntity(attackerId))
            {
                bool heavyHit = (damage >= Get_m_impactHeavyDamageThreshold());
                float heavyScale = 1.0f;

                if (m_session)
                {
                    const Combat::ActionFlags flags = m_session->GetPlayerFlags();
                    if (flags.chargeActive || flags.chargeLevel > 0)
                    {
                        heavyHit = true;
                        if (Get_m_chargeExecuteExtraPulseEnabled())
                            heavyScale = std::max(1.0f, Get_m_chargeExecuteExtraPulseScale());
                    }
                }

                TriggerImpactPulse(hitPosWS, heavyHit, heavyScale);
            }
            break;
        }
        case Combat::ResolveResult::None:
        default:
            break;
        }

        ResolveCameraEntity();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();
        const PulseAccumulation pulseAccumulation = UpdateActivePulses(0.0f);
        ApplyOutputs(0.0f, pulseAccumulation);
    }
}
