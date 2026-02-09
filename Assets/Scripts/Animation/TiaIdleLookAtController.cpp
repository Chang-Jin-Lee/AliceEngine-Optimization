#include "TiaIdleLookAtController.h"

#include <algorithm>
#include <cmath>

#include <DirectXMath.h>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"

#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(TiaIdleLookAtController);

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
    }

    void TiaIdleLookAtController::Awake()
    {
        ResolveTarget();
        ResolveSession();
    }

    void TiaIdleLookAtController::Start()
    {
        ResolveTarget();
        ResolveSession();
    }

    void TiaIdleLookAtController::OnDisable()
    {
        RestoreOriginalAim();
    }

    void TiaIdleLookAtController::OnDestroy()
    {
        RestoreOriginalAim();
    }

    void TiaIdleLookAtController::ResolveTarget()
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

    void TiaIdleLookAtController::ResolveSession()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        if (m_sessionEntityId != InvalidEntityId && world->IsEntityValid(m_sessionEntityId, m_sessionEntityGen))
            return;

        m_sessionEntityId = InvalidEntityId;
        m_sessionEntityGen = 0;

        if (Get_m_sessionEntityName().empty())
            return;

        GameObject sessionGo = world->FindGameObject(Get_m_sessionEntityName());
        if (!sessionGo.IsValid())
            return;

        m_sessionEntityId = sessionGo.id();
        m_sessionEntityGen = world->GetEntityGeneration(m_sessionEntityId);
    }

    float TiaIdleLookAtController::SmoothAlpha(float speed, float dt) const
    {
        if (speed <= 0.0f || dt <= 0.0f)
            return 1.0f;
        return std::clamp(1.0f - std::exp(-speed * dt), 0.0f, 1.0f);
    }

    float TiaIdleLookAtController::WrapPi(float rad)
    {
        while (rad > kPi) rad -= 2.0f * kPi;
        while (rad < -kPi) rad += 2.0f * kPi;
        return rad;
    }

    bool TiaIdleLookAtController::IsIdleState() const
    {
        if (!Get_m_requireIdleState())
            return true;

        auto* world = GetWorld();
        if (!world)
            return false;

        if (m_sessionEntityId == InvalidEntityId || !world->IsEntityValid(m_sessionEntityId, m_sessionEntityGen))
            return false;

        const auto* session = world->GetComponent<C_CombatSessionComponent>(m_sessionEntityId);
        if (!session)
            return false;

        return session->GetPlayerState() == Combat::ActionState::Idle;
    }

    void TiaIdleLookAtController::RestoreOriginalAim()
    {
        if (!m_savedAimValid)
        {
            m_isControllingAim = false;
            return;
        }

        auto* world = GetWorld();
        if (!world)
        {
            m_savedAimValid = false;
            m_isControllingAim = false;
            return;
        }

        if (m_targetId == InvalidEntityId || !world->IsEntityValid(m_targetId, m_targetGen))
        {
            m_savedAimValid = false;
            m_isControllingAim = false;
            return;
        }

        auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_targetId);
        if (!anim)
        {
            m_savedAimValid = false;
            m_isControllingAim = false;
            return;
        }

        anim->aim.enabled = m_savedAimEnabled;
        anim->aim.yawRad = m_savedAimYawRad;
        anim->aim.pitchRad = m_savedAimPitchRad;
        anim->aim.weight = m_savedAimWeight;

        m_savedAimValid = false;
        m_isControllingAim = false;
        m_smoothedYawRad = 0.0f;
        m_smoothedPitchRad = 0.0f;
        m_smoothedWeight = 0.0f;
    }

    void TiaIdleLookAtController::Update(float deltaTime)
    {
        auto* world = GetWorld();
        auto cameraGo = gameObject();
        if (!world || !cameraGo.IsValid())
            return;

        auto* cameraTr = cameraGo.GetComponent<TransformComponent>();
        if (!cameraTr)
            return;

        ResolveTarget();
        ResolveSession();

        if (m_targetId == InvalidEntityId || !world->IsEntityValid(m_targetId, m_targetGen))
        {
            if (m_isControllingAim)
                RestoreOriginalAim();
            return;
        }

        auto* targetTr = world->GetComponent<TransformComponent>(m_targetId);
        auto* targetAnim = world->GetComponent<AdvancedAnimationComponent>(m_targetId);
        if (!targetTr || !targetAnim)
        {
            if (m_isControllingAim)
                RestoreOriginalAim();
            return;
        }

        const bool idleState = IsIdleState();
        const bool canControlAim = Get_m_enableLookAt() && idleState;
        if (!canControlAim)
        {
            if (m_isControllingAim)
                RestoreOriginalAim();
            return;
        }

        if (!m_isControllingAim)
        {
            m_savedAimEnabled = targetAnim->aim.enabled;
            m_savedAimYawRad = targetAnim->aim.yawRad;
            m_savedAimPitchRad = targetAnim->aim.pitchRad;
            m_savedAimWeight = targetAnim->aim.weight;
            m_savedAimValid = true;
            m_isControllingAim = true;
            m_smoothedYawRad = targetAnim->aim.yawRad;
            m_smoothedPitchRad = targetAnim->aim.pitchRad;
            m_smoothedWeight = std::clamp(targetAnim->aim.weight, 0.0f, 1.0f);
        }

        DirectX::XMMATRIX targetWorld = world->ComputeWorldMatrix(m_targetId);
        DirectX::XMFLOAT4X4 targetWorldF{};
        DirectX::XMStoreFloat4x4(&targetWorldF, targetWorld);
        const DirectX::XMFLOAT3 targetWorldPos = {
            targetWorldF._41,
            targetWorldF._42,
            targetWorldF._43
        };

        DirectX::XMFLOAT3 faceWorld = targetWorldPos;
        faceWorld.y += Get_m_faceFallbackHeight() + Get_m_faceExtraHeightOffset();

        if (!Get_m_faceBoneName().empty())
        {
            faceWorld = targetAnim->GetWorldLocationToBone(Get_m_faceBoneName(), targetWorldF);
            faceWorld.y += Get_m_faceExtraHeightOffset();
        }

        const DirectX::XMFLOAT3 toCam = {
            cameraTr->position.x - faceWorld.x,
            cameraTr->position.y - faceWorld.y,
            cameraTr->position.z - faceWorld.z
        };
        const DirectX::XMFLOAT3 toCamFlat = {
            toCam.x,
            0.0f,
            toCam.z
        };

        float targetYawRad = 0.0f;
        float targetPitchRad = 0.0f;
        float targetWeight = 0.0f;
        float debugRelYawRad = 0.0f;
        float debugRelPitchRad = 0.0f;
        bool debugInsideCone = false;

        const float lenSq = toCamFlat.x * toCamFlat.x + toCamFlat.z * toCamFlat.z;
        if (lenSq > 1e-8f)
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
                if (soft > 1e-6f && absYaw > (maxYaw - soft))
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
                    "[TiaIdleLookAt] idle=%s relYawDeg=%.2f relPitchDeg=%.2f inside=%s aimEnabled=%s yawRad=%.4f pitchRad=%.4f weight=%.3f",
                    idleState ? "Y" : "N",
                    DirectX::XMConvertToDegrees(debugRelYawRad),
                    DirectX::XMConvertToDegrees(debugRelPitchRad),
                    debugInsideCone ? "Y" : "N",
                    targetAnim->aim.enabled ? "Y" : "N",
                    targetAnim->aim.yawRad,
                    targetAnim->aim.pitchRad,
                    targetAnim->aim.weight);
            }
        }
        else
        {
            m_debugLogTimer = 0.0f;
        }
    }
}
