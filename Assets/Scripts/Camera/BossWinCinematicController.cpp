#include "BossWinCinematicController.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(BossWinCinematicController);

    namespace
    {
        float Saturate(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float SmoothStep01(float t)
        {
            t = Saturate(t);
            return t * t * (3.0f - 2.0f * t);
        }

        DirectX::XMFLOAT3 AddVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        DirectX::XMFLOAT3 SubVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        DirectX::XMFLOAT3 MulVec(const DirectX::XMFLOAT3& v, float s)
        {
            return { v.x * s, v.y * s, v.z * s };
        }

        DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return {
                Lerp(a.x, b.x, t),
                Lerp(a.y, b.y, t),
                Lerp(a.z, b.z, t)
            };
        }

        float LengthSq(const DirectX::XMFLOAT3& v)
        {
            return v.x * v.x + v.y * v.y + v.z * v.z;
        }

        DirectX::XMFLOAT3 NormalizeSafe(const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& fallback)
        {
            const float lenSq = LengthSq(v);
            if (lenSq <= 1e-8f)
                return fallback;

            const float invLen = 1.0f / std::sqrt(lenSq);
            return { v.x * invLen, v.y * invLen, v.z * invLen };
        }

        DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir)
        {
            const DirectX::XMFLOAT3 n = NormalizeSafe(dir, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
            const float yaw = std::atan2(n.x, n.z);
            const float pitch = -std::atan2(n.y, std::sqrt(n.x * n.x + n.z * n.z));
            return { pitch, yaw, 0.0f };
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
            const DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&quat);
            const DirectX::XMVECTOR f = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);

            DirectX::XMFLOAT3 f3{};
            DirectX::XMStoreFloat3(&f3, f);

            const float yaw = std::atan2(f3.x, f3.z);
            const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));
            return { pitch, yaw, 0.0f };
        }

        DirectX::XMFLOAT3 SlerpEuler(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            const DirectX::XMFLOAT4 qa = EulerToQuaternion(a);
            const DirectX::XMFLOAT4 qb = EulerToQuaternion(b);
            DirectX::XMFLOAT4 qOut{};
            DirectX::XMStoreFloat4(
                &qOut,
                DirectX::XMQuaternionSlerp(
                    DirectX::XMLoadFloat4(&qa),
                    DirectX::XMLoadFloat4(&qb),
                    Saturate(t)));
            return QuaternionToEuler(qOut);
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

    void BossWinCinematicController::Start()
    {
        // Lock cinematic tuning to the validated scene values at runtime.
        // Inspector-edited values are intentionally overridden for deterministic playback.
        Set_m_autoCreatePostProcessVolume(true);
        Set_m_autoStartOnBossDeath(true);
        Set_m_bloomBoostIntensity(1.08f);
        Set_m_bloomBoostKnee(0.48f);
        Set_m_bloomBoostThreshold(0.85f);
        Set_m_bossEffectPointName("BossEffectPoint");
        Set_m_bossEntityName("Boss");
        Set_m_bossFadeDurationSec(1.6f);
        Set_m_bossFadeStartSec(0.18f);
        Set_m_bossMinAlpha(0.0f);
        Set_m_bossWeaponEntityName("BossWeapon");
        Set_m_disableFollowDuringSequence(true);
        Set_m_disableFollowInputDuringSequence(true);
        Set_m_disableLookAtDuringSequence(true);
        Set_m_disableMainChangerDuringSequence(true);
        Set_m_disableMainChangerOnStart(true);
        Set_m_disableSpringArmDuringSequence(false);
        Set_m_emissiveBloomBoost(1.24f);
        Set_m_enableBloomBoost(true);
        Set_m_enableHotkeys(true);
        Set_m_enableLogs(false);
        Set_m_enableSceneTransition(true);
        Set_m_eyeObjectName("W_EYE");
        Set_m_fadeBossEnabled(true);
        Set_m_focusBlurCenterX(0.5f);
        Set_m_focusBlurCenterY(0.5f);
        Set_m_focusBlurPeakIntensity(1.3f);
        Set_m_focusBlurRadius(0.27f);
        Set_m_focusDistance(3.66f);
        Set_m_focusDurationSec(1.08f);
        Set_m_focusFovDeg(36.96f);
        Set_m_focusHeightOffset(0.71f);
        Set_m_focusHoldSec(4.82f);
        Set_m_focusSideOffset(0.22f);
        Set_m_forceKillKey(7);
        Set_m_mainCameraName("MainCamera");
        Set_m_mainChangerScriptName("MainChangerScript");
        Set_m_nextScenePath("Assets/Scenes/MainGameLoopScene/ClearScene.scene");
        Set_m_playerEntityName("Player(Tia)");
        Set_m_postProcessPriority(2950);
        Set_m_postProcessVolumeName("BossWinCinematicVolume");
        Set_m_previewKey(6);
        Set_m_restoreBossIfNoSceneTransition(true);
        Set_m_returnDurationSec(1.64f);
        Set_m_sceneManagerEntityName("SceneManager");
        Set_m_sceneTransitionDelaySec(0.55f);
        Set_m_shakeAmplitude(0.08f);
        Set_m_shakeBlurDurationSec(0.5f);
        Set_m_shakeBlurPeakIntensity(1.65f);
        Set_m_shakeDecay(1.74f);
        Set_m_shakeDurationSec(0.3f);
        Set_m_shakeFrequency(20.33f);
        Set_m_showEyeDuringSequence(true);
        Set_m_targetLookYOffset(0.76f);
        Set_m_useSmoothBlend(true);
        Set_m_waitBeforeShakeSec(0.77f);

        m_seq = {};
        m_pendingSceneLoad = false;
        m_pendingSceneLoadTimerSec = 0.0f;
        m_controlsOverridden = false;
        m_bossMaterials.clear();
        m_scriptOverrides.clear();
        m_ppvBaseline = {};

        ResolveEntities(true);
        ResolveCameraEntity();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();
        DisableMainChangerIfNeeded(true);

        m_prevBossDead = IsBossDead();
    }

    void BossWinCinematicController::OnDisable()
    {
        AbortSequence(true);
    }

    void BossWinCinematicController::OnDestroy()
    {
        AbortSequence(true);
    }

    void BossWinCinematicController::Update(float deltaTime)
    {
        const float dt = std::max(0.0f, deltaTime);

        ResolveEntities(false);
        ResolveCameraEntity();
        EnsurePostProcessVolume();
        CapturePostProcessBaseline();

        if (m_pendingSceneLoad)
        {
            m_pendingSceneLoadTimerSec += dt;
            if (m_pendingSceneLoadTimerSec >= std::max(0.0f, Get_m_sceneTransitionDelaySec()))
            {
                const std::string nextScene = Get_m_nextScenePath();
                if (!nextScene.empty() && Scenes())
                    Scenes()->LoadSceneFileRequest(nextScene.c_str());

                m_pendingSceneLoad = false;
            }
            return;
        }

        if (m_seq.running)
        {
            UpdateSequence(dt);
            m_prevBossDead = IsBossDead();
            return;
        }

        if (Get_m_enableHotkeys())
        {
            if (auto* input = Input())
            {
                const KeyCode previewKey = static_cast<KeyCode>(Get_m_previewKey());
                const KeyCode forceKillKey = static_cast<KeyCode>(Get_m_forceKillKey());

                if (input->GetKeyDown(previewKey))
                {
                    StartSequence(false);
                    m_prevBossDead = IsBossDead();
                    return;
                }

                if (input->GetKeyDown(forceKillKey))
                {
                    StartSequence(true);
                    m_prevBossDead = IsBossDead();
                    return;
                }
            }
        }

        const bool bossDead = IsBossDead();
        if (Get_m_autoStartOnBossDeath() && bossDead && !m_prevBossDead)
        {
            StartSequence(false);
            m_prevBossDead = IsBossDead();
            return;
        }

        m_prevBossDead = bossDead;
    }

    bool BossWinCinematicController::ResolveEntities(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        m_sceneManagerEntity = InvalidEntityId;
        m_playerEntity = InvalidEntityId;
        m_bossEntity = InvalidEntityId;
        m_bossWeaponEntity = InvalidEntityId;
        m_eyeEntity = InvalidEntityId;

        if (!Get_m_sceneManagerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_sceneManagerEntityName());
            if (go.IsValid())
                m_sceneManagerEntity = go.id();
        }

        if (!Get_m_playerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerEntityName());
            if (go.IsValid())
                m_playerEntity = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[BossWinCinematicController] Player entity not found: %s", Get_m_playerEntityName().c_str());
        }

        if (!Get_m_bossEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossEntityName());
            if (go.IsValid())
                m_bossEntity = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[BossWinCinematicController] Boss entity not found: %s", Get_m_bossEntityName().c_str());
        }

        if (!Get_m_bossWeaponEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossWeaponEntityName());
            if (go.IsValid())
                m_bossWeaponEntity = go.id();
            else if (logWarnings && Get_m_enableLogs())
                ALICE_LOG_WARN("[BossWinCinematicController] Boss weapon entity not found: %s", Get_m_bossWeaponEntityName().c_str());
        }

        if (!Get_m_eyeObjectName().empty())
        {
            const GameObject eyeGo = world->FindGameObject(Get_m_eyeObjectName());
            if (eyeGo.IsValid())
                m_eyeEntity = eyeGo.id();
            else if (m_bossEntity != InvalidEntityId)
                m_eyeEntity = FindDescendantByName(*world, m_bossEntity, Get_m_eyeObjectName());
        }

        return (m_cameraEntity != InvalidEntityId && m_bossEntity != InvalidEntityId);
    }

    bool BossWinCinematicController::ResolveCameraEntity()
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

        if (!Get_m_mainCameraName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_mainCameraName());
            if (go.IsValid() && world->GetComponent<CameraComponent>(go.id()))
            {
                m_cameraEntity = go.id();
                return true;
            }
        }

        if (world->GetComponent<CameraComponent>(GetOwnerId()))
        {
            m_cameraEntity = GetOwnerId();
            return true;
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

    bool BossWinCinematicController::EnsurePostProcessVolume()
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
            m_volumeEntity = id;
            world->SetEntityName(id, Get_m_postProcessVolumeName().empty() ? "BossWinCinematicVolume" : Get_m_postProcessVolumeName());

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

        if (auto* tr = world->GetComponent<TransformComponent>(m_volumeEntity))
        {
            tr->enabled = true;
            tr->visible = true;
        }

        ppv->unbound = true;
        ppv->blendWeight = 1.0f;
        ppv->priority = Get_m_postProcessPriority();
        return true;
    }

    void BossWinCinematicController::CapturePostProcessBaseline()
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

        m_ppvBaseline.bOverrideBloomIntensity = s.bOverride_BloomIntensity;
        m_ppvBaseline.bOverrideBloomThreshold = s.bOverride_BloomThreshold;
        m_ppvBaseline.bOverrideBloomKnee = s.bOverride_BloomKnee;
        m_ppvBaseline.bOverrideEmissiveBloomIntensity = s.bOverride_EmissiveBloomIntensity;
        m_ppvBaseline.bloomIntensity = s.bloomIntensity;
        m_ppvBaseline.bloomThreshold = s.bloomThreshold;
        m_ppvBaseline.bloomKnee = s.bloomKnee;
        m_ppvBaseline.emissiveBloomIntensity = s.emissiveBloomIntensity;
    }

    void BossWinCinematicController::RestorePostProcessBaseline()
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

        s.bOverride_BloomIntensity = m_ppvBaseline.bOverrideBloomIntensity;
        s.bOverride_BloomThreshold = m_ppvBaseline.bOverrideBloomThreshold;
        s.bOverride_BloomKnee = m_ppvBaseline.bOverrideBloomKnee;
        s.bOverride_EmissiveBloomIntensity = m_ppvBaseline.bOverrideEmissiveBloomIntensity;
        s.bloomIntensity = m_ppvBaseline.bloomIntensity;
        s.bloomThreshold = m_ppvBaseline.bloomThreshold;
        s.bloomKnee = m_ppvBaseline.bloomKnee;
        s.emissiveBloomIntensity = m_ppvBaseline.emissiveBloomIntensity;
    }

    bool BossWinCinematicController::TryGetCameraPose(CameraPose& outPose) const
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

    void BossWinCinematicController::ApplyCameraPose(const CameraPose& pose) const
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity);
        auto* cam = world->GetComponent<CameraComponent>(m_cameraEntity);
        if (!tr || !cam)
            return;

        tr->position = pose.position;
        tr->rotation = pose.rotationRad;
        world->MarkTransformDirty(m_cameraEntity);

        cam->SetFov(std::clamp(pose.fovDeg, 1.0f, 170.0f));
    }

    bool BossWinCinematicController::BuildFocusPose(CameraPose& outPose) const
    {
        World* world = GetWorld();
        if (!world || m_bossEntity == InvalidEntityId)
            return false;

        CameraPose current{};
        if (!TryGetCameraPose(current))
            return false;

        DirectX::XMFLOAT3 anchorPos{};
        if (!Get_m_bossEffectPointName().empty())
        {
            GameObject go = world->FindGameObject(Get_m_bossEffectPointName());
            if (go.IsValid())
            {
                if (const auto* tr = world->GetComponent<TransformComponent>(go.id()))
                    anchorPos = tr->position;
            }
            else
            {
                const EntityId anchorId = FindDescendantByName(*world, m_bossEntity, Get_m_bossEffectPointName());
                if (anchorId != InvalidEntityId)
                {
                    if (const auto* tr = world->GetComponent<TransformComponent>(anchorId))
                        anchorPos = tr->position;
                }
            }
        }

        if (LengthSq(anchorPos) <= 1e-8f)
        {
            if (const auto* bossTr = world->GetComponent<TransformComponent>(m_bossEntity))
                anchorPos = bossTr->position;
            else
                return false;
        }

        const DirectX::XMFLOAT3 lookTarget = AddVec(anchorPos, DirectX::XMFLOAT3(0.0f, Get_m_targetLookYOffset(), 0.0f));

        DirectX::XMFLOAT3 toCamera = SubVec(current.position, lookTarget);
        toCamera.y = 0.0f;
        const DirectX::XMFLOAT3 dir = NormalizeSafe(toCamera, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
        const DirectX::XMFLOAT3 right = NormalizeSafe(Cross(DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f), dir), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f));

        DirectX::XMFLOAT3 focusPos = AddVec(lookTarget, MulVec(dir, std::max(0.1f, Get_m_focusDistance())));
        focusPos = AddVec(focusPos, MulVec(right, Get_m_focusSideOffset()));
        focusPos.y = lookTarget.y + Get_m_focusHeightOffset();

        outPose.position = focusPos;
        outPose.rotationRad = DirectionToEuler(SubVec(lookTarget, focusPos));
        outPose.fovDeg = std::clamp(Get_m_focusFovDeg(), 1.0f, 170.0f);
        return true;
    }

    void BossWinCinematicController::SetGameplayCameraControl(bool enable)
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* tr = world->GetComponent<TransformComponent>(m_cameraEntity);
        auto* follow = world->GetComponent<CameraFollowComponent>(m_cameraEntity);
        auto* lookAt = world->GetComponent<CameraLookAtComponent>(m_cameraEntity);
        auto* springArm = world->GetComponent<CameraSpringArmComponent>(m_cameraEntity);
        auto* blend = world->GetComponent<CameraBlendComponent>(m_cameraEntity);

        if (!enable)
        {
            if (m_controlsOverridden)
                return;

            m_hasFollow = (follow != nullptr);
            m_hasLookAt = (lookAt != nullptr);
            m_hasSpringArm = (springArm != nullptr);

            if (m_hasFollow)
            {
                m_savedFollowEnabled = follow->enabled;
                m_savedFollowInputEnabled = follow->enableInput;
            }
            if (m_hasLookAt)
                m_savedLookAtEnabled = lookAt->enabled;
            if (m_hasSpringArm)
                m_savedSpringArmEnabled = springArm->enabled;

            if (m_hasFollow && Get_m_disableFollowDuringSequence())
                follow->enabled = false;
            if (m_hasFollow && Get_m_disableFollowInputDuringSequence())
                follow->enableInput = false;
            if (m_hasLookAt && Get_m_disableLookAtDuringSequence())
                lookAt->enabled = false;
            if (m_hasSpringArm && Get_m_disableSpringArmDuringSequence())
                springArm->enabled = false;

            if (blend)
            {
                blend->active = false;
                blend->needsSnapshot = false;
                blend->targetId = InvalidEntityId;
                blend->targetName.clear();
            }

            m_controlsOverridden = true;
            return;
        }

        if (!m_controlsOverridden)
            return;

        if (m_hasFollow && follow)
        {
            follow->enabled = m_savedFollowEnabled;
            follow->enableInput = m_savedFollowInputEnabled;
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

    void BossWinCinematicController::DisableMainChangerIfNeeded(bool onStart)
    {
        if (onStart)
        {
            if (!Get_m_disableMainChangerOnStart())
                return;
        }
        else
        {
            if (!Get_m_disableMainChangerDuringSequence())
                return;
        }

        World* world = GetWorld();
        if (!world)
            return;

        if (m_sceneManagerEntity == InvalidEntityId)
            return;

        auto* scripts = world->GetScripts(m_sceneManagerEntity);
        if (!scripts)
            return;

        for (auto& scriptComp : *scripts)
        {
            if (scriptComp.scriptName != Get_m_mainChangerScriptName())
                continue;

            const auto alreadySaved = std::find_if(
                m_scriptOverrides.begin(),
                m_scriptOverrides.end(),
                [&](const ScriptState& s)
                {
                    return s.entity == m_sceneManagerEntity && s.scriptName == scriptComp.scriptName;
                });
            if (alreadySaved == m_scriptOverrides.end())
            {
                ScriptState state{};
                state.entity = m_sceneManagerEntity;
                state.scriptName = scriptComp.scriptName;
                state.enabled = scriptComp.enabled;
                m_scriptOverrides.push_back(state);
            }

            scriptComp.enabled = false;
        }
    }

    void BossWinCinematicController::RestoreOverriddenScripts()
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

    void BossWinCinematicController::StartSequence(bool forceBossDead)
    {
        if (!ResolveEntities(true) || !ResolveCameraEntity())
            return;

        if (m_bossEntity == InvalidEntityId || m_cameraEntity == InvalidEntityId)
            return;

        if (m_seq.running)
            AbortSequence(true);

        if (forceBossDead)
            ForceBossDead();

        if (!TryGetCameraPose(m_seq.startPose))
            return;

        if (!BuildFocusPose(m_seq.focusPose))
            return;

        SetGameplayCameraControl(false);
        DisableMainChangerIfNeeded(false);

        EnsurePostProcessVolume();
        CapturePostProcessBaseline();

        RestoreBossMaterials();
        CollectBossMaterials();
        ApplyBossAlpha(1.0f);

        if (Get_m_showEyeDuringSequence() && m_eyeEntity != InvalidEntityId)
            SetEntityVisible(m_eyeEntity, true);

        m_pendingSceneLoad = false;
        m_pendingSceneLoadTimerSec = 0.0f;

        m_seq.running = true;
        m_seq.phase = Phase::FocusIn;
        m_seq.phaseTimerSec = 0.0f;
        m_seq.totalTimerSec = 0.0f;

        ApplyPostProcess(0.0f, Get_m_focusBlurRadius(), Get_m_focusBlurCenterX(), Get_m_focusBlurCenterY(), 0.0f);

        if (Get_m_enableLogs())
            ALICE_LOG_INFO("[BossWinCinematicController] Sequence started (forceKill=%d)", forceBossDead ? 1 : 0);
    }

    void BossWinCinematicController::UpdateSequence(float deltaTime)
    {
        if (!m_seq.running)
            return;

        const float dt = std::max(0.0f, deltaTime);
        m_seq.phaseTimerSec += dt;
        m_seq.totalTimerSec += dt;

        UpdateBossFade();

        CameraPose pose = m_seq.startPose;
        float blur = 0.0f;
        float bloomWeight = 0.0f;

        switch (m_seq.phase)
        {
        case Phase::FocusIn:
        {
            const float duration = std::max(1e-4f, Get_m_focusDurationSec());
            float t = Saturate(m_seq.phaseTimerSec / duration);
            if (Get_m_useSmoothBlend())
                t = SmoothStep01(t);

            pose.position = LerpVec(m_seq.startPose.position, m_seq.focusPose.position, t);
            pose.rotationRad = SlerpEuler(m_seq.startPose.rotationRad, m_seq.focusPose.rotationRad, t);
            pose.fovDeg = Lerp(m_seq.startPose.fovDeg, m_seq.focusPose.fovDeg, t);

            blur = Get_m_focusBlurPeakIntensity() * t;
            bloomWeight = t;

            if (m_seq.phaseTimerSec >= duration)
                AdvancePhase(Phase::FocusHold);
            break;
        }
        case Phase::FocusHold:
        {
            pose = m_seq.focusPose;
            blur = Get_m_focusBlurPeakIntensity();
            bloomWeight = 1.0f;

            if (m_seq.phaseTimerSec >= std::max(0.0f, Get_m_focusHoldSec()))
                AdvancePhase(Phase::Return);
            break;
        }
        case Phase::Return:
        {
            const float duration = std::max(1e-4f, Get_m_returnDurationSec());
            float t = Saturate(m_seq.phaseTimerSec / duration);
            if (Get_m_useSmoothBlend())
                t = SmoothStep01(t);

            pose.position = LerpVec(m_seq.focusPose.position, m_seq.startPose.position, t);
            pose.rotationRad = SlerpEuler(m_seq.focusPose.rotationRad, m_seq.startPose.rotationRad, t);
            pose.fovDeg = Lerp(m_seq.focusPose.fovDeg, m_seq.startPose.fovDeg, t);

            blur = Get_m_focusBlurPeakIntensity() * (1.0f - t);
            bloomWeight = 1.0f - t;

            if (m_seq.phaseTimerSec >= duration)
                AdvancePhase(Phase::WaitShake);
            break;
        }
        case Phase::WaitShake:
        {
            pose = m_seq.startPose;
            blur = 0.0f;
            bloomWeight = 0.0f;

            if (m_seq.phaseTimerSec >= std::max(0.0f, Get_m_waitBeforeShakeSec()))
            {
                TriggerShake();
                AdvancePhase(Phase::Shake);
            }
            break;
        }
        case Phase::Shake:
        {
            pose = m_seq.startPose;
            const float duration = std::max(1e-4f, Get_m_shakeBlurDurationSec());
            const float t = Saturate(m_seq.phaseTimerSec / duration);

            blur = Get_m_shakeBlurPeakIntensity() * (1.0f - t);
            // Final phase bloom disabled by request.
            bloomWeight = 0.0f;

            if (m_seq.phaseTimerSec >= duration)
                FinishSequence();
            break;
        }
        case Phase::Idle:
        default:
            break;
        }

        ApplyCameraPose(pose);
        ApplyPostProcess(blur, Get_m_focusBlurRadius(), Get_m_focusBlurCenterX(), Get_m_focusBlurCenterY(), bloomWeight);
    }

    void BossWinCinematicController::FinishSequence()
    {
        const bool hasScenePath = !Get_m_nextScenePath().empty();
        const bool willLoadScene = Get_m_enableSceneTransition() && hasScenePath;

        m_seq = {};
        ApplyPostProcess(0.0f, Get_m_focusBlurRadius(), Get_m_focusBlurCenterX(), Get_m_focusBlurCenterY(), 0.0f);
        RestorePostProcessBaseline();
        SetGameplayCameraControl(true);

        if (!willLoadScene && Get_m_restoreBossIfNoSceneTransition())
            RestoreBossMaterials();

        if (willLoadScene)
        {
            m_pendingSceneLoad = true;
            m_pendingSceneLoadTimerSec = 0.0f;
        }
        else
        {
            RestoreOverriddenScripts();
        }

        if (Get_m_enableLogs())
            ALICE_LOG_INFO("[BossWinCinematicController] Sequence finished.");
    }

    void BossWinCinematicController::AbortSequence(bool restoreBoss)
    {
        m_seq = {};
        m_pendingSceneLoad = false;
        m_pendingSceneLoadTimerSec = 0.0f;

        RestorePostProcessBaseline();
        SetGameplayCameraControl(true);
        RestoreOverriddenScripts();

        if (restoreBoss)
            RestoreBossMaterials();
    }

    void BossWinCinematicController::UpdateBossFade()
    {
        if (!Get_m_fadeBossEnabled())
            return;

        if (m_bossMaterials.empty())
            return;

        const float startSec = std::max(0.0f, Get_m_bossFadeStartSec());
        const float fadeDuration = std::max(0.0f, Get_m_bossFadeDurationSec());
        const float minAlpha = Saturate(Get_m_bossMinAlpha());

        float alpha = 1.0f;
        if (m_seq.totalTimerSec >= startSec)
        {
            if (fadeDuration <= 1e-6f)
            {
                alpha = minAlpha;
            }
            else
            {
                const float t = Saturate((m_seq.totalTimerSec - startSec) / fadeDuration);
                alpha = Lerp(1.0f, minAlpha, t);
            }
        }

        ApplyBossAlpha(alpha);
    }

    void BossWinCinematicController::ApplyBossAlpha(float alpha)
    {
        World* world = GetWorld();
        if (!world)
            return;

        const float clampedAlpha = Saturate(alpha);
        for (const MaterialState& state : m_bossMaterials)
        {
            auto* mat = world->GetComponent<MaterialComponent>(state.entity);
            if (mat)
            {
                mat->Set_alpha(clampedAlpha);
                const bool makeTransparent = (clampedAlpha < 0.999f) ? true : state.transparent;
                mat->Set_transparent(makeTransparent);
            }

            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                if (clampedAlpha > 1e-3f)
                {
                    tr->enabled = true;
                    tr->visible = true;
                }
                else
                {
                    tr->visible = false;
                }
                world->MarkTransformDirty(state.entity);
            }
        }
    }

    void BossWinCinematicController::CollectBossMaterials()
    {
        World* world = GetWorld();
        if (!world || m_bossEntity == InvalidEntityId)
            return;

        m_bossMaterials.clear();
        std::unordered_set<EntityId> visited;
        std::vector<EntityId> stack;
        stack.push_back(m_bossEntity);
        if (m_bossWeaponEntity != InvalidEntityId)
            stack.push_back(m_bossWeaponEntity);

        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();
            if (current == InvalidEntityId)
                continue;
            if (!visited.insert(current).second)
                continue;

            if (auto* mat = world->GetComponent<MaterialComponent>(current))
            {
                MaterialState state{};
                state.entity = current;
                state.alpha = mat->Get_alpha();
                state.transparent = mat->Get_transparent();
                m_bossMaterials.push_back(state);
            }

            const auto children = world->GetChildren(current);
            for (const EntityId child : children)
                stack.push_back(child);
        }
    }

    void BossWinCinematicController::RestoreBossMaterials()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_bossMaterials.clear();
            return;
        }

        for (const MaterialState& state : m_bossMaterials)
        {
            if (auto* mat = world->GetComponent<MaterialComponent>(state.entity))
            {
                mat->Set_alpha(state.alpha);
                mat->Set_transparent(state.transparent);
            }

            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->enabled = true;
                tr->visible = true;
                world->MarkTransformDirty(state.entity);
            }
        }

        m_bossMaterials.clear();
    }

    void BossWinCinematicController::SetEntityVisible(EntityId id, bool visible)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->enabled = visible;
            tr->visible = visible;
            world->MarkTransformDirty(id);
        }
    }

    bool BossWinCinematicController::IsBossDead() const
    {
        World* world = GetWorld();
        if (!world || m_bossEntity == InvalidEntityId)
            return false;

        const auto* health = world->GetComponent<HealthComponent>(m_bossEntity);
        if (!health)
            return false;

        return (!health->alive || health->currentHealth <= 0.0f);
    }

    void BossWinCinematicController::ForceBossDead()
    {
        World* world = GetWorld();
        if (!world || m_bossEntity == InvalidEntityId)
            return;

        if (auto* health = world->GetComponent<HealthComponent>(m_bossEntity))
        {
            health->currentHealth = 0.0f;
            health->alive = false;
        }
    }

    void BossWinCinematicController::ApplyPostProcess(float blurIntensity, float blurRadius, float centerX, float centerY, float bloomWeight)
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
        s.impactBlurIntensity = std::max(0.0f, blurIntensity);
        s.impactBlurRadius = std::max(0.01f, blurRadius);
        s.impactBlurCenterX = Saturate(centerX);
        s.impactBlurCenterY = Saturate(centerY);

        const float bloomT = Saturate(bloomWeight);
        if (Get_m_enableBloomBoost())
        {
            s.bOverride_BloomIntensity = true;
            s.bOverride_BloomThreshold = true;
            s.bOverride_BloomKnee = true;
            s.bOverride_EmissiveBloomIntensity = true;

            s.bloomIntensity = std::max(0.0f, Get_m_bloomBoostIntensity()) * bloomT;
            s.bloomThreshold = std::max(0.0f, Get_m_bloomBoostThreshold());
            s.bloomKnee = std::clamp(Get_m_bloomBoostKnee(), 0.0f, 1.0f);
            s.emissiveBloomIntensity = Lerp(1.0f, std::max(0.0f, Get_m_emissiveBloomBoost()), bloomT);
        }
    }

    void BossWinCinematicController::TriggerShake()
    {
        World* world = GetWorld();
        if (!world || m_cameraEntity == InvalidEntityId)
            return;

        auto* shake = world->GetComponent<CameraShakeComponent>(m_cameraEntity);
        if (!shake)
            shake = &world->AddComponent<CameraShakeComponent>(m_cameraEntity);

        shake->enabled = true;
        shake->amplitude = std::max(0.0f, Get_m_shakeAmplitude());
        shake->frequency = std::max(0.0f, Get_m_shakeFrequency());
        shake->duration = std::max(0.0f, Get_m_shakeDurationSec());
        shake->decay = std::max(0.0f, Get_m_shakeDecay());
        shake->elapsed = 0.0f;
    }

    void BossWinCinematicController::AdvancePhase(Phase nextPhase)
    {
        m_seq.phase = nextPhase;
        m_seq.phaseTimerSec = 0.0f;
    }
}
