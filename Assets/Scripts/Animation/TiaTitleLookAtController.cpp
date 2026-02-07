#include "TiaTitleLookAtController.h"

#include <algorithm>
#include <cmath>

#include <DirectXMath.h>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(TiaTitleLookAtController);

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
    }

    void TiaTitleLookAtController::Awake()
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* cam = go.GetComponent<CameraComponent>();
        if (!cam) cam = &go.AddComponent<CameraComponent>();
        cam->SetPrimary(true);

        auto* follow = go.GetComponent<CameraFollowComponent>();
        if (!follow) follow = &go.AddComponent<CameraFollowComponent>();
        follow->enabled = true;
        follow->enableInput = true;
        follow->targetName = Get_m_targetName();
        follow->pitchMinDeg = Get_m_pitchMinDeg();
        follow->pitchMaxDeg = Get_m_pitchMaxDeg();
        follow->sensitivity = Get_m_orbitSensitivity();
        follow->shoulderOffset = 0.0f;
        follow->shoulderSide = 1.0f;
        follow->enableLockOn = false;
        follow->allowManualOrbitInLockOn = true;
        follow->lockOnActive = false;
        follow->mouseLocked = false;
        follow->mode = 0;

        auto* spring = go.GetComponent<CameraSpringArmComponent>();
        if (!spring) spring = &go.AddComponent<CameraSpringArmComponent>();
        spring->enabled = true;
        spring->enableZoom = true;
        spring->minDistance = Get_m_zoomMinDistance();
        spring->maxDistance = std::max(Get_m_zoomMinDistance(), Get_m_zoomMaxDistance());
        spring->zoomSpeed = Get_m_zoomSpeed();
        if (spring->desiredDistance <= 0.0f)
            spring->desiredDistance = spring->distance;
        spring->desiredDistance = std::clamp(spring->desiredDistance, spring->minDistance, spring->maxDistance);
    }

    void TiaTitleLookAtController::Start()
    {
        ResolveTarget();
        ApplyInitialIdleClip();
    }

    void TiaTitleLookAtController::OnDisable()
    {
        RestoreOriginalAim();
    }

    void TiaTitleLookAtController::OnDestroy()
    {
        RestoreOriginalAim();
    }

    void TiaTitleLookAtController::ResolveTarget()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        if (m_targetId != InvalidEntityId && world->IsEntityValid(m_targetId, m_targetGen))
            return;

        m_targetId = InvalidEntityId;
        m_targetGen = 0;

        if (Get_m_targetName().empty())
            return;

        GameObject targetGo = world->FindGameObject(Get_m_targetName());
        if (!targetGo.IsValid())
            return;

        m_targetId = targetGo.id();
        m_targetGen = world->GetEntityGeneration(m_targetId);
    }

    void TiaTitleLookAtController::ApplyInitialIdleClip()
    {
        if (!Get_m_forceIdleOnStart() || m_idleApplied)
            return;

        auto* world = GetWorld();
        if (!world)
            return;

        ResolveTarget();
        if (m_targetId == InvalidEntityId || !world->IsEntityValid(m_targetId, m_targetGen))
            return;

        auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_targetId);
        if (!anim)
            anim = &world->AddComponent<AdvancedAnimationComponent>(m_targetId);
        if (!anim)
            return;

        const std::string idleClip = Get_m_idleClipName();
        if (!idleClip.empty())
        {
            anim->enabled = true;
            anim->playing = true;
            anim->base.enabled = true;
            anim->base.autoAdvance = true;
            anim->base.clipA = idleClip;
            anim->base.clipB = idleClip;
            anim->base.blend01 = 0.0f;
            anim->base.loopA = true;
            anim->base.loopB = true;
            anim->base.speedA = 1.0f;
            anim->base.speedB = 1.0f;
            anim->base.timeA = 0.0f;
            anim->base.timeB = 0.0f;
        }

        m_idleApplied = true;
    }

    float TiaTitleLookAtController::SmoothAlpha(float speed, float dt) const
    {
        if (speed <= 0.0f || dt <= 0.0f)
            return 1.0f;
        return std::clamp(1.0f - std::exp(-speed * dt), 0.0f, 1.0f);
    }

    float TiaTitleLookAtController::WrapPi(float rad)
    {
        while (rad > kPi) rad -= 2.0f * kPi;
        while (rad < -kPi) rad += 2.0f * kPi;
        return rad;
    }

    void TiaTitleLookAtController::RestoreOriginalAim()
    {
        if (!m_savedAimValid)
            return;

        auto* world = GetWorld();
        if (!world)
            return;
        if (m_targetId == InvalidEntityId || !world->IsEntityValid(m_targetId, m_targetGen))
            return;

        auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_targetId);
        if (!anim)
            return;

        anim->aim.enabled = m_savedAimEnabled;
        anim->aim.yawRad = m_savedAimYawRad;
        anim->aim.pitchRad = m_savedAimPitchRad;
        anim->aim.weight = m_savedAimWeight;
    }

    void TiaTitleLookAtController::Update(float deltaTime)
    {
        auto* world = GetWorld();
        auto* input = Input();
        auto cameraGo = gameObject();
        if (!world || !input || !cameraGo.IsValid())
            return;

        auto* cameraTr = cameraGo.GetComponent<TransformComponent>();
        auto* follow = cameraGo.GetComponent<CameraFollowComponent>();
        auto* spring = cameraGo.GetComponent<CameraSpringArmComponent>();
        if (!cameraTr || !follow || !spring)
            return;

        ResolveTarget();
        ApplyInitialIdleClip();

        if (m_targetId == InvalidEntityId || !world->IsEntityValid(m_targetId, m_targetGen))
            return;

        auto* targetTr = world->GetComponent<TransformComponent>(m_targetId);
        auto* targetAnim = world->GetComponent<AdvancedAnimationComponent>(m_targetId);
        if (!targetTr || !targetAnim)
            return;

        DirectX::XMMATRIX targetWorld = world->ComputeWorldMatrix(m_targetId);
        DirectX::XMFLOAT4X4 targetWorldF{};
        DirectX::XMStoreFloat4x4(&targetWorldF, targetWorld);
        const DirectX::XMFLOAT3 targetWorldPos = {
            targetWorldF._41,
            targetWorldF._42,
            targetWorldF._43
        };

        if (!m_savedAimValid)
        {
            m_savedAimEnabled = targetAnim->aim.enabled;
            m_savedAimYawRad = targetAnim->aim.yawRad;
            m_savedAimPitchRad = targetAnim->aim.pitchRad;
            m_savedAimWeight = targetAnim->aim.weight;
            m_savedAimValid = true;
        }

        follow->targetName = Get_m_targetName();
        follow->pitchMinDeg = Get_m_pitchMinDeg();
        follow->pitchMaxDeg = Get_m_pitchMaxDeg();

        spring->minDistance = Get_m_zoomMinDistance();
        spring->maxDistance = std::max(Get_m_zoomMinDistance(), Get_m_zoomMaxDistance());
        spring->zoomSpeed = Get_m_zoomSpeed();
        spring->desiredDistance = std::clamp(spring->desiredDistance, spring->minDistance, spring->maxDistance);

        if (input->GetMouseButton(MouseCode::Left))
        {
            follow->yawDeg += input->GetMouseDeltaX() * Get_m_orbitSensitivity();
            follow->pitchDeg += input->GetMouseDeltaY() * Get_m_orbitSensitivity();
            follow->pitchDeg = std::clamp(follow->pitchDeg, follow->pitchMinDeg, follow->pitchMaxDeg);
        }

        const float wheel = input->GetMouseScrollDelta();
        if (std::abs(wheel) > 1e-6f)
        {
            spring->desiredDistance = std::clamp(
                spring->desiredDistance - wheel * spring->zoomSpeed,
                spring->minDistance,
                spring->maxDistance);
        }
        if (input->GetMouseButton(MouseCode::Middle))
        {
            spring->desiredDistance = std::clamp(
                spring->desiredDistance + input->GetMouseDeltaY() * spring->zoomSpeed * 0.02f,
                spring->minDistance,
                spring->maxDistance);
        }

        // Use face bone world position as camera pivot/zoom anchor when available.
        DirectX::XMFLOAT3 faceWorld = targetWorldPos;
        faceWorld.y += Get_m_faceFallbackHeight() + Get_m_faceExtraHeightOffset();
        if (!Get_m_faceBoneName().empty())
        {
            faceWorld = targetAnim->GetWorldLocationToBone(Get_m_faceBoneName(), targetWorldF);
            faceWorld.y += Get_m_faceExtraHeightOffset();
        }

        follow->heightOffset = faceWorld.y - targetWorldPos.y;

        const DirectX::XMFLOAT3 toCam = {
            cameraTr->position.x - faceWorld.x,
            cameraTr->position.y - faceWorld.y,
            cameraTr->position.z - faceWorld.z
        };
        const DirectX::XMFLOAT3 toCamFlat = {
            cameraTr->position.x - faceWorld.x,
            0.0f,
            cameraTr->position.z - faceWorld.z
        };

        float targetYawRad = 0.0f;
        float targetPitchRad = 0.0f;
        float targetWeight = 0.0f;
        float debugRelYawRad = 0.0f;
        float debugRelPitchRad = 0.0f;
        bool debugInsideCone = false;

        const float lenSq = toCamFlat.x * toCamFlat.x + toCamFlat.z * toCamFlat.z;
        if (Get_m_enableLookAt() && lenSq > 1e-8f)
        {
            const float cameraYaw = std::atan2(toCamFlat.x, toCamFlat.z);
            const float actorYaw = targetTr->rotation.y + DirectX::XMConvertToRadians(Get_m_characterForwardOffsetDeg());
            const float relYaw = WrapPi(cameraYaw - actorYaw);
            const float absYaw = std::abs(relYaw);
            debugRelYawRad = relYaw;
            const float horizontalLen = std::sqrt(lenSq);
            const float relPitch = std::atan2(toCam.y, horizontalLen);
            const float absPitch = std::abs(relPitch);
            debugRelPitchRad = relPitch;

            const float maxYaw = DirectX::XMConvertToRadians(std::max(0.0f, Get_m_maxLookYawDeg()));
            const float maxPitch = DirectX::XMConvertToRadians(std::max(0.0f, Get_m_maxLookPitchDeg()));
            const float soft = DirectX::XMConvertToRadians(std::max(0.0f, Get_m_lookBoundarySoftnessDeg()));

            if (maxYaw > 1e-6f && maxPitch > 1e-6f && absYaw <= maxYaw && absPitch <= maxPitch)
            {
                debugInsideCone = true;
                targetYawRad = std::clamp(relYaw, -maxYaw, maxYaw) * Get_m_aimYawScale();
                targetPitchRad = std::clamp(relPitch, -maxPitch, maxPitch) * Get_m_aimPitchScale();

                float yawW = 1.0f;
                if (soft <= 1e-6f || absYaw <= (maxYaw - soft))
                {
                    yawW = 1.0f;
                }
                else
                {
                    const float edge0 = maxYaw - soft;
                    const float t = std::clamp((absYaw - edge0) / soft, 0.0f, 1.0f);
                    yawW = 1.0f - t;
                }

                float pitchW = 1.0f;
                if (soft > 1e-6f && absPitch > (maxPitch - soft))
                {
                    const float edge0 = maxPitch - soft;
                    const float t = std::clamp((absPitch - edge0) / soft, 0.0f, 1.0f);
                    pitchW = 1.0f - t;
                }

                targetWeight = std::min(yawW, pitchW);
            }
        }

        const float yawAlpha = SmoothAlpha(Get_m_yawSmoothSpeed(), deltaTime);
        const float pitchAlpha = SmoothAlpha(Get_m_pitchSmoothSpeed(), deltaTime);
        const float weightAlpha = SmoothAlpha(Get_m_weightSmoothSpeed(), deltaTime);
        m_smoothedYawRad += (targetYawRad - m_smoothedYawRad) * yawAlpha;
        m_smoothedPitchRad += (targetPitchRad - m_smoothedPitchRad) * pitchAlpha;
        m_smoothedWeight += (targetWeight - m_smoothedWeight) * weightAlpha;

        targetAnim->aim.enabled = (m_smoothedWeight > 0.001f);
        targetAnim->aim.yawRad = m_smoothedYawRad;
        targetAnim->aim.pitchRad = m_smoothedPitchRad;
        targetAnim->aim.weight = std::clamp(Get_m_aimWeight() * m_smoothedWeight, 0.0f, 1.0f);

        if (Get_m_debugAimLog())
        {
            m_debugLogTimer += deltaTime;
            const float interval = std::max(0.02f, Get_m_debugLogInterval());
            if (m_debugLogTimer >= interval)
            {
                m_debugLogTimer = 0.0f;
                ALICE_LOG_ERRORF(
                    "[TiaTitleLookAt] relYawDeg=%.2f relPitchDeg=%.2f inside=%s aimEnabled=%s yawRad=%.4f pitchRad=%.4f weight=%.3f fwdOffsetDeg=%.1f",
                    DirectX::XMConvertToDegrees(debugRelYawRad),
                    DirectX::XMConvertToDegrees(debugRelPitchRad),
                    debugInsideCone ? "Y" : "N",
                    targetAnim->aim.enabled ? "Y" : "N",
                    targetAnim->aim.yawRad,
                    targetAnim->aim.pitchRad,
                    targetAnim->aim.weight,
                    Get_m_characterForwardOffsetDeg());
            }
        }
        else
        {
            m_debugLogTimer = 0.0f;
        }
    }
}
