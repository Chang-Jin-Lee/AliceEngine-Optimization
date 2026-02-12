#include "BossSoulAoETelegraphScript.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>

#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "../Combat/C_BossBrainComponent.h"
#include <assimp/scene.h>

namespace Alice
{
    REGISTER_SCRIPT(BossSoulAoETelegraphScript);

    namespace
    {
        constexpr float kTimingEpsilon = 1e-4f;

        bool IsAttackClip(const AttackDriverClip& clip, const std::string& soulClipName)
        {
            return clip.enabled
                && clip.type == AttackDriverNotifyType::Attack
                && clip.clipName == soulClipName;
        }

        bool TryParseIndex(const std::string& key, int& outIdx)
        {
            if (key.empty())
                return false;
            for (char c : key)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            outIdx = std::atoi(key.c_str());
            return true;
        }

        float GetClipDurationSecByName(const SkinnedMeshRegistry* registry,
                                       World& world,
                                       EntityId entityId,
                                       const std::string& clipName)
        {
            if (clipName.empty() || !registry || entityId == InvalidEntityId)
                return 0.0f;

            auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                return 0.0f;

            auto mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return 0.0f;

            const auto& names = mesh->sourceModel->GetAnimationNames();
            const auto* scene = mesh->sourceModel->GetScenePtr();
            const size_t clipCount = scene ? scene->mNumAnimations : names.size();

            for (size_t i = 0; i < names.size() && i < clipCount; ++i)
            {
                if (names[i] == clipName)
                    return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
            }

            if (scene)
            {
                for (size_t i = 0; i < scene->mNumAnimations; ++i)
                {
                    const auto* anim = scene->mAnimations[i];
                    if (anim && anim->mName.length > 0 && clipName == anim->mName.C_Str())
                        return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
                }
            }

            int idx = -1;
            if (TryParseIndex(clipName, idx))
            {
                if (idx >= 0 && static_cast<size_t>(idx) < clipCount)
                    return static_cast<float>(mesh->sourceModel->GetClipDurationSec(idx));
            }

            return 0.0f;
        }

        float ResolveClipSpeed(const AdvancedAnimationComponent& anim, const std::string& clip)
        {
            if (clip.empty())
                return 1.0f;
            if (anim.base.clipA == clip)
                return anim.base.speedA;
            if (anim.base.clipB == clip)
                return anim.base.speedB;
            if (anim.upper.clipA == clip)
                return anim.upper.speedA;
            if (anim.upper.clipB == clip)
                return anim.upper.speedB;
            if (anim.additive.clip == clip)
                return anim.additive.speed;
            return 1.0f;
        }
    }

    void BossSoulAoETelegraphScript::Start()
    {
        ResolveBoss(true);
        ResolveAoe(true);
        ResolveHowlDust(true);
        ResolveSoulTiming(true);
        ResetHowlDust();
        m_appliedAlpha = std::clamp(Get_alphaIdle(), 0.0f, 1.0f);
        m_alphaInitialized = true;
        ApplyAlpha(m_appliedAlpha);
    }

    void BossSoulAoETelegraphScript::Update(float deltaTime)
    {
        ResolveBoss(false);
        ResolveAoe(false);
        ResolveHowlDust(false);
        ResolveSoulTiming(false);

        const float idleAlpha = std::clamp(Get_alphaIdle(), 0.0f, 1.0f);
        if (IsBossDeadNow())
        {
            m_appliedAlpha = idleAlpha;
            m_alphaInitialized = true;
            ApplyAlpha(m_appliedAlpha);
            ResetHowlDust();
            return;
        }

        const float peakAlpha = std::clamp(Get_alphaPeak(), 0.0f, 1.0f);
        float targetAlpha = idleAlpha;
        float soulTimeSec = 0.0f;
        const bool inSoul = TryGetCurrentSoulTimeSec(soulTimeSec);
        const bool treatBoostAsActive = Get_treatBoostAsActiveWhenDashClip()
            && (Get_soulClipName().find("Dash_Attack") != std::string::npos)
            && IsBoostPatternActive();

        if (inSoul)
        {
            if (Get_useAttackWindowPlateau() && m_hasTiming)
            {
                float durationSec = ResolveCurrentSoulClipDurationSec();
                if (durationSec <= kTimingEpsilon)
                {
                    durationSec = (std::max)(kTimingEpsilon, Get_soulDurationFallbackSec());
                    durationSec = (std::max)(durationSec, m_slotEndSec + 0.1f);
                }

                const float clampedTime = std::clamp(soulTimeSec, 0.0f, durationSec);
                const float riseEndSec = std::clamp(m_slotStartSec, 0.0f, durationSec);
                const float holdEndSec = std::clamp((std::max)(m_slotEndSec, riseEndSec), 0.0f, durationSec);

                if (clampedTime <= riseEndSec)
                {
                    const float riseDen = (std::max)(kTimingEpsilon, riseEndSec);
                    const float t = std::clamp(clampedTime / riseDen, 0.0f, 1.0f);
                    targetAlpha = idleAlpha + (peakAlpha - idleAlpha) * t;
                }
                else if (clampedTime <= holdEndSec)
                {
                    targetAlpha = peakAlpha;
                }
                else
                {
                    const float fallDen = (std::max)(kTimingEpsilon, durationSec - holdEndSec);
                    const float t = std::clamp((clampedTime - holdEndSec) / fallDen, 0.0f, 1.0f);
                    targetAlpha = peakAlpha + (idleAlpha - peakAlpha) * t;
                }
            }
            else if (Get_holdPeakWhileActive())
            {
                targetAlpha = peakAlpha;
            }
            else
            {
                float durationSec = ResolveCurrentSoulClipDurationSec();
                if (durationSec <= kTimingEpsilon)
                {
                    durationSec = (std::max)(kTimingEpsilon, Get_soulDurationFallbackSec());
                    if (m_hasTiming)
                        durationSec = (std::max)(durationSec, m_slotStartSec + 0.1f);
                }

                float peakSec = Get_soulPeakTimeSec();
                if (Get_useSlotPeakTiming() && m_hasTiming && m_slotStartSec > 0.0f)
                    peakSec = m_slotStartSec;
                if (peakSec < 0.0f)
                    peakSec = durationSec * 0.5f;

                peakSec = std::clamp(peakSec, 0.0f, durationSec);
                const float clampedTime = std::clamp(soulTimeSec, 0.0f, durationSec);

                if (clampedTime <= peakSec)
                {
                    const float upDen = (std::max)(kTimingEpsilon, peakSec);
                    const float t = std::clamp(clampedTime / upDen, 0.0f, 1.0f);
                    targetAlpha = idleAlpha + (peakAlpha - idleAlpha) * t;
                }
                else
                {
                    const float downDen = (std::max)(kTimingEpsilon, durationSec - peakSec);
                    const float t = std::clamp((clampedTime - peakSec) / downDen, 0.0f, 1.0f);
                    targetAlpha = peakAlpha + (idleAlpha - peakAlpha) * t;
                }
            }
        }
        else if (treatBoostAsActive)
        {
            targetAlpha = peakAlpha;
        }

        const float targetClamped = std::clamp(targetAlpha, 0.0f, 1.0f);
        if (!m_alphaInitialized)
        {
            m_appliedAlpha = targetClamped;
            m_alphaInitialized = true;
        }
        else
        {
            const bool rising = (targetClamped > m_appliedAlpha + kTimingEpsilon);
            const bool falling = (targetClamped < m_appliedAlpha - kTimingEpsilon);
            float speed = 0.0f;
            if (rising)
                speed = (std::max)(0.0f, Get_alphaFadeInSpeed());
            else if (falling)
                speed = (std::max)(0.0f, Get_alphaFadeOutSpeed());

            if (speed > kTimingEpsilon)
            {
                const float maxStep = speed * (std::max)(0.0f, deltaTime);
                if (rising)
                    m_appliedAlpha = (std::min)(targetClamped, m_appliedAlpha + maxStep);
                else if (falling)
                    m_appliedAlpha = (std::max)(targetClamped, m_appliedAlpha - maxStep);
                else
                    m_appliedAlpha = targetClamped;
            }
            else
            {
                m_appliedAlpha = targetClamped;
            }
        }

        ApplyAlpha(m_appliedAlpha);
        UpdateHowlDust(deltaTime);
    }

    void BossSoulAoETelegraphScript::OnDisable()
    {
        m_appliedAlpha = std::clamp(Get_alphaIdle(), 0.0f, 1.0f);
        m_alphaInitialized = false;
        ApplyAlpha(m_appliedAlpha);
        ResetHowlDust();
    }

    void BossSoulAoETelegraphScript::ResolveBoss(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_bossId = InvalidEntityId;
            return;
        }

        const std::string bossName = Get_bossEntityName();
        if (bossName.empty())
        {
            m_bossId = InvalidEntityId;
            return;
        }

        const GameObject bossGo = world->FindGameObject(bossName);
        if (!bossGo.IsValid())
        {
            m_bossId = InvalidEntityId;
            if (logWarnings && !m_warnedMissingBoss)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Missing boss entity: '%s'", bossName.c_str());
                m_warnedMissingBoss = true;
            }
            return;
        }

        m_bossId = bossGo.id();
    }

    void BossSoulAoETelegraphScript::ResolveAoe(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_aoeId = InvalidEntityId;
            return;
        }

        const std::string aoeName = Get_aoeEntityName();
        if (aoeName.empty())
        {
            m_aoeId = InvalidEntityId;
            return;
        }

        EntityId resolved = InvalidEntityId;
        if (m_bossId != InvalidEntityId)
        {
            const auto children = world->GetChildren(m_bossId);
            for (EntityId child : children)
            {
                if (world->GetEntityName(child) == aoeName)
                {
                    resolved = child;
                    break;
                }
            }
        }

        if (resolved == InvalidEntityId)
        {
            const GameObject aoeGo = world->FindGameObject(aoeName);
            if (aoeGo.IsValid())
                resolved = aoeGo.id();
        }

        if (resolved == InvalidEntityId)
        {
            m_aoeId = InvalidEntityId;
            if (logWarnings && !m_warnedMissingAoe)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Missing AoE entity: '%s'", aoeName.c_str());
                m_warnedMissingAoe = true;
            }
            return;
        }

        m_aoeId = resolved;
        if (!world->GetComponent<UnityVfxComponent>(m_aoeId))
        {
            if (logWarnings && !m_warnedMissingAoeVfx)
            {
                ALICE_LOG_WARN("[BossSoulAoE] AoE entity has no UnityVfxComponent: '%s'", aoeName.c_str());
                m_warnedMissingAoeVfx = true;
            }
            m_aoeId = InvalidEntityId;
        }
    }

    void BossSoulAoETelegraphScript::ResolveHowlDust(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_howlDustVfxId = InvalidEntityId;
            return;
        }

        const std::string dustName = Get_howlDustEntityName();
        if (dustName.empty())
        {
            m_howlDustVfxId = InvalidEntityId;
            return;
        }

        EntityId resolved = InvalidEntityId;

        // Prefer exact name under Boss children.
        if (m_bossId != InvalidEntityId)
        {
            const auto children = world->GetChildren(m_bossId);
            for (EntityId child : children)
            {
                if (world->GetEntityName(child) == dustName)
                {
                    resolved = child;
                    break;
                }
            }
        }

        if (resolved == InvalidEntityId)
        {
            const GameObject dustGo = world->FindGameObject(dustName);
            if (dustGo.IsValid())
                resolved = dustGo.id();
        }

        if (resolved == InvalidEntityId)
        {
            m_howlDustVfxId = InvalidEntityId;
            if (logWarnings && !m_warnedMissingDustEntity)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Missing howl dust entity: '%s'", dustName.c_str());
                m_warnedMissingDustEntity = true;
            }
            return;
        }

        m_howlDustVfxId = resolved;

        if (!world->GetComponent<UnityVfxComponent>(m_howlDustVfxId))
        {
            if (logWarnings && !m_warnedMissingDustVfx)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Howl dust entity has no UnityVfxComponent: '%s'", dustName.c_str());
                m_warnedMissingDustVfx = true;
            }
            m_howlDustVfxId = InvalidEntityId;
            return;
        }
    }

    void BossSoulAoETelegraphScript::ResolveSoulTiming(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
        {
            m_hasTiming = false;
            m_slotEndSec = 0.0f;
            return;
        }

        auto* driver = world->GetComponent<AttackDriverComponent>(m_bossId);
        if (!driver)
        {
            m_hasTiming = false;
            m_slotEndSec = 0.0f;
            if (logWarnings && !m_warnedMissingDriver)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Boss has no AttackDriverComponent.");
                m_warnedMissingDriver = true;
            }
            return;
        }

        const int slotIndex = Get_telegraphSlotIndex();
        if (slotIndex < 1 || slotIndex > 32)
        {
            m_hasTiming = false;
            m_slotEndSec = 0.0f;
            if (logWarnings && !m_warnedBadSlotIndex)
            {
                ALICE_LOG_WARN("[BossSoulAoE] telegraphSlotIndex out of range: %d (valid 1~32)", slotIndex);
                m_warnedBadSlotIndex = true;
            }
            return;
        }

        const std::uint32_t slotBit = (1u << (slotIndex - 1));
        const std::string soulClip = Get_soulClipName();
        float soulStart = (std::numeric_limits<float>::max)();
        float slotStart = (std::numeric_limits<float>::max)();
        float slotEnd = -1.0f;
        bool foundSoul = false;
        bool foundSlotStart = false;

        for (const auto& clip : driver->clips)
        {
            if (!IsAttackClip(clip, soulClip))
                continue;

            foundSoul = true;
            soulStart = (std::min)(soulStart, clip.startTimeSec);
        }

        if (foundSoul)
        {
            for (const auto& clip : driver->clips)
            {
                if (!IsAttackClip(clip, soulClip))
                    continue;

                const bool slotMatch = (clip.traceSlotMask == 0u) || ((clip.traceSlotMask & slotBit) != 0u);
                if (!slotMatch)
                    continue;
                if (clip.startTimeSec + kTimingEpsilon < soulStart)
                    continue;

                foundSlotStart = true;
                slotStart = (std::min)(slotStart, clip.startTimeSec);
                slotEnd = (std::max)(slotEnd, (std::max)(clip.endTimeSec, clip.startTimeSec));
            }
        }

        if (!foundSoul || !foundSlotStart)
        {
            m_hasTiming = false;
            m_slotEndSec = 0.0f;
            if (logWarnings && !m_warnedMissingTiming)
            {
                ALICE_LOG_WARN("[BossSoulAoE] Missing Soul_Attack timing in AttackDriver. clip='%s', slot=%d",
                               soulClip.c_str(),
                               slotIndex);
                m_warnedMissingTiming = true;
            }
            return;
        }

        m_soulStartSec = soulStart;
        m_slotStartSec = slotStart;
        m_slotEndSec = (slotEnd >= slotStart) ? slotEnd : slotStart;
        m_hasTiming = true;

        if (Get_debugLogs())
        {
            ALICE_LOG_INFO("[BossSoulAoE] Timing resolved. soulStart=%.3f slotStart=%.3f slotEnd=%.3f slot=%d",
                           m_soulStartSec,
                           m_slotStartSec,
                           m_slotEndSec,
                           slotIndex);
        }
    }

    void BossSoulAoETelegraphScript::UpdateHowlDust(float deltaTime)
    {
        (void)deltaTime;
        World* world = GetWorld();
        if (!world || m_howlDustVfxId == InvalidEntityId)
            return;

        float howlingTimeSec = 0.0f;
        const bool isHowling = TryGetCurrentClipTimeSec(Get_howlClipName(), howlingTimeSec);
        m_howlDustAlpha = 0.0f;

        if (isHowling)
        {
            float durationSec = 0.0f;
            if (auto* registry = SkinnedRegistry())
            {
                durationSec = GetClipDurationSecByName(registry, *world, m_bossId, Get_howlClipName());
            }

            float speed = 1.0f;
            if (auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_bossId))
            {
                speed = ResolveClipSpeed(*anim, Get_howlClipName());
            }
            else if (auto* skinnedAnim = world->GetComponent<SkinnedAnimationComponent>(m_bossId))
            {
                speed = skinnedAnim->speed;
            }

            const float speedAbs = std::abs(speed);
            if (durationSec > 0.0f && speedAbs > kTimingEpsilon)
                durationSec /= speedAbs;
            if (durationSec <= kTimingEpsilon)
                durationSec = (std::max)(kTimingEpsilon, Get_soulDurationFallbackSec());

            const float t = std::clamp(howlingTimeSec / durationSec, 0.0f, 1.0f);
            const float tri = (t <= 0.5f)
                ? (t / 0.5f)
                : ((1.0f - t) / 0.5f);
            const float peak = (std::max)(0.0f, Get_howlDustAlphaPeak());
            m_howlDustAlpha = std::clamp(tri, 0.0f, 1.0f) * peak;
        }

        const bool active = (m_howlDustAlpha > 0.001f);

        if (auto* tr = world->GetComponent<TransformComponent>(m_howlDustVfxId))
        {
            const bool changed = (tr->enabled != active) || (tr->visible != active);
            tr->enabled = active;
            tr->visible = active;
            if (changed)
                world->MarkTransformDirty(m_howlDustVfxId);
        }

        if (auto* vfx = world->GetComponent<UnityVfxComponent>(m_howlDustVfxId))
        {
            vfx->enabled = active;
            vfx->emitNewParticles = active;
            vfx->alphaScale = m_howlDustAlpha;
        }

        if (auto* compute = world->GetComponent<ComputeEffectComponent>(m_howlDustVfxId))
        {
            compute->enabled = false;
            compute->spawnRate = 0.0f;
        }
    }

    void BossSoulAoETelegraphScript::ResetHowlDust()
    {
        m_howlDustAlpha = 0.0f;

        World* world = GetWorld();
        if (!world || m_howlDustVfxId == InvalidEntityId)
            return;

        if (auto* tr = world->GetComponent<TransformComponent>(m_howlDustVfxId))
        {
            tr->enabled = false;
            tr->visible = false;
            world->MarkTransformDirty(m_howlDustVfxId);
        }

        if (auto* vfx = world->GetComponent<UnityVfxComponent>(m_howlDustVfxId))
        {
            vfx->enabled = false;
            vfx->emitNewParticles = false;
            vfx->alphaScale = 0.0f;
        }

        if (auto* compute = world->GetComponent<ComputeEffectComponent>(m_howlDustVfxId))
        {
            compute->enabled = false;
            compute->spawnRate = 0.0f;
        }
    }

    float BossSoulAoETelegraphScript::ResolveCurrentSoulClipDurationSec() const
    {
        World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
            return 0.0f;

        const std::string clip = Get_soulClipName();
        if (clip.empty())
            return 0.0f;

        float durationSec = 0.0f;
        if (auto* registry = SkinnedRegistry())
        {
            durationSec = GetClipDurationSecByName(registry, *world, m_bossId, clip);
        }

        float speed = 1.0f;
        if (auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_bossId))
        {
            speed = ResolveClipSpeed(*anim, clip);
        }
        else if (auto* skinnedAnim = world->GetComponent<SkinnedAnimationComponent>(m_bossId))
        {
            speed = skinnedAnim->speed;
        }

        const float speedAbs = std::abs(speed);
        if (durationSec > 0.0f && speedAbs > kTimingEpsilon)
            durationSec /= speedAbs;

        return (durationSec > 0.0f) ? durationSec : 0.0f;
    }

    bool BossSoulAoETelegraphScript::IsBossDeadNow() const
    {
        const World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
            return false;

        const auto* health = world->GetComponent<HealthComponent>(m_bossId);
        if (!health)
            return false;

        return (!health->alive || health->currentHealth <= 0.0f);
    }

    bool BossSoulAoETelegraphScript::IsBoostPatternActive() const
    {
        World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
            return false;

        auto* scripts = world->GetScripts(m_bossId);
        if (!scripts)
            return false;

        for (const auto& sc : *scripts)
        {
            if (sc.scriptName != "C_BossBrainComponent" || !sc.instance)
                continue;

            const auto* brain = static_cast<const C_BossBrainComponent*>(sc.instance.get());
            if (!brain)
                continue;

            const C_BossBrainComponent::PatternType pattern = brain->GetActivePattern();
            return pattern == C_BossBrainComponent::PatternType::BoostAttackA
                || pattern == C_BossBrainComponent::PatternType::BoostAttackB
                || pattern == C_BossBrainComponent::PatternType::BoostAttackC;
        }

        return false;
    }

    bool BossSoulAoETelegraphScript::TryGetCurrentClipTimeSec(const std::string& clipName, float& outTimeSec) const
    {
        outTimeSec = 0.0f;

        const World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId || clipName.empty())
            return false;

        const auto* anim = world->GetComponent<AdvancedAnimationComponent>(m_bossId);
        if (!anim || !anim->playing)
            return false;

        if (anim->base.clipA == clipName)
        {
            outTimeSec = anim->base.timeA;
            return true;
        }
        if (anim->base.clipB == clipName)
        {
            outTimeSec = anim->base.timeB;
            return true;
        }
        if (anim->upper.enabled)
        {
            if (anim->upper.clipA == clipName)
            {
                outTimeSec = anim->upper.timeA;
                return true;
            }
            if (anim->upper.clipB == clipName)
            {
                outTimeSec = anim->upper.timeB;
                return true;
            }
        }
        if (anim->additive.enabled && anim->additive.clip == clipName)
        {
            outTimeSec = anim->additive.time;
            return true;
        }

        return false;
    }

    bool BossSoulAoETelegraphScript::TryGetCurrentSoulTimeSec(float& outTimeSec) const
    {
        return TryGetCurrentClipTimeSec(Get_soulClipName(), outTimeSec);
    }

    bool BossSoulAoETelegraphScript::IsTelegraphSlotActive() const
    {
        const World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
            return false;

        const auto* driver = world->GetComponent<AttackDriverComponent>(m_bossId);
        if (!driver)
            return false;

        const int slotIndex = Get_telegraphSlotIndex();
        if (slotIndex < 1 || slotIndex > 32)
            return false;

        const std::uint32_t bit = (1u << (slotIndex - 1));
        return (driver->attackTraceMaskActive & bit) != 0u;
    }

    void BossSoulAoETelegraphScript::ApplyAlpha(float alpha)
    {
        World* world = GetWorld();
        if (!world)
            return;

        EntityId target = m_aoeId;
        if (target == InvalidEntityId)
            target = GetOwnerId();
        if (target == InvalidEntityId)
            return;

        auto* vfx = world->GetComponent<UnityVfxComponent>(target);
        if (!vfx)
            return;

        const float clamped = std::clamp(alpha, 0.0f, 1.0f);
        const bool active = (clamped > 0.001f);
        vfx->alphaScale = clamped;
        vfx->enabled = active;
        vfx->emitNewParticles = active;

        if (auto* tr = world->GetComponent<TransformComponent>(target))
        {
            const bool changed = (tr->enabled != active) || (tr->visible != active);
            tr->enabled = active;
            tr->visible = active;
            if (changed)
                world->MarkTransformDirty(target);
        }

        if (!active)
        {
            if (auto* compute = world->GetComponent<ComputeEffectComponent>(target))
            {
                compute->enabled = false;
                compute->spawnRate = 0.0f;
            }
        }
    }
}
