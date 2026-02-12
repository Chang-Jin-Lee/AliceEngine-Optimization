#include "CombatDeathFpsProduction.h"

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CombatDeathFpsProduction);

    namespace
    {
        float Saturate(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float SmoothStep01(float t)
        {
            t = Saturate(t);
            return t * t * (3.0f - 2.0f * t);
        }

        float ExpSmooth(float damping, float dt)
        {
            if (damping <= 0.0f)
                return 1.0f;
            return 1.0f - std::exp(-damping * std::max(0.0f, dt));
        }

        DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
        {
            const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
            DirectX::XMFLOAT4 out{};
            DirectX::XMStoreFloat4(&out, q);
            return out;
        }

        DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat)
        {
            using namespace DirectX;

            const XMVECTOR q = XMLoadFloat4(&quat);
            const XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
            const XMVECTOR u = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);

            XMFLOAT3 f3{};
            XMStoreFloat3(&f3, f);

            const float yaw = std::atan2(f3.x, f3.z);
            const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));

            XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
            XMVECTOR r0 = XMVector3Cross(worldUp, f);
            if (XMVectorGetX(XMVector3LengthSq(r0)) < 1e-6f)
                r0 = XMVector3Cross(XMVectorSet(1, 0, 0, 0), f);

            r0 = XMVector3Normalize(r0);
            const XMVECTOR u0 = XMVector3Cross(f, r0);
            const float roll = std::atan2(
                XMVectorGetX(XMVector3Dot(r0, u)),
                XMVectorGetX(XMVector3Dot(u0, u))
            );

            return { pitch, yaw, roll };
        }

        DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir)
        {
            const float yaw = std::atan2(dir.x, dir.z);
            const float distXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            const float pitch = -std::atan2(dir.y, distXZ);
            return DirectX::XMFLOAT3(pitch, yaw, 0.0f);
        }

        bool FetchWorldTransform(World* world, EntityId id,
                                 DirectX::XMFLOAT3& outPos,
                                 DirectX::XMFLOAT3& outRot,
                                 DirectX::XMFLOAT3& outScale)
        {
            if (!world || id == InvalidEntityId)
                return false;

            auto* tr = world->GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled)
                return false;

            if (tr->parent == InvalidEntityId)
            {
                outPos = tr->position;
                outRot = tr->rotation;
                outScale = tr->scale;
                return true;
            }

            DirectX::XMMATRIX worldM = world->ComputeWorldMatrix(id);
            DirectX::XMVECTOR s, q, t;
            if (DirectX::XMMatrixDecompose(&s, &q, &t, worldM))
            {
                DirectX::XMStoreFloat3(&outPos, t);
                DirectX::XMStoreFloat3(&outScale, s);
                DirectX::XMFLOAT4 quat{};
                DirectX::XMStoreFloat4(&quat, q);
                outRot = QuaternionToEuler(quat);
                return true;
            }

            outPos = tr->position;
            outRot = tr->rotation;
            outScale = tr->scale;
            return true;
        }

        EntityId FindDescendantByName(World& world, EntityId rootId, const std::string& targetName)
        {
            if (rootId == InvalidEntityId || targetName.empty())
                return InvalidEntityId;

            std::vector<EntityId> stack = world.GetChildren(rootId);
            while (!stack.empty())
            {
                const EntityId current = stack.back();
                stack.pop_back();

                if (world.GetEntityName(current) == targetName)
                    return current;

                const auto children = world.GetChildren(current);
                for (const EntityId child : children)
                    stack.push_back(child);
            }

            return InvalidEntityId;
        }
    }

    void CombatDeathFpsProduction::Start()
    {
        ResolveOutputCamera();
        ResolveFpsCamera();
        ResolveActorEntities();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();
        m_prevPlayerDead = IsPlayerDead();
    }

    void CombatDeathFpsProduction::OnDisable()
    {
        RestoreIfNeeded();
    }

    void CombatDeathFpsProduction::OnDestroy()
    {
        RestoreIfNeeded();
    }

    bool CombatDeathFpsProduction::IsUiBlockingActive() const
    {
        if (m_running)
            return true;

        // Keep UI blocked during the immediate post-cut fall blend to avoid overlap.
        if (m_cutDone && Get_m_enableFallBlend())
        {
            const float blockSec = std::max(0.0f, Get_m_fallBlendDurationSec());
            if (m_afterCutElapsedSec < blockSec)
                return true;
        }

        return false;
    }

    void CombatDeathFpsProduction::Update(float deltaTime)
    {
        const float dt = std::max(0.0f, deltaTime);

        ResolveOutputCamera();
        ResolveFpsCamera();
        ResolveActorEntities();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();

        const bool playerDead = IsPlayerDead();
        if (Get_m_autoStartOnPlayerDeath() && playerDead && !m_prevPlayerDead)
        {
            if (!m_running || Get_m_restartOnKey())
                StartSequence();
        }
        m_prevPlayerDead = playerDead;

        if (Get_m_enableHotkey() && Get_m_triggerWithAlpha7())
        {
            if (auto* input = Input())
            {
                if (input->GetKeyDown(KeyCode::Alpha7))
                {
                    if (!m_running || Get_m_restartOnKey())
                        StartSequence();
                }
            }
        }

        if (m_running)
            ApplyTimeline(dt);

        if (m_cutDone && Get_m_holdFpsAfterCut())
            UpdateAfterCut(dt);
    }

    bool CombatDeathFpsProduction::ResolveOutputCamera()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        if (m_outputCameraEntity != InvalidEntityId)
        {
            if (world->GetComponent<CameraComponent>(m_outputCameraEntity))
                return true;
            m_outputCameraEntity = InvalidEntityId;
        }

        const std::string& name = Get_m_outputCameraName();
        if (name.empty())
            return false;

        const GameObject go = world->FindGameObject(name);
        if (!go.IsValid())
            return false;
        if (!world->GetComponent<CameraComponent>(go.id()))
            return false;

        m_outputCameraEntity = go.id();
        return true;
    }

    bool CombatDeathFpsProduction::ResolveFpsCamera()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        if (m_fpsCameraEntity != InvalidEntityId)
        {
            if (world->GetComponent<CameraComponent>(m_fpsCameraEntity))
                return true;
            m_fpsCameraEntity = InvalidEntityId;
        }

        const std::string& name = Get_m_fpsCameraName();
        if (!name.empty())
        {
            const GameObject go = world->FindGameObject(name);
            if (go.IsValid() && world->GetComponent<CameraComponent>(go.id()))
            {
                m_fpsCameraEntity = go.id();
                return true;
            }
        }

        if (!Get_m_autoCreateFallbackFpsCamera())
            return false;

        ResolveActorEntities();
        if (m_playerEntity == InvalidEntityId)
            return false;

        const EntityId id = world->CreateEntity();
        m_fpsCameraEntity = id;
        world->SetEntityName(id, name.empty() ? "FPSCamera_Auto" : name);

        auto& tr = world->AddComponent<TransformComponent>(id);
        tr.enabled = true;
        tr.visible = true;
        tr.position = Get_m_fallbackFpsLocalOffset();
        tr.rotation = Get_m_fallbackFpsLocalRotation();
        tr.scale = Get_m_fallbackFpsLocalScale();

        auto& cam = world->AddComponent<CameraComponent>(id);
        cam.SetFov(std::clamp(Get_m_fallbackFpsFovDeg(), 1.0f, 170.0f));

        if (ResolveOutputCamera())
        {
            if (const auto* outCam = world->GetComponent<CameraComponent>(m_outputCameraEntity))
            {
                cam.SetFov(outCam->GetFov());
                cam.SetNear(outCam->GetNear());
                cam.SetFar(outCam->GetFar());
            }
        }

        world->SetParent(id, m_playerEntity, false);
        world->MarkTransformDirty(id);
        return true;
    }

    bool CombatDeathFpsProduction::ResolveActorEntities()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        m_playerEntity = InvalidEntityId;
        m_playerCoreEntity = InvalidEntityId;
        m_playerWeaponEntity = InvalidEntityId;
        m_bossEntity = InvalidEntityId;
        m_bossHeadEntity = InvalidEntityId;

        if (!Get_m_playerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerEntityName());
            if (go.IsValid())
                m_playerEntity = go.id();
        }

        if (!Get_m_playerCoreEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerCoreEntityName());
            if (go.IsValid())
                m_playerCoreEntity = go.id();
            else if (m_playerEntity != InvalidEntityId)
                m_playerCoreEntity = FindDescendantByName(*world, m_playerEntity, Get_m_playerCoreEntityName());
        }

        if (!Get_m_playerWeaponEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerWeaponEntityName());
            if (go.IsValid())
                m_playerWeaponEntity = go.id();
            else if (m_playerEntity != InvalidEntityId)
                m_playerWeaponEntity = FindDescendantByName(*world, m_playerEntity, Get_m_playerWeaponEntityName());
        }

        if (!Get_m_bossEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossEntityName());
            if (go.IsValid())
                m_bossEntity = go.id();
        }

        if (!Get_m_bossHeadEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossHeadEntityName());
            if (go.IsValid())
                m_bossHeadEntity = go.id();
            else if (m_bossEntity != InvalidEntityId)
                m_bossHeadEntity = FindDescendantByName(*world, m_bossEntity, Get_m_bossHeadEntityName());
        }

        return true;
    }

    bool CombatDeathFpsProduction::IsPlayerDead()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        bool dead = false;

        if (auto* scripts = world->GetScripts(GetOwnerId()))
        {
            for (auto& sc : *scripts)
            {
                if (sc.scriptName != "C_CombatSessionComponent" || !sc.instance)
                    continue;

                if (auto* session = dynamic_cast<C_CombatSessionComponent*>(sc.instance.get()))
                {
                    dead = dead || (session->GetPlayerState() == Combat::ActionState::Dead);
                    break;
                }
            }
        }

        if (m_playerEntity != InvalidEntityId)
        {
            const auto* health = world->GetComponent<HealthComponent>(m_playerEntity);
            if (health)
                dead = dead || (!health->alive || health->currentHealth <= 0.0f);
        }

        return dead;
    }

    bool CombatDeathFpsProduction::EnsurePostProcessVolume()
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
            world->SetEntityName(id, Get_m_postProcessVolumeName().empty() ? "DeathFpsImpactVolume" : Get_m_postProcessVolumeName());
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

        ppv->unbound = true;
        ppv->priority = Get_m_postProcessPriority();
        ppv->blendWeight = 1.0f;
        return true;
    }

    void CombatDeathFpsProduction::CapturePostProcessBaseline()
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

    void CombatDeathFpsProduction::RestorePostProcessBaseline()
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

    void CombatDeathFpsProduction::StartSequence()
    {
        if (!ResolveOutputCamera() || !ResolveFpsCamera())
            return;

        ResolveActorEntities();

        if (Get_m_restoreVisibilityOnStop())
            RestoreSavedVisibility();
        RestoreOverriddenScripts();

        SetGameplayCameraControl(false);
        DisableBlockingScriptsForSequence();

        if (Get_m_hidePlayerImmediatelyOnDeath())
        {
            if (Get_m_hidePlayerOnCut() && m_playerEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerEntity);
                SetVisibilityRecursive(m_playerEntity, false);
            }

            if (Get_m_hidePlayerCoreOnCut() && m_playerCoreEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerCoreEntity);
                SetVisibilityRecursive(m_playerCoreEntity, false);
            }

            if (Get_m_hidePlayerWeaponOnCut() && m_playerWeaponEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerWeaponEntity);
                SetVisibilityRecursive(m_playerWeaponEntity, false);
            }
        }

        m_running = true;
        m_cutDone = false;
        m_elapsedSec = 0.0f;
        m_holdFpsElapsedSec = 0.0f;
        m_afterCutElapsedSec = 0.0f;
        m_bossChargeTriggered = false;
        m_fallPoseCaptured = false;

        ApplyBlur(0.0f);
    }

    void CombatDeathFpsProduction::StopSequence(bool restoreCameraControl)
    {
        m_running = false;
        if (!Get_m_holdFpsAfterCut())
        {
            m_cutDone = false;
            if (Get_m_restoreVisibilityOnStop())
                RestoreSavedVisibility();
            RestoreOverriddenScripts();
        }
        ApplyBlur(0.0f);

        if (restoreCameraControl)
            SetGameplayCameraControl(true);
    }

    void CombatDeathFpsProduction::ApplyTimeline(float deltaTime)
    {
        m_elapsedSec += std::max(0.0f, deltaTime);

        const float blurIn = std::max(0.0f, Get_m_blurInSec());
        const float blurHold = std::max(0.0f, Get_m_blurHoldSec());
        const float blurOut = std::max(0.0f, Get_m_blurOutSec());
        const float blurTotal = blurIn + blurHold + blurOut;
        const float peak = std::max(0.0f, Get_m_blurPeakIntensity());

        float blurIntensity = 0.0f;
        if (m_elapsedSec < blurIn)
        {
            const float t = (blurIn > 1e-6f) ? (m_elapsedSec / blurIn) : 1.0f;
            blurIntensity = peak * Saturate(t);
        }
        else if (m_elapsedSec < (blurIn + blurHold))
        {
            blurIntensity = peak;
        }
        else if (m_elapsedSec < blurTotal)
        {
            const float outT = (blurOut > 1e-6f) ? ((m_elapsedSec - blurIn - blurHold) / blurOut) : 1.0f;
            blurIntensity = peak * (1.0f - Saturate(outT));
        }
        else
        {
            blurIntensity = 0.0f;
            StopSequence(Get_m_restoreGameplayCameraWhenFinished() && !Get_m_holdFpsAfterCut());
        }

        ApplyBlur(blurIntensity);

        if (!m_cutDone && m_elapsedSec >= std::max(0.0f, Get_m_cutAtSec()))
        {
            CutToFpsCamera();
            CaptureFallPose();
            m_cutDone = true;
            m_holdFpsElapsedSec = 0.0f;
            m_afterCutElapsedSec = 0.0f;

            if (Get_m_hidePlayerOnCut() && m_playerEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerEntity);
                SetVisibilityRecursive(m_playerEntity, false);
            }

            if (Get_m_hidePlayerWeaponOnCut() && m_playerWeaponEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerWeaponEntity);
                SetVisibilityRecursive(m_playerWeaponEntity, false);
            }

            if (Get_m_hidePlayerCoreOnCut() && m_playerCoreEntity != InvalidEntityId)
            {
                SaveVisibilityRecursive(m_playerCoreEntity);
                SetVisibilityRecursive(m_playerCoreEntity, false);
            }

            if (!m_bossChargeTriggered
                && Get_m_triggerBossCharge()
                && GetEffectiveBossChargeDelaySec() <= 1e-4f)
            {
                TriggerBossCharge();
            }
        }
    }

    void CombatDeathFpsProduction::UpdateAfterCut(float deltaTime)
    {
        m_holdFpsElapsedSec += std::max(0.0f, deltaTime);
        m_afterCutElapsedSec += std::max(0.0f, deltaTime);

        if (!m_bossChargeTriggered
            && Get_m_triggerBossCharge()
            && m_afterCutElapsedSec >= GetEffectiveBossChargeDelaySec())
        {
            TriggerBossCharge();
        }

        if (Get_m_enableFallBlend())
            ApplyFallBlend(deltaTime);
        else
            FollowFpsCamera();

        if (Get_m_lockLookToBossDuringFall())
            ApplyLookAtBoss(deltaTime);

        const float holdDuration = std::max(0.0f, Get_m_holdFpsDurationSec());
        if (holdDuration > 0.0f && m_holdFpsElapsedSec >= holdDuration)
        {
            m_cutDone = false;
            if (Get_m_restoreVisibilityOnStop())
                RestoreSavedVisibility();
            RestoreOverriddenScripts();
            if (Get_m_restoreGameplayCameraWhenFinished())
                SetGameplayCameraControl(true);
        }
    }

    void CombatDeathFpsProduction::ApplyBlur(float intensity)
    {
        World* world = GetWorld();
        if (!world || m_volumeEntity == InvalidEntityId)
            return;

        auto* ppv = world->GetComponent<PostProcessVolumeComponent>(m_volumeEntity);
        if (!ppv)
            return;

        auto& s = ppv->settings;
        s.bOverride_ImpactBlurIntensity = true;
        s.bOverride_ImpactBlurRadius = true;
        s.bOverride_ImpactBlurCenterX = true;
        s.bOverride_ImpactBlurCenterY = true;

        s.impactBlurIntensity = std::max(0.0f, intensity);
        s.impactBlurRadius = std::max(0.0f, Get_m_blurRadius());
        s.impactBlurCenterX = Saturate(Get_m_blurCenterX());
        s.impactBlurCenterY = Saturate(Get_m_blurCenterY());
    }

    void CombatDeathFpsProduction::CutToFpsCamera()
    {
        World* world = GetWorld();
        if (!world)
            return;
        if (!ResolveOutputCamera() || !ResolveFpsCamera())
            return;

        auto* outTr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        if (!outTr)
            return;

        DirectX::XMFLOAT3 fpsWorldPos{};
        DirectX::XMFLOAT3 fpsWorldRot{};
        DirectX::XMFLOAT3 fpsWorldScale{};
        if (!FetchWorldTransform(world, m_fpsCameraEntity, fpsWorldPos, fpsWorldRot, fpsWorldScale))
            return;

        if (auto* blend = world->GetComponent<CameraBlendComponent>(m_outputCameraEntity))
        {
            blend->active = false;
            blend->needsSnapshot = false;
            blend->targetId = InvalidEntityId;
            blend->targetName.clear();
        }

        outTr->position = fpsWorldPos;
        outTr->rotation = fpsWorldRot;
        outTr->scale = fpsWorldScale;
        world->MarkTransformDirty(m_outputCameraEntity);

        auto* outCam = world->GetComponent<CameraComponent>(m_outputCameraEntity);
        auto* fpsCam = world->GetComponent<CameraComponent>(m_fpsCameraEntity);
        if (outCam && fpsCam)
        {
            outCam->SetFov(fpsCam->GetFov());
            outCam->SetNear(fpsCam->GetNear());
            outCam->SetFar(fpsCam->GetFar());
        }
    }

    void CombatDeathFpsProduction::FollowFpsCamera()
    {
        World* world = GetWorld();
        if (!world)
            return;
        if (!ResolveOutputCamera() || !ResolveFpsCamera())
            return;

        auto* outTr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        if (!outTr)
            return;

        DirectX::XMFLOAT3 fpsWorldPos{};
        DirectX::XMFLOAT3 fpsWorldRot{};
        DirectX::XMFLOAT3 fpsWorldScale{};
        if (!FetchWorldTransform(world, m_fpsCameraEntity, fpsWorldPos, fpsWorldRot, fpsWorldScale))
            return;

        outTr->position = fpsWorldPos;
        outTr->rotation = fpsWorldRot;
        outTr->scale = fpsWorldScale;
        world->MarkTransformDirty(m_outputCameraEntity);
    }

    void CombatDeathFpsProduction::CaptureFallPose()
    {
        World* world = GetWorld();
        if (!world || m_outputCameraEntity == InvalidEntityId)
            return;

        auto* outTr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        if (!outTr)
            return;

        m_fallStartPose.position = outTr->position;
        m_fallStartPose.rotation = outTr->rotation;
        m_fallStartPose.scale = outTr->scale;

        m_fallEndPose = m_fallStartPose;
        if (Get_m_approachBossDuringFall())
        {
            const EntityId lookTarget = (m_bossHeadEntity != InvalidEntityId) ? m_bossHeadEntity : m_bossEntity;
            if (lookTarget != InvalidEntityId)
            {
                DirectX::XMFLOAT3 targetPos{};
                DirectX::XMFLOAT3 targetRot{};
                DirectX::XMFLOAT3 targetScale{};
                if (FetchWorldTransform(world, lookTarget, targetPos, targetRot, targetScale))
                {
                    targetPos.y += Get_m_lookAtBossHeightOffset();

                    DirectX::XMFLOAT3 fromTarget{
                        m_fallStartPose.position.x - targetPos.x,
                        m_fallStartPose.position.y - targetPos.y,
                        m_fallStartPose.position.z - targetPos.z
                    };

                    const float lenSq = fromTarget.x * fromTarget.x
                                      + fromTarget.y * fromTarget.y
                                      + fromTarget.z * fromTarget.z;
                    if (lenSq > 1e-6f)
                    {
                        const float len = std::sqrt(lenSq);
                        const float desiredDist = std::max(0.1f, Get_m_targetBossDistanceAfterFall());
                        if (len > desiredDist + 1e-3f)
                        {
                            const float invLen = 1.0f / len;
                            m_fallEndPose.position.x = targetPos.x + fromTarget.x * invLen * desiredDist;
                            m_fallEndPose.position.y = targetPos.y + fromTarget.y * invLen * desiredDist;
                            m_fallEndPose.position.z = targetPos.z + fromTarget.z * invLen * desiredDist;
                        }
                    }
                }
            }
        }

        const auto& offset = Get_m_fallOffsetWorld();
        m_fallEndPose.position.x += offset.x;
        m_fallEndPose.position.y += offset.y;
        m_fallEndPose.position.z += offset.z;
        m_fallPoseCaptured = true;
    }

    void CombatDeathFpsProduction::ApplyFallBlend(float deltaTime)
    {
        World* world = GetWorld();
        if (!world || !m_fallPoseCaptured || m_outputCameraEntity == InvalidEntityId)
            return;

        auto* outTr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        if (!outTr)
            return;

        const float duration = std::max(0.001f, Get_m_fallBlendDurationSec());
        const float t = SmoothStep01(m_afterCutElapsedSec / duration);
        outTr->position = LerpVec(m_fallStartPose.position, m_fallEndPose.position, t);
        world->MarkTransformDirty(m_outputCameraEntity);
        (void)deltaTime;
    }

    void CombatDeathFpsProduction::ApplyLookAtBoss(float deltaTime)
    {
        World* world = GetWorld();
        if (!world || m_outputCameraEntity == InvalidEntityId)
            return;

        auto* outTr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        if (!outTr)
            return;

        const EntityId lookTarget = (m_bossHeadEntity != InvalidEntityId) ? m_bossHeadEntity : m_bossEntity;
        if (lookTarget == InvalidEntityId)
            return;

        DirectX::XMFLOAT3 targetPos{};
        DirectX::XMFLOAT3 targetRot{};
        DirectX::XMFLOAT3 targetScale{};
        if (!FetchWorldTransform(world, lookTarget, targetPos, targetRot, targetScale))
            return;

        targetPos.y += Get_m_lookAtBossHeightOffset();

        DirectX::XMFLOAT3 toTarget{
            targetPos.x - outTr->position.x,
            targetPos.y - outTr->position.y,
            targetPos.z - outTr->position.z
        };

        const float lenSq = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;
        if (lenSq < 1e-6f)
            return;

        DirectX::XMFLOAT3 desired = DirectionToEuler(toTarget);
        desired.x += DirectX::XMConvertToRadians(Get_m_lookPitchOffsetDeg());

        const float rotAlpha = ExpSmooth(std::max(0.0f, Get_m_lookRotationDamping()), deltaTime);
        const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(outTr->rotation);
        const DirectX::XMFLOAT4 qTo = EulerToQuaternion(desired);
        DirectX::XMFLOAT4 qOut{};
        DirectX::XMStoreFloat4(
            &qOut,
            DirectX::XMQuaternionSlerp(
                DirectX::XMLoadFloat4(&qFrom),
                DirectX::XMLoadFloat4(&qTo),
                Saturate(rotAlpha)));

        outTr->rotation = QuaternionToEuler(qOut);
        world->MarkTransformDirty(m_outputCameraEntity);
    }

    void CombatDeathFpsProduction::TriggerBossCharge()
    {
        if (m_bossChargeTriggered)
            return;

        ResolveActorEntities();

        World* world = GetWorld();
        if (!world || m_bossEntity == InvalidEntityId)
            return;

        auto* bossTr = world->GetComponent<TransformComponent>(m_bossEntity);
        if (bossTr && m_playerEntity != InvalidEntityId)
        {
            DirectX::XMFLOAT3 bossWorldPos{};
            DirectX::XMFLOAT3 bossWorldRot{};
            DirectX::XMFLOAT3 bossWorldScale{};
            DirectX::XMFLOAT3 playerWorldPos{};
            DirectX::XMFLOAT3 playerWorldRot{};
            DirectX::XMFLOAT3 playerWorldScale{};

            const bool hasBossWorld = FetchWorldTransform(world, m_bossEntity, bossWorldPos, bossWorldRot, bossWorldScale);
            const bool hasPlayerWorld = FetchWorldTransform(world, m_playerEntity, playerWorldPos, playerWorldRot, playerWorldScale);

            const float dx = (hasPlayerWorld ? playerWorldPos.x : 0.0f) - (hasBossWorld ? bossWorldPos.x : 0.0f);
            const float dz = (hasPlayerWorld ? playerWorldPos.z : 0.0f) - (hasBossWorld ? bossWorldPos.z : 0.0f);
            if ((dx * dx + dz * dz) > 1e-6f)
            {
                float yawWorld = std::atan2(dx, dz);
                yawWorld += DirectX::XMConvertToRadians(Get_m_bossFacingYawOffsetDeg());

                float localYaw = yawWorld;
                if (bossTr->parent != InvalidEntityId)
                {
                    DirectX::XMFLOAT3 parentWorldPos{};
                    DirectX::XMFLOAT3 parentWorldRot{};
                    DirectX::XMFLOAT3 parentWorldScale{};
                    if (FetchWorldTransform(world, bossTr->parent, parentWorldPos, parentWorldRot, parentWorldScale))
                        localYaw -= parentWorldRot.y;
                }

                bossTr->rotation.y = localYaw;
                world->MarkTransformDirty(m_bossEntity);
            }
        }

        if (auto* adv = world->GetComponent<AdvancedAnimationComponent>(m_bossEntity))
        {
            const std::string clip = Get_m_bossChargeClipName();
            if (!clip.empty())
            {
                adv->enabled = true;
                adv->playing = true;
                adv->base.enabled = true;
                adv->base.autoAdvance = true;
                adv->base.clipA = clip;
                adv->base.clipB = clip;
                adv->base.timeA = 0.0f;
                adv->base.timeB = 0.0f;
                adv->base.speedA = std::max(0.01f, Get_m_bossChargeSpeed());
                adv->base.speedB = std::max(0.01f, Get_m_bossChargeSpeed());
                adv->base.loopA = false;
                adv->base.loopB = false;
                adv->base.blend01 = 0.0f;
                adv->upper.enabled = false;
                adv->additive.enabled = false;
            }
        }

        if (auto* skinAnim = world->GetComponent<SkinnedAnimationComponent>(m_bossEntity))
        {
            skinAnim->playing = true;
            skinAnim->speed = std::max(0.01f, Get_m_bossChargeSpeed());
            skinAnim->timeSec = 0.0f;
        }

        m_bossChargeTriggered = true;
    }

    float CombatDeathFpsProduction::GetEffectiveBossChargeDelaySec() const
    {
        float delay = std::max(0.0f, Get_m_triggerBossChargeAfterCutSec());
        if (delay > 1e-4f)
            return delay;

        if (Get_m_approachBossDuringFall() && Get_m_enableFallBlend())
            return std::max(0.0f, Get_m_fallBlendDurationSec());

        return delay;
    }

    void CombatDeathFpsProduction::SetGameplayCameraControl(bool enable)
    {
        World* world = GetWorld();
        if (!world)
            return;
        if (!ResolveOutputCamera())
            return;

        auto* tr = world->GetComponent<TransformComponent>(m_outputCameraEntity);
        auto* follow = world->GetComponent<CameraFollowComponent>(m_outputCameraEntity);
        auto* lookAt = world->GetComponent<CameraLookAtComponent>(m_outputCameraEntity);
        auto* springArm = world->GetComponent<CameraSpringArmComponent>(m_outputCameraEntity);
        auto* blend = world->GetComponent<CameraBlendComponent>(m_outputCameraEntity);

        if (!enable)
        {
            if (m_controlsOverridden)
                return;

            m_hasFollow = (follow != nullptr);
            m_hasLookAt = (lookAt != nullptr);
            m_hasSpringArm = (springArm != nullptr);

            if (m_hasFollow)
                m_savedFollowEnabled = follow->enabled;
            if (m_hasLookAt)
                m_savedLookAtEnabled = lookAt->enabled;
            if (m_hasSpringArm)
                m_savedSpringArmEnabled = springArm->enabled;

            if (m_hasFollow && Get_m_disableFollowDuringSequence())
                follow->enabled = false;
            if (m_hasLookAt && Get_m_disableLookAtDuringSequence())
                lookAt->enabled = false;
            if (m_hasSpringArm && Get_m_disableSpringArmDuringSequence())
                springArm->enabled = false;

            if (blend)
            {
                blend->active = false;
                blend->needsSnapshot = false;
            }

            m_controlsOverridden = true;
            return;
        }

        if (!m_controlsOverridden)
            return;

        if (m_hasFollow && follow)
        {
            follow->enabled = m_savedFollowEnabled;
            if (tr)
            {
                follow->initialized = false;
                follow->smoothedPosition = tr->position;
                follow->smoothedRotation = tr->rotation;
                follow->yawDeg = DirectX::XMConvertToDegrees(tr->rotation.y);
                follow->pitchDeg = DirectX::XMConvertToDegrees(tr->rotation.x);
            }
        }

        if (m_hasLookAt && lookAt)
            lookAt->enabled = m_savedLookAtEnabled;
        if (m_hasSpringArm && springArm)
            springArm->enabled = m_savedSpringArmEnabled;

        m_controlsOverridden = false;
    }

    void CombatDeathFpsProduction::DisableBlockingScriptsForSequence()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto disableScript = [&](EntityId entity, const std::string& scriptName)
        {
            if (entity == InvalidEntityId || scriptName.empty())
                return;

            auto* scripts = world->GetScripts(entity);
            if (!scripts)
                return;

            for (auto& scriptComp : *scripts)
            {
                if (scriptComp.scriptName != scriptName)
                    continue;

                const auto alreadySaved = std::find_if(
                    m_scriptOverrides.begin(),
                    m_scriptOverrides.end(),
                    [&](const ScriptState& s)
                    {
                        return s.entity == entity && s.scriptName == scriptComp.scriptName;
                    });

                if (alreadySaved == m_scriptOverrides.end())
                {
                    ScriptState state{};
                    state.entity = entity;
                    state.scriptName = scriptComp.scriptName;
                    state.enabled = scriptComp.enabled;
                    m_scriptOverrides.push_back(state);
                }

                scriptComp.enabled = false;
            }
        };

        if (Get_m_disableCombatSessionDuringSequence())
            disableScript(GetOwnerId(), Get_m_combatSessionScriptName());

        if (Get_m_disableBossCombatSessionDuringSequence())
            disableScript(GetOwnerId(), Get_m_bossCombatSessionScriptName());

        if (Get_m_disableBossBrainDuringSequence())
            disableScript(m_bossEntity, Get_m_bossBrainScriptName());
    }

    void CombatDeathFpsProduction::RestoreOverriddenScripts()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_scriptOverrides.clear();
            return;
        }

        for (const ScriptState& state : m_scriptOverrides)
        {
            auto* scripts = world->GetScripts(state.entity);
            if (!scripts)
                continue;

            for (auto& scriptComp : *scripts)
            {
                if (scriptComp.scriptName == state.scriptName)
                {
                    scriptComp.enabled = state.enabled;
                    break;
                }
            }
        }

        m_scriptOverrides.clear();
    }

    void CombatDeathFpsProduction::SaveVisibilityRecursive(EntityId rootId)
    {
        World* world = GetWorld();
        if (!world || rootId == InvalidEntityId)
            return;

        std::vector<EntityId> stack;
        stack.push_back(rootId);

        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (auto* tr = world->GetComponent<TransformComponent>(current))
            {
                const auto alreadySaved = std::find_if(
                    m_savedVisibility.begin(),
                    m_savedVisibility.end(),
                    [&](const VisibilityState& s)
                    {
                        return s.entity == current;
                    });

                if (alreadySaved == m_savedVisibility.end())
                {
                    VisibilityState state{};
                    state.entity = current;
                    state.visible = tr->visible;
                    m_savedVisibility.push_back(state);
                }
            }

            const auto children = world->GetChildren(current);
            for (const EntityId child : children)
                stack.push_back(child);
        }
    }

    void CombatDeathFpsProduction::SetVisibilityRecursive(EntityId rootId, bool visible)
    {
        World* world = GetWorld();
        if (!world || rootId == InvalidEntityId)
            return;

        std::vector<EntityId> stack;
        stack.push_back(rootId);

        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (auto* tr = world->GetComponent<TransformComponent>(current))
            {
                tr->visible = visible;
                world->MarkTransformDirty(current);
            }

            const auto children = world->GetChildren(current);
            for (const EntityId child : children)
                stack.push_back(child);
        }
    }

    void CombatDeathFpsProduction::RestoreSavedVisibility()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_savedVisibility.clear();
            return;
        }

        for (const VisibilityState& state : m_savedVisibility)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->visible = state.visible;
                world->MarkTransformDirty(state.entity);
            }
        }

        m_savedVisibility.clear();
    }

    void CombatDeathFpsProduction::RestoreIfNeeded()
    {
        m_running = false;
        m_cutDone = false;
        m_elapsedSec = 0.0f;
        m_holdFpsElapsedSec = 0.0f;
        m_afterCutElapsedSec = 0.0f;
        m_bossChargeTriggered = false;
        m_fallPoseCaptured = false;

        ApplyBlur(0.0f);
        RestorePostProcessBaseline();
        if (Get_m_restoreVisibilityOnStop())
            RestoreSavedVisibility();
        RestoreOverriddenScripts();
        SetGameplayCameraControl(true);
    }
}
