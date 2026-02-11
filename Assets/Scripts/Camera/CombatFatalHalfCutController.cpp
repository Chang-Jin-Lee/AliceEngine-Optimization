#include "CombatFatalHalfCutController.h"

#include <algorithm>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"

#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CombatFatalHalfCutController);

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
            return 1.0f - (inv * inv * inv);
        }

        float EaseInCubic(float t)
        {
            t = Saturate(t);
            return t * t * t;
        }

        float SmoothStep01(float t)
        {
            t = Saturate(t);
            return t * t * (3.0f - 2.0f * t);
        }

        float LerpUnclamped(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return {
                LerpUnclamped(a.x, b.x, t),
                LerpUnclamped(a.y, b.y, t),
                LerpUnclamped(a.z, b.z, t)
            };
        }

        DirectX::XMFLOAT4 EulerToQuat(const DirectX::XMFLOAT3& eulerRad)
        {
            DirectX::XMFLOAT4 q{};
            const DirectX::XMVECTOR v = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
            DirectX::XMStoreFloat4(&q, v);
            return q;
        }

        DirectX::XMFLOAT3 QuatToEuler(const DirectX::XMFLOAT4& quat)
        {
            using namespace DirectX;
            const XMVECTOR q = XMLoadFloat4(&quat);
            const XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
            XMFLOAT3 f3{};
            XMStoreFloat3(&f3, f);

            const float yaw = std::atan2(f3.x, f3.z);
            const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));
            return { pitch, yaw, 0.0f };
        }

        DirectX::XMFLOAT3 SlerpEuler(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            using namespace DirectX;
            const XMFLOAT4 qa = EulerToQuat(a);
            const XMFLOAT4 qb = EulerToQuat(b);
            XMFLOAT4 qOut{};
            XMStoreFloat4(&qOut, XMQuaternionSlerp(XMLoadFloat4(&qa), XMLoadFloat4(&qb), Saturate(t)));
            return QuatToEuler(qOut);
        }

        DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir)
        {
            const float lenXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (lenXZ <= 1e-6f && std::abs(dir.y) <= 1e-6f)
                return { 0.0f, 0.0f, 0.0f };

            const float yaw = std::atan2(dir.x, dir.z);
            const float pitch = -std::atan2(dir.y, lenXZ);
            return { pitch, yaw, 0.0f };
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
    }

    void CombatFatalHalfCutController::Start()
    {
        m_prevFatalActive = false;
        m_startedThisFatal = false;
        m_runtimeSec = 0.0f;
        m_lastSequenceStartSec = -1000.0f;
        m_phase = Phase::Idle;
        m_phaseTimerSec = 0.0f;
        m_snapApplied = false;
        m_controlsCaptured = false;
        m_ppvBaseline = {};

        ResolveSessionAndActors(true);
        ResolveCameraEntity();
        EnsurePostProcessVolume();
    }

    void CombatFatalHalfCutController::OnDisable()
    {
        AbortSequence();
    }

    void CombatFatalHalfCutController::OnDestroy()
    {
        AbortSequence();
    }

    void CombatFatalHalfCutController::Update(float deltaTime)
    {
        const float dt = std::max(0.0f, deltaTime);
        m_runtimeSec += dt;

        ResolveSessionAndActors(false);
        ResolveCameraEntity();
        EnsurePostProcessVolume();

        const bool fatalActive = (m_session && m_session->IsFatalActive());
        if (!fatalActive)
            m_startedThisFatal = false;

        if (m_phase == Phase::Idle && fatalActive && !m_startedThisFatal)
        {
            bool triggerReady = true;
            if (Get_m_useFatalProgressStart())
            {
                const float targetProgress = Saturate(Get_m_startFatalProgress01());
                const float currentProgress = Saturate(m_session ? m_session->GetFatalProgress01() : 0.0f);
                triggerReady = (currentProgress >= targetProgress);
            }
            if (triggerReady && Get_m_useFatalElapsedStart())
            {
                const float currentProgress = Saturate(m_session ? m_session->GetFatalProgress01() : 0.0f);
                const float remainSec = std::max(0.0f, m_session ? m_session->GetFatalRemainSec() : 0.0f);
                float elapsedSec = 0.0f;
                if (currentProgress >= 1.0f)
                {
                    elapsedSec = std::max(0.0f, Get_m_startFatalElapsedSec());
                }
                else if (currentProgress > 1e-5f)
                {
                    const float totalSec = remainSec / std::max(1.0f - currentProgress, 1e-5f);
                    elapsedSec = totalSec * currentProgress;
                }

                triggerReady = (elapsedSec >= std::max(0.0f, Get_m_startFatalElapsedSec()));
            }

            if (triggerReady)
            {
                const float coolSec = std::max(0.0f, Get_m_retriggerCooldownSec());
                if ((m_runtimeSec - m_lastSequenceStartSec) >= coolSec)
                {
                    StartFatalSequence();
                    if (m_phase != Phase::Idle)
                        m_startedThisFatal = true;
                }
            }
        }

        if (m_phase == Phase::HalfCut)
            UpdateHalfCut(dt);
        else if (m_phase == Phase::BlendBack)
            UpdateBlendBack(dt);

        m_prevFatalActive = fatalActive;
    }

    void CombatFatalHalfCutController::ResolveSessionAndActors(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
            return;

        m_session = FindSession(*world, Get_m_sessionEntityName());

        m_playerId = InvalidEntityId;
        m_bossId = InvalidEntityId;

        if (!Get_m_playerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerEntityName());
            if (go.IsValid())
                m_playerId = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[CombatFatalHalfCutController] Player entity '%s' not found.", Get_m_playerEntityName().c_str());
        }

        if (!Get_m_bossEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossEntityName());
            if (go.IsValid())
                m_bossId = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[CombatFatalHalfCutController] Boss entity '%s' not found.", Get_m_bossEntityName().c_str());
        }
    }

    void CombatFatalHalfCutController::ResolveCameraEntity()
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

    bool CombatFatalHalfCutController::EnsurePostProcessVolume()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        auto* current = (m_volumeEntity == InvalidEntityId)
            ? nullptr
            : world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);

        if (!current && !Get_m_postProcessVolumeName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_postProcessVolumeName());
            if (go.IsValid())
            {
                current = world->GetComponent<PostProcessVolumeComponent>(go.id());
                if (current)
                    m_volumeEntity = go.id();
            }
        }

        if (!current && Get_m_autoCreateVolume())
        {
            const EntityId id = world->CreateEntity();
            m_volumeEntity = id;
            world->SetEntityName(id, Get_m_postProcessVolumeName().empty() ? "FatalHalfCutVolume" : Get_m_postProcessVolumeName());

            auto& tr = world->AddComponent<TransformComponent>(id);
            tr.enabled = true;
            tr.visible = true;
            tr.position = { 0.0f, 0.0f, 0.0f };
            tr.rotation = { 0.0f, 0.0f, 0.0f };
            tr.scale = { 1.0f, 1.0f, 1.0f };

            auto& ppv = world->AddComponent<PostProcessVolumeComponent>(id);
            ppv.unbound = true;
            ppv.shape = PostProcessVolumeShape::Box;
            ppv.boxSize = { 1.0f, 1.0f, 1.0f };
            ppv.blendRadius = 0.0f;
            ppv.blendWeight = 1.0f;
            current = &ppv;
        }

        if (!current)
            return false;

        if (m_volumeEntity != InvalidEntityId)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(m_volumeEntity))
            {
                tr->enabled = true;
                tr->visible = true;
            }
        }

        current->unbound = true;
        current->blendWeight = 1.0f;
        current->priority = Get_m_volumePriority();
        return true;
    }

    void CombatFatalHalfCutController::CapturePostProcessBaseline()
    {
        World* world = GetWorld();
        if (!world || !EnsurePostProcessVolume() || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

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
        m_ppvBaseline.bOverrideSplitFxColorA = s.bOverride_SplitFxColorA;
        m_ppvBaseline.bOverrideSplitFxColorB = s.bOverride_SplitFxColorB;
        m_ppvBaseline.bOverrideExposure = s.bOverride_Exposure;
        m_ppvBaseline.bOverrideBloomThreshold = s.bOverride_BloomThreshold;
        m_ppvBaseline.bOverrideBloomKnee = s.bOverride_BloomKnee;
        m_ppvBaseline.bOverrideBloomIntensity = s.bOverride_BloomIntensity;
        m_ppvBaseline.bOverrideBloomGaussianIntensity = s.bOverride_BloomGaussianIntensity;
        m_ppvBaseline.bOverrideBloomRadius = s.bOverride_BloomRadius;
        m_ppvBaseline.bOverrideImpactBlurIntensity = s.bOverride_ImpactBlurIntensity;
        m_ppvBaseline.bOverrideImpactBlurRadius = s.bOverride_ImpactBlurRadius;
        m_ppvBaseline.bOverrideImpactBlurCenterX = s.bOverride_ImpactBlurCenterX;
        m_ppvBaseline.bOverrideImpactBlurCenterY = s.bOverride_ImpactBlurCenterY;

        m_ppvBaseline.splitAmount = s.splitAmount;
        m_ppvBaseline.splitAngleDeg = s.splitAngleDeg;
        m_ppvBaseline.splitLineOffset = s.splitLineOffset;
        m_ppvBaseline.splitFeather = s.splitFeather;
        m_ppvBaseline.splitFxIntensity = s.splitFxIntensity;
        m_ppvBaseline.splitFxWidth = s.splitFxWidth;
        m_ppvBaseline.splitFxSpeed = s.splitFxSpeed;
        m_ppvBaseline.splitFxTimeSec = s.splitFxTimeSec;
        m_ppvBaseline.splitFxColorA = s.splitFxColorA;
        m_ppvBaseline.splitFxColorB = s.splitFxColorB;
        m_ppvBaseline.exposure = s.exposure;
        m_ppvBaseline.bloomThreshold = s.bloomThreshold;
        m_ppvBaseline.bloomKnee = s.bloomKnee;
        m_ppvBaseline.bloomIntensity = s.bloomIntensity;
        m_ppvBaseline.bloomGaussianIntensity = s.bloomGaussianIntensity;
        m_ppvBaseline.bloomRadius = s.bloomRadius;
        m_ppvBaseline.impactBlurIntensity = s.impactBlurIntensity;
        m_ppvBaseline.impactBlurRadius = s.impactBlurRadius;
        m_ppvBaseline.impactBlurCenterX = s.impactBlurCenterX;
        m_ppvBaseline.impactBlurCenterY = s.impactBlurCenterY;
    }

    void CombatFatalHalfCutController::RestorePostProcessBaseline()
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
        s.bOverride_SplitAmount = m_ppvBaseline.bOverrideSplitAmount;
        s.bOverride_SplitAngleDeg = m_ppvBaseline.bOverrideSplitAngleDeg;
        s.bOverride_SplitLineOffset = m_ppvBaseline.bOverrideSplitLineOffset;
        s.bOverride_SplitFeather = m_ppvBaseline.bOverrideSplitFeather;
        s.bOverride_SplitFxIntensity = m_ppvBaseline.bOverrideSplitFxIntensity;
        s.bOverride_SplitFxWidth = m_ppvBaseline.bOverrideSplitFxWidth;
        s.bOverride_SplitFxSpeed = m_ppvBaseline.bOverrideSplitFxSpeed;
        s.bOverride_SplitFxTimeSec = m_ppvBaseline.bOverrideSplitFxTimeSec;
        s.bOverride_SplitFxColorA = m_ppvBaseline.bOverrideSplitFxColorA;
        s.bOverride_SplitFxColorB = m_ppvBaseline.bOverrideSplitFxColorB;
        s.bOverride_Exposure = m_ppvBaseline.bOverrideExposure;
        s.bOverride_BloomThreshold = m_ppvBaseline.bOverrideBloomThreshold;
        s.bOverride_BloomKnee = m_ppvBaseline.bOverrideBloomKnee;
        s.bOverride_BloomIntensity = m_ppvBaseline.bOverrideBloomIntensity;
        s.bOverride_BloomGaussianIntensity = m_ppvBaseline.bOverrideBloomGaussianIntensity;
        s.bOverride_BloomRadius = m_ppvBaseline.bOverrideBloomRadius;
        s.bOverride_ImpactBlurIntensity = m_ppvBaseline.bOverrideImpactBlurIntensity;
        s.bOverride_ImpactBlurRadius = m_ppvBaseline.bOverrideImpactBlurRadius;
        s.bOverride_ImpactBlurCenterX = m_ppvBaseline.bOverrideImpactBlurCenterX;
        s.bOverride_ImpactBlurCenterY = m_ppvBaseline.bOverrideImpactBlurCenterY;

        s.splitAmount = m_ppvBaseline.splitAmount;
        s.splitAngleDeg = m_ppvBaseline.splitAngleDeg;
        s.splitLineOffset = m_ppvBaseline.splitLineOffset;
        s.splitFeather = m_ppvBaseline.splitFeather;
        s.splitFxIntensity = m_ppvBaseline.splitFxIntensity;
        s.splitFxWidth = m_ppvBaseline.splitFxWidth;
        s.splitFxSpeed = m_ppvBaseline.splitFxSpeed;
        s.splitFxTimeSec = m_ppvBaseline.splitFxTimeSec;
        s.splitFxColorA = m_ppvBaseline.splitFxColorA;
        s.splitFxColorB = m_ppvBaseline.splitFxColorB;
        s.exposure = m_ppvBaseline.exposure;
        s.bloomThreshold = m_ppvBaseline.bloomThreshold;
        s.bloomKnee = m_ppvBaseline.bloomKnee;
        s.bloomIntensity = m_ppvBaseline.bloomIntensity;
        s.bloomGaussianIntensity = m_ppvBaseline.bloomGaussianIntensity;
        s.bloomRadius = m_ppvBaseline.bloomRadius;
        s.impactBlurIntensity = m_ppvBaseline.impactBlurIntensity;
        s.impactBlurRadius = m_ppvBaseline.impactBlurRadius;
        s.impactBlurCenterX = m_ppvBaseline.impactBlurCenterX;
        s.impactBlurCenterY = m_ppvBaseline.impactBlurCenterY;
    }

    bool CombatFatalHalfCutController::GetCurrentCameraPose(CameraPose& outPose) const
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return false;

        const auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity);
        const auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity);
        if (!tr || !cam)
            return false;

        outPose.position = tr->position;
        outPose.rotationRad = tr->rotation;
        outPose.fovDeg = cam->GetFov();
        return true;
    }

    void CombatFatalHalfCutController::ApplyCameraPose(const CameraPose& pose)
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        if (auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity))
        {
            tr->position = pose.position;
            tr->rotation = pose.rotationRad;
            world->MarkTransformDirty(m_cameraEntity);
        }

        if (auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity))
        {
            cam->SetFov(std::clamp(pose.fovDeg, 1.0f, 170.0f));
        }
    }

    bool CombatFatalHalfCutController::BuildSnapPoseBehindBoss(CameraPose& outPose) const
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId || m_bossId == InvalidEntityId)
            return false;

        const auto* bossTr = world->GetComponent<TransformComponent>(m_bossId);
        if (!bossTr || !bossTr->enabled)
            return false;

        const auto* playerTr = world->GetComponent<TransformComponent>(m_playerId);

        DirectX::XMFLOAT3 toPlayer{
            0.0f, 0.0f, 1.0f
        };

        if (playerTr && playerTr->enabled)
        {
            toPlayer.x = playerTr->position.x - bossTr->position.x;
            toPlayer.y = 0.0f;
            toPlayer.z = playerTr->position.z - bossTr->position.z;
        }
        else
        {
            const float yaw = bossTr->rotation.y;
            toPlayer.x = std::sin(yaw);
            toPlayer.z = std::cos(yaw);
        }

        const float len = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
        if (len <= 1e-6f)
        {
            toPlayer = { 0.0f, 0.0f, 1.0f };
        }
        else
        {
            toPlayer.x /= len;
            toPlayer.z /= len;
        }

        const DirectX::XMFLOAT3 right{ toPlayer.z, 0.0f, -toPlayer.x };
        DirectX::XMFLOAT3 cameraPos{
            bossTr->position.x - toPlayer.x * std::max(0.0f, Get_m_snapBehindDistance()) + right.x * Get_m_snapSideOffset(),
            bossTr->position.y + Get_m_snapHeight(),
            bossTr->position.z - toPlayer.z * std::max(0.0f, Get_m_snapBehindDistance()) + right.z * Get_m_snapSideOffset()
        };

        DirectX::XMFLOAT3 lookAtPos{};
        if (playerTr && playerTr->enabled)
        {
            lookAtPos = playerTr->position;
            lookAtPos.y += Get_m_lookAtPlayerYOffset();
        }
        else
        {
            lookAtPos = bossTr->position;
            lookAtPos.x += toPlayer.x;
            lookAtPos.z += toPlayer.z;
            lookAtPos.y += Get_m_lookAtBossYOffset();
        }

        const DirectX::XMFLOAT3 to{
            lookAtPos.x - cameraPos.x,
            lookAtPos.y - cameraPos.y,
            lookAtPos.z - cameraPos.z
        };

        CameraPose current{};
        if (!GetCurrentCameraPose(current))
            return false;

        outPose = current;
        outPose.position = cameraPos;
        outPose.rotationRad = DirectionToEuler(to);
        return true;
    }

    void CombatFatalHalfCutController::SaveAndDisableCameraControls()
    {
        if (m_controlsCaptured)
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        auto* lookAt = world->GetComponent<CameraLookAtComponent>(m_cameraEntity);
        auto* spring = world->GetComponent<CameraSpringArmComponent>(m_cameraEntity);
        auto* blend = world->GetComponent<CameraBlendComponent>(m_cameraEntity);

        m_savedFollowEnabled = follow ? follow->enabled : true;
        m_savedLookAtEnabled = lookAt ? lookAt->enabled : false;
        m_savedSpringArmEnabled = spring ? spring->enabled : true;
        m_savedFollowMode = follow ? follow->mode : 0;

        if (blend)
        {
            blend->active = false;
            blend->needsSnapshot = false;
        }

        if (follow && Get_m_disableFollowDuringSequence())
            follow->enabled = false;
        if (lookAt && Get_m_disableLookAtDuringSequence())
            lookAt->enabled = false;
        if (spring && Get_m_disableSpringArmDuringSequence())
            spring->enabled = false;

        m_controlsCaptured = true;
    }

    void CombatFatalHalfCutController::RestoreCameraControls()
    {
        if (!m_controlsCaptured)
            return;

        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
        {
            m_controlsCaptured = false;
            return;
        }

        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        auto* lookAt = world->GetComponent<CameraLookAtComponent>(m_cameraEntity);
        auto* spring = world->GetComponent<CameraSpringArmComponent>(m_cameraEntity);
        auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity);

        if (follow)
        {
            follow->enabled = m_savedFollowEnabled;
            follow->mode = m_savedFollowMode;

            if (tr)
            {
                follow->initialized = false;
                follow->smoothedPosition = tr->position;
                follow->smoothedRotation = tr->rotation;
                follow->yawDeg = DirectX::XMConvertToDegrees(tr->rotation.y);
                follow->pitchDeg = DirectX::XMConvertToDegrees(tr->rotation.x);
            }
        }

        if (lookAt)
            lookAt->enabled = m_savedLookAtEnabled;

        if (spring)
            spring->enabled = m_savedSpringArmEnabled;

        m_controlsCaptured = false;
    }

    void CombatFatalHalfCutController::StartFatalSequence()
    {
        ResolveCameraEntity();
        if (m_cameraEntity == InvalidEntityId)
            return;

        CameraPose current{};
        if (!GetCurrentCameraPose(current))
            return;

        m_preFatalPose = current;
        m_snapPose = current;
        m_blendFromPose = current;

        CapturePostProcessBaseline();
        SaveAndDisableCameraControls();

        m_phase = Phase::HalfCut;
        m_phaseTimerSec = 0.0f;
        m_snapApplied = false;
        m_lastSequenceStartSec = m_runtimeSec;

        const float split = EvaluateSplitAmount(0.0f);
        const float flash = EvaluateFlash01(0.0f);
        const float cutWeight = EvaluateCutWeight01(split, flash);
        ApplyCutPostProcess(split, flash, 0.0f, cutWeight);
    }

    void CombatFatalHalfCutController::UpdateHalfCut(float deltaTime)
    {
        m_phaseTimerSec += std::max(0.0f, deltaTime);

        const float split = EvaluateSplitAmount(m_phaseTimerSec);
        const float flash = EvaluateFlash01(m_phaseTimerSec);
        const float cutWeight = EvaluateCutWeight01(split, flash);

        if (Get_m_enableSnapMove() &&
            !Get_m_snapAfterRecover() &&
            !m_snapApplied &&
            m_phaseTimerSec >= std::max(0.0f, Get_m_snapAtSec()))
        {
            CameraPose snap{};
            if (BuildSnapPoseBehindBoss(snap))
            {
                m_snapPose = snap;
                m_snapApplied = true;
            }
        }

        CameraPose pose = m_snapApplied ? m_snapPose : m_preFatalPose;
        pose.fovDeg = std::clamp(pose.fovDeg - std::max(0.0f, Get_m_cutZoomInDeg()) * cutWeight, 1.0f, 170.0f);
        ApplyCameraPose(pose);
        ApplyCutPostProcess(split, flash, m_phaseTimerSec, cutWeight);

        if (m_phaseTimerSec >= GetCutTotalDurationSec())
        {
            m_blendFromPose = pose;
            RestorePostProcessBaseline();
            m_phase = Phase::BlendBack;
            m_phaseTimerSec = 0.0f;
        }
    }

    void CombatFatalHalfCutController::UpdateBlendBack(float deltaTime)
    {
        m_phaseTimerSec += std::max(0.0f, deltaTime);

        const float duration = std::max(0.0f, Get_m_blendBackDurationSec());
        if (duration <= 1e-6f)
        {
            ApplyCameraPose(m_preFatalPose);
            FinishSequence();
            return;
        }

        const float t = Saturate(m_phaseTimerSec / duration);
        const float s = SmoothStep01(t);

        CameraPose pose{};
        pose.position = LerpVec(m_blendFromPose.position, m_preFatalPose.position, s);
        pose.rotationRad = SlerpEuler(m_blendFromPose.rotationRad, m_preFatalPose.rotationRad, s);
        pose.fovDeg = LerpUnclamped(m_blendFromPose.fovDeg, m_preFatalPose.fovDeg, s);
        ApplyCameraPose(pose);

        if (t >= 1.0f)
            FinishSequence();
    }

    void CombatFatalHalfCutController::FinishSequence()
    {
        if (Get_m_enableSnapMove() &&
            Get_m_snapAfterRecover() &&
            !m_snapApplied)
        {
            CameraPose snap{};
            if (BuildSnapPoseBehindBoss(snap))
            {
                ApplyCameraPose(snap);
                m_snapApplied = true;
            }
        }

        m_phase = Phase::Idle;
        m_phaseTimerSec = 0.0f;
        m_snapApplied = false;

        RestorePostProcessBaseline();
        RestoreCameraControls();
    }

    void CombatFatalHalfCutController::AbortSequence()
    {
        m_phase = Phase::Idle;
        m_phaseTimerSec = 0.0f;
        m_snapApplied = false;

        RestorePostProcessBaseline();
        RestoreCameraControls();
    }

    float CombatFatalHalfCutController::GetCutTotalDurationSec() const
    {
        return std::max(0.0f, Get_m_attackSec())
            + std::max(0.0f, Get_m_holdSec())
            + std::max(0.0f, Get_m_releaseSec());
    }

    float CombatFatalHalfCutController::EvaluateSplitAmount(float timeSec) const
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

    float CombatFatalHalfCutController::EvaluateFlash01(float timeSec) const
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

    float CombatFatalHalfCutController::EvaluateCutWeight01(float splitAmount, float flash01) const
    {
        const float peakAbs = std::max(std::abs(Get_m_peakAmount()), 1e-6f);
        const float split01 = Saturate(std::abs(splitAmount) / peakAbs);
        return std::max(split01, Saturate(flash01));
    }

    void CombatFatalHalfCutController::ApplyCutPostProcess(float splitAmount, float flash01, float timeSec, float cutWeight01)
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
        s.bOverride_SplitFxColorA = true;
        s.bOverride_SplitFxColorB = true;

        s.splitAmount = splitAmount;
        s.splitAngleDeg = Get_m_angleDeg();
        s.splitLineOffset = Get_m_lineOffset();
        s.splitFeather = std::max(0.0001f, Get_m_feather());
        s.splitFxWidth = std::max(0.0001f, Get_m_splitFxWidth());
        s.splitFxSpeed = std::max(0.0f, Get_m_splitFxSpeed());
        s.splitFxTimeSec = std::max(0.0f, timeSec) * std::max(0.0f, Get_m_splitFxTimeScale());
        s.splitFxColorA = Get_m_splitFxColorA();
        s.splitFxColorB = Get_m_splitFxColorB();
        s.splitFxIntensity = Get_m_splitFxEnabled()
            ? std::max(0.0f, Get_m_splitFxIntensity()) * cutWeight01
            : 0.0f;

        const float baseExposure = m_ppvBaseline.valid ? m_ppvBaseline.exposure : 0.0f;
        const float baseBloomThreshold = m_ppvBaseline.valid ? m_ppvBaseline.bloomThreshold : 1.0f;
        const float baseBloomKnee = m_ppvBaseline.valid ? m_ppvBaseline.bloomKnee : 0.5f;
        const float baseBloomIntensity = m_ppvBaseline.valid ? m_ppvBaseline.bloomIntensity : 0.0f;
        const float baseBloomGaussianIntensity = m_ppvBaseline.valid ? m_ppvBaseline.bloomGaussianIntensity : 1.0f;
        const float baseBloomRadius = m_ppvBaseline.valid ? m_ppvBaseline.bloomRadius : 1.0f;

        if (Get_m_enableFlashTone())
        {
            s.bOverride_Exposure = true;
            s.bOverride_BloomThreshold = true;
            s.bOverride_BloomKnee = true;
            s.bOverride_BloomIntensity = true;
            s.bOverride_BloomGaussianIntensity = true;
            s.bOverride_BloomRadius = true;

            s.exposure = LerpUnclamped(baseExposure, Get_m_peakExposure(), flash01);
            s.bloomThreshold = LerpUnclamped(baseBloomThreshold, Get_m_peakBloomThreshold(), flash01);
            s.bloomKnee = LerpUnclamped(baseBloomKnee, Get_m_peakBloomKnee(), flash01);
            s.bloomIntensity = LerpUnclamped(baseBloomIntensity, Get_m_peakBloomIntensity(), flash01);
            s.bloomGaussianIntensity = LerpUnclamped(baseBloomGaussianIntensity, Get_m_peakBloomGaussianIntensity(), flash01);
            s.bloomRadius = LerpUnclamped(baseBloomRadius, Get_m_peakBloomRadius(), flash01);
        }
        else if (m_ppvBaseline.valid)
        {
            s.bOverride_Exposure = m_ppvBaseline.bOverrideExposure;
            s.bOverride_BloomThreshold = m_ppvBaseline.bOverrideBloomThreshold;
            s.bOverride_BloomKnee = m_ppvBaseline.bOverrideBloomKnee;
            s.bOverride_BloomIntensity = m_ppvBaseline.bOverrideBloomIntensity;
            s.bOverride_BloomGaussianIntensity = m_ppvBaseline.bOverrideBloomGaussianIntensity;
            s.bOverride_BloomRadius = m_ppvBaseline.bOverrideBloomRadius;

            s.exposure = baseExposure;
            s.bloomThreshold = baseBloomThreshold;
            s.bloomKnee = baseBloomKnee;
            s.bloomIntensity = baseBloomIntensity;
            s.bloomGaussianIntensity = baseBloomGaussianIntensity;
            s.bloomRadius = baseBloomRadius;
        }
        else
        {
            s.bOverride_Exposure = false;
            s.bOverride_BloomThreshold = false;
            s.bOverride_BloomKnee = false;
            s.bOverride_BloomIntensity = false;
            s.bOverride_BloomGaussianIntensity = false;
            s.bOverride_BloomRadius = false;
        }

        if (Get_m_useImpactBlurDuringCut())
        {
            s.bOverride_ImpactBlurIntensity = true;
            s.bOverride_ImpactBlurRadius = true;
            s.bOverride_ImpactBlurCenterX = true;
            s.bOverride_ImpactBlurCenterY = true;
            s.impactBlurIntensity = std::max(0.0f, Get_m_peakImpactBlurIntensity()) * cutWeight01;
            s.impactBlurRadius = std::max(0.001f, Get_m_impactBlurRadius());
            s.impactBlurCenterX = Saturate(Get_m_impactBlurCenterX());
            s.impactBlurCenterY = Saturate(Get_m_impactBlurCenterY());
        }
    }
}
