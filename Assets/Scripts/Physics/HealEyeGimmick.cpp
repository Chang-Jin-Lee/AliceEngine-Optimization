#include "HealEyeGimmick.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <cctype>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"

namespace Alice
{
    namespace
    {
        float Clamp(float v, float minV, float maxV)
        {
            return std::max(minV, std::min(maxV, v));
        }

        float SmoothStep(float t)
        {
            float clamped = Clamp(t, 0.0f, 1.0f);
            return clamped * clamped * (3.0f - 2.0f * clamped);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        std::string Trim(const std::string& s)
        {
            size_t a = 0;
            while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            size_t b = s.size();
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
        }
    }

    REGISTER_SCRIPT(HealEyeGimmick);

    void HealEyeGimmick::Start()
    {
        FindEntities();
        if (!m_initialized)
            return;

        m_weaponDefaultAlpha = GetMaterialAlpha(m_weaponCombined, 1.0f);
        m_currentWeaponAlpha = m_weaponDefaultAlpha;
        m_currentEyeAlpha = m_eyeIdleAlpha;
        SetUnityVfxAlpha(m_playerHealEffect, m_currentEyeAlpha);

        EnterIdle();
    }

    void HealEyeGimmick::Update(float deltaTime)
    {
        if (!m_initialized)
            return;

        if (m_transitionDuration > 0.0f)
            UpdateTransition(deltaTime);

        if (m_phase == Phase::Loop)
            UpdateEyeFloat(deltaTime);
    }

    void HealEyeGimmick::BeginHeal(float enterDurationSec)
    {
        if (!m_initialized)
            return;

        SetEnabled(m_weaponCombined, true);
        SetVisible(m_weaponCombined, true);
        SetEnabled(m_eye, true);
        SetVisible(m_eye, true);
        ShowShards(true);

        m_eyeFloatAnchorValid = false;
        m_eyeBaseRotationValid = false;
        m_bobTime = 0.0f;
        m_spinYawDeg = 0.0f;
        m_loopRequested = false;
        m_pendingShardCombinePulseCount = 0;
        m_pendingEyeCombinePulse = false;
        m_enterNextShardThresholdIndex = 0;
        m_enterEyeCombineReached = false;

        m_currentWeaponAlpha = m_weaponDefaultAlpha;
        m_currentEyeAlpha = 0.0f;
        SetMaterialAlpha(m_weaponCombined, m_currentWeaponAlpha);
        SetMaterialAlpha(m_eye, m_currentEyeAlpha);
        SetUnityVfxAlpha(m_playerHealEffect, m_currentEyeAlpha);

        const float duration = ResolveFadeDuration(enterDurationSec, m_enterFadeRatio);
        StartTransition(m_currentWeaponAlpha, 0.0f, m_currentEyeAlpha, 1.0f, duration);
        m_phase = Phase::Entering;
    }

    void HealEyeGimmick::BeginHealLoop()
    {
        if (!m_initialized)
            return;

        if (m_phase == Phase::Loop)
            return;

        m_loopRequested = true;
        if (m_transitionDuration <= 0.0f && m_phase != Phase::Exiting)
        {
            m_phase = Phase::Loop;
            m_eyeFloatAnchorValid = false;
            m_bobTime = 0.0f;
        }
    }

    void HealEyeGimmick::EndHeal(float exitDurationSec)
    {
        if (!m_initialized)
            return;

        if (m_phase == Phase::Idle && m_transitionDuration <= 0.0f)
        {
            EnterIdle();
            return;
        }

        m_loopRequested = false;
        m_pendingShardCombinePulseCount = 0;
        m_pendingEyeCombinePulse = false;
        m_enterNextShardThresholdIndex = 0;
        m_enterEyeCombineReached = false;
        const float duration = ResolveFadeDuration(exitDurationSec, m_exitFadeRatio);
        StartTransition(m_currentWeaponAlpha, m_weaponDefaultAlpha, m_currentEyeAlpha, m_eyeIdleAlpha, duration);
        m_phase = Phase::Exiting;
    }

    int HealEyeGimmick::ConsumeShardCombinePulseCount()
    {
        const int pulseCount = std::max(0, m_pendingShardCombinePulseCount);
        m_pendingShardCombinePulseCount = 0;
        return pulseCount;
    }

    bool HealEyeGimmick::ConsumeEyeCombinePulse()
    {
        const bool pending = m_pendingEyeCombinePulse;
        m_pendingEyeCombinePulse = false;
        return pending;
    }

    void HealEyeGimmick::FindEntities()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        auto findByName = [&](const std::string& name) -> EntityId {
            if (name.empty())
                return InvalidEntityId;
            GameObject go = world->FindGameObject(name);
            return go.IsValid() ? go.id() : InvalidEntityId;
        };

        m_weaponCombined = findByName(m_weaponCombinedName);
        m_eye = !m_eyeName.empty() ? findByName(m_eyeName) : GetOwnerId();
        if (m_eye == InvalidEntityId)
            m_eye = GetOwnerId();
        m_playerHealEffect = findByName(m_playerHealEffectName);

        m_shards.clear();
        std::stringstream ss(m_shardNamesCsv);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string name = Trim(item);
            if (name.empty())
                continue;
            EntityId id = findByName(name);
            if (id != InvalidEntityId)
            {
                ShardState shard{};
                shard.id = id;
                shard.name = name;
                m_shards.push_back(shard);
            }
            else if (m_enableLogs)
            {
                ALICE_LOG_WARN("[HealEyeGimmick] Shard not found: %s", name.c_str());
            }
        }

        m_initialized = (m_weaponCombined != InvalidEntityId && m_eye != InvalidEntityId);
        if (!m_initialized && m_enableLogs)
        {
            ALICE_LOG_WARN("[HealEyeGimmick] Missing required entities. WeaponCombined=%llu Eye=%llu",
                static_cast<unsigned long long>(m_weaponCombined),
                static_cast<unsigned long long>(m_eye));
        }
    }

    void HealEyeGimmick::EnterIdle()
    {
        m_phase = Phase::Idle;
        m_transitionTimer = 0.0f;
        m_transitionDuration = 0.0f;
        m_loopRequested = false;
        m_pendingShardCombinePulseCount = 0;
        m_pendingEyeCombinePulse = false;
        m_enterNextShardThresholdIndex = 0;
        m_enterEyeCombineReached = false;
        m_eyeFloatAnchorValid = false;
        m_bobTime = 0.0f;
        m_eyeBaseRotationValid = false;
        m_spinYawDeg = 0.0f;

        SetEnabled(m_weaponCombined, true);
        SetVisible(m_weaponCombined, true);
        m_currentWeaponAlpha = m_weaponDefaultAlpha;
        SetMaterialAlpha(m_weaponCombined, m_currentWeaponAlpha);

        SetEnabled(m_eye, true);
        m_currentEyeAlpha = m_eyeIdleAlpha;
        SetMaterialAlpha(m_eye, m_currentEyeAlpha);
        SetUnityVfxAlpha(m_playerHealEffect, m_currentEyeAlpha);
        SetVisible(m_eye, m_currentEyeAlpha > 0.001f);

        ShowShards(false);
    }

    void HealEyeGimmick::StartTransition(float weaponFrom, float weaponTo,
                                         float eyeFrom, float eyeTo,
                                         float durationSec)
    {
        m_transitionTimer = 0.0f;
        m_transitionDuration = std::max(m_fadeMinSec, durationSec);
        m_weaponAlphaFrom = weaponFrom;
        m_weaponAlphaTo = weaponTo;
        m_eyeAlphaFrom = eyeFrom;
        m_eyeAlphaTo = eyeTo;
    }

    void HealEyeGimmick::UpdateTransition(float dt)
    {
        if (m_transitionDuration <= 0.0f)
            return;

        m_transitionTimer += dt;
        float t = Clamp(m_transitionTimer / m_transitionDuration, 0.0f, 1.0f);
        float smoothT = SmoothStep(t);
        if (m_phase == Phase::Entering)
        {
            const std::size_t shardCount = m_shards.size();
            while (m_enterNextShardThresholdIndex < shardCount)
            {
                const float threshold = static_cast<float>(m_enterNextShardThresholdIndex + 1)
                    / static_cast<float>(shardCount + 1);
                if (t < threshold)
                    break;
                ++m_pendingShardCombinePulseCount;
                ++m_enterNextShardThresholdIndex;
            }
            if (!m_enterEyeCombineReached && t >= 1.0f)
            {
                m_pendingEyeCombinePulse = true;
                m_enterEyeCombineReached = true;
            }
        }
        m_currentWeaponAlpha = Lerp(m_weaponAlphaFrom, m_weaponAlphaTo, smoothT);
        m_currentEyeAlpha = Lerp(m_eyeAlphaFrom, m_eyeAlphaTo, smoothT);
        SetMaterialAlpha(m_weaponCombined, m_currentWeaponAlpha);
        SetMaterialAlpha(m_eye, m_currentEyeAlpha);
        SetUnityVfxAlpha(m_playerHealEffect, m_currentEyeAlpha);

        if (t >= 1.0f)
        {
            m_transitionDuration = 0.0f;
            m_transitionTimer = 0.0f;
            m_currentWeaponAlpha = m_weaponAlphaTo;
            m_currentEyeAlpha = m_eyeAlphaTo;

            if (m_phase == Phase::Exiting)
            {
                EnterIdle();
            }
            else if (m_phase == Phase::Entering && m_loopRequested)
            {
                m_phase = Phase::Loop;
        m_eyeFloatAnchorValid = false;
        m_bobTime = 0.0f;
        m_eyeBaseRotationValid = false;
        m_spinYawDeg = 0.0f;
            }
        }
    }

    void HealEyeGimmick::UpdateEyeFloat(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId)
            return;

        auto* tr = world->GetComponent<TransformComponent>(m_eye);
        if (!tr)
            return;

        if (!m_eyeFloatAnchorValid)
        {
            m_eyeFloatAnchor = tr->position;
            m_eyeFloatAnchorValid = true;
            m_bobTime = 0.0f;
        }
        if (!m_eyeBaseRotationValid)
        {
            m_eyeBaseRotation = tr->rotation;
            m_eyeBaseRotationValid = true;
            m_spinYawDeg = 0.0f;
        }

        m_bobTime += dt;
        if (m_spinSpeedDeg != 0.0f)
        {
            m_spinYawDeg += m_spinSpeedDeg * dt;
            if (m_spinYawDeg > 360.0f || m_spinYawDeg < -360.0f)
                m_spinYawDeg = std::fmod(m_spinYawDeg, 360.0f);
        }
        DirectX::XMFLOAT3 targetPos = m_eyeFloatAnchor;
        targetPos.y = m_bobBaseY;
        targetPos.y += m_bobHeightOffset;
        targetPos.y += std::sin(m_bobTime * m_bobSpeed) * m_bobAmplitude;

        tr->position = targetPos;
        DirectX::XMFLOAT3 targetRot = m_eyeBaseRotation;
        targetRot.y += m_spinYawDeg;
        tr->rotation = targetRot;
        world->MarkTransformDirty(m_eye);
    }

    float HealEyeGimmick::ResolveFadeDuration(float baseDurationSec, float ratio) const
    {
        float duration = (baseDurationSec > 0.0f) ? baseDurationSec : 0.0f;
        if (duration > 0.0f && ratio > 0.0f)
            duration *= ratio;
        if (duration <= 0.0f)
            duration = m_fadeMinSec;
        return std::max(m_fadeMinSec, duration);
    }

    void HealEyeGimmick::ShowShards(bool visible)
    {
        for (const auto& shard : m_shards)
        {
            if (shard.id == InvalidEntityId)
                continue;
            SetEnabled(shard.id, visible);
            SetVisible(shard.id, visible);
        }
    }

    void HealEyeGimmick::SetEnabled(EntityId id, bool enabled)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;
        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->enabled = enabled;
            world->MarkTransformDirty(id);
        }
    }

    void HealEyeGimmick::SetVisible(EntityId id, bool visible)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;
        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->visible = visible;
        }
    }

    void HealEyeGimmick::SetMaterialAlpha(EntityId id, float alpha)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        MaterialComponent* mat = world->GetComponent<MaterialComponent>(id);
        if (!mat)
        {
            MaterialComponent& newMat = world->AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
            mat = &newMat;
        }

        const float clamped = Clamp(alpha, 0.0f, 1.0f);
        mat->Set_alpha(clamped);
        mat->Set_transparent(clamped < 0.999f);
    }

    void HealEyeGimmick::SetUnityVfxAlpha(EntityId id, float alpha)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        auto* vfx = world->GetComponent<UnityVfxComponent>(id);
        if (!vfx)
            return;

        const float clamped = Clamp(alpha, 0.0f, 1.0f);
        vfx->alphaScale = clamped;
    }

    float HealEyeGimmick::GetMaterialAlpha(EntityId id, float fallback) const
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return fallback;

        if (auto* mat = world->GetComponent<MaterialComponent>(id))
            return mat->alpha;
        return fallback;
    }
}
