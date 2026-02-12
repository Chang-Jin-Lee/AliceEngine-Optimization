#include "C_BossCombatSessionComponent.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <cstdlib>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxModel.h"
#include <assimp/scene.h>

#include "C_BossBrainComponent.h"

namespace Alice
{
    namespace
    {
        static bool TryParseIndex(const std::string& key, int& outIdx)
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

        static float GetClipDurationSecByName(const SkinnedMeshRegistry* registry,
                                              World& world,
                                              EntityId entityId,
                                              const std::string& clipName)
        {
            if (clipName.empty() || !registry)
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
    }

    REGISTER_SCRIPT(C_BossCombatSessionComponent);

    void C_BossCombatSessionComponent::Start()
    {
        Reset();
    }

    void C_BossCombatSessionComponent::Update(float deltaTime)
    {
        // Boss combat is driven externally by C_CombatSessionComponent via Tick().
        (void)deltaTime;
    }

    void C_BossCombatSessionComponent::OnDisable()
    {
        Reset();
    }

    void C_BossCombatSessionComponent::Reset()
    {
        m_state = Combat::ActionState::Idle;
        m_stateTimer = 0.0f;
        m_hitReactTimer = 0.0f;
        m_groggyTimer = 0.0f;
        m_groggyDurationSec = 0.0f;
        m_groggyRecovering = false;
        m_groggyRecoverTimer = 0.0f;
        m_groggyRecoverDurationSec = 0.0f;
        m_postGroggyIdleHoldTimer = 0.0f;
        m_prevGroggyHold = false;
    }

    Combat::BossOutput C_BossCombatSessionComponent::Tick(World& world,
                                                          float deltaTime,
                                                          EntityId bossId,
                                                          EntityId targetId,
                                                          C_BossBrainComponent* brain,
                                                          const Combat::Sensors& sensors,
                                                          bool hitstopActive,
                                                          const Combat::BossSignals& signals)
    {
        Combat::BossOutput out{};
        const float dt = std::max(0.0f, deltaTime);
        const bool groggyHold = signals.groggyHold;
        if (m_postGroggyIdleHoldTimer > 0.0f)
            m_postGroggyIdleHoldTimer = std::max(0.0f, m_postGroggyIdleHoldTimer - dt);
        const float groggyRecoverIdleHoldSec = std::max(0.0f, m_groggyRecoverIdleHoldSec);
        auto EnterIdleFromGroggy = [&]()
        {
            m_state = Combat::ActionState::Idle;
            m_stateTimer = 0.0f;
            m_groggyTimer = 0.0f;
            m_groggyDurationSec = 0.0f;
            m_groggyRecovering = false;
            m_groggyRecoverTimer = 0.0f;
            m_groggyRecoverDurationSec = 0.0f;
            m_postGroggyIdleHoldTimer = groggyRecoverIdleHoldSec;
        };

        bool isDead = signals.dead;
        if (!isDead)
        {
            if (auto* hc = world.GetComponent<HealthComponent>(bossId))
                isDead = (hc->currentHealth <= 0.0f);
        }

        if (isDead)
        {
            m_state = Combat::ActionState::Dead;
            m_stateTimer = 0.0f;
            m_hitReactTimer = 0.0f;
            m_groggyTimer = 0.0f;
            m_groggyRecovering = false;
            m_groggyRecoverTimer = 0.0f;
            m_groggyRecoverDurationSec = 0.0f;
            m_postGroggyIdleHoldTimer = 0.0f;
            m_prevGroggyHold = false;
        }

        if (!isDead && signals.groggyTriggered)
        {
            m_state = Combat::ActionState::Groggy;
            m_stateTimer = 0.0f;
            m_groggyTimer = 0.0f;
            m_hitReactTimer = 0.0f;
            m_groggyRecovering = false;
            m_groggyRecoverTimer = 0.0f;
            m_groggyRecoverDurationSec = 0.0f;
            m_postGroggyIdleHoldTimer = 0.0f;
            m_groggyDurationSec = sensors.groggyDuration;
            if (m_groggyDurationSec <= 0.0f)
                m_groggyDurationSec = 1.0f;
        }

        if (!isDead
            && m_state == Combat::ActionState::Groggy
            && m_prevGroggyHold
            && !groggyHold)
        {
            // Groggy attack sequence finished: play recover clip before returning to idle.
            float recoverDuration = 0.0f;
            if (brain)
            {
                recoverDuration = GetClipDurationSecByName(
                    SkinnedRegistry(), world, bossId, brain->GetGroggyRecoverClip());
            }
            if (recoverDuration > 0.0f)
            {
                m_groggyTimer = 0.0f;
                m_groggyDurationSec = 0.0f;
                m_groggyRecovering = true;
                m_groggyRecoverTimer = 0.0f;
                m_groggyRecoverDurationSec = recoverDuration;
            }
            else
            {
                EnterIdleFromGroggy();
            }
        }

        if (m_state == Combat::ActionState::Groggy)
        {
            if (!isDead && signals.groggyExtendSec > 0.0f)
            {
                const float minDuration = m_groggyTimer + signals.groggyExtendSec;
                m_groggyDurationSec = std::max(m_groggyDurationSec, minDuration);
            }

            if (groggyHold)
            {
                // Keep boss in groggy while fatal attack is in progress.
                m_groggyRecovering = false;
                m_groggyRecoverTimer = 0.0f;
                m_groggyRecoverDurationSec = 0.0f;
            }
            else if (m_groggyRecovering)
            {
                m_groggyRecoverTimer += dt;
                if (m_groggyRecoverDurationSec <= 0.0f
                    || m_groggyRecoverTimer >= m_groggyRecoverDurationSec)
                {
                    EnterIdleFromGroggy();
                }
            }
            else
            {
                m_groggyTimer += dt;
                if (m_groggyDurationSec > 0.0f && m_groggyTimer >= m_groggyDurationSec)
                {
                    float recoverDuration = 0.0f;
                    if (brain)
                        recoverDuration = GetClipDurationSecByName(
                            SkinnedRegistry(), world, bossId, brain->GetGroggyRecoverClip());
                    if (recoverDuration > 0.0f)
                    {
                        m_groggyRecovering = true;
                        m_groggyRecoverTimer = 0.0f;
                        m_groggyRecoverDurationSec = recoverDuration;
                    }
                    else
                    {
                        EnterIdleFromGroggy();
                    }
                }
            }
        }

        const bool postGroggyIdleHoldActive = (m_postGroggyIdleHoldTimer > 0.0f);

        Combat::BossIntent intent{};
        if (!isDead && m_state != Combat::ActionState::Groggy && !postGroggyIdleHoldActive)
        {
            if (brain)
                intent = brain->Think(dt, targetId);
        }
        const bool phaseHowlingActive = brain
            && (brain->GetActivePattern() == C_BossBrainComponent::PatternType::Special);

        if (signals.hitThisFrame && !signals.wasAttacking
            && !isDead && m_state != Combat::ActionState::Groggy
            && !phaseHowlingActive)
        {
            float duration = std::max(0.0f, m_hitReactDurationSec);
            if (m_hitReactUseFullClipDuration)
            {
                std::string hitClipName;
                if (brain)
                    hitClipName = brain->GetHitClip();
                const float clipDuration = GetClipDurationSecByName(SkinnedRegistry(), world, bossId, hitClipName);
                if (clipDuration > 0.0f)
                    duration = clipDuration;
            }
            if (duration > 0.0f)
                m_hitReactTimer = std::max(m_hitReactTimer, duration);
        }
        m_hitReactTimer = std::max(0.0f, m_hitReactTimer - dt);
        const bool hitReactActive = (m_hitReactTimer > 0.0f);

        Combat::ActionState nextState = m_state;
        if (!isDead && m_state != Combat::ActionState::Groggy && !postGroggyIdleHoldActive)
        {
            if (brain)
            {
                switch (brain->GetBrainState())
                {
                case C_BossBrainComponent::BrainState::Attack:
                    nextState = Combat::ActionState::Attack;
                    break;
                case C_BossBrainComponent::BrainState::Idle:
                    nextState = Combat::ActionState::Idle;
                    break;
                case C_BossBrainComponent::BrainState::Orbit:
                case C_BossBrainComponent::BrainState::Approach:
                case C_BossBrainComponent::BrainState::Retreat:
                case C_BossBrainComponent::BrainState::Chase:
                    nextState = Combat::ActionState::Move;
                    break;
                case C_BossBrainComponent::BrainState::Gimmick:
                    nextState = phaseHowlingActive
                        ? Combat::ActionState::Attack
                        : Combat::ActionState::Idle;
                    break;
                default:
                    nextState = Combat::ActionState::Idle;
                    break;
                }
            }
            else
            {
                const float moveMag = std::abs(intent.move.x) + std::abs(intent.move.y);
                if (intent.attackRequested)
                    nextState = Combat::ActionState::Attack;
                else if (moveMag > 0.001f)
                    nextState = Combat::ActionState::Move;
                else
                    nextState = Combat::ActionState::Idle;
            }
        }
        else if (postGroggyIdleHoldActive && !isDead && m_state != Combat::ActionState::Groggy)
        {
            nextState = Combat::ActionState::Idle;
        }

        if (nextState != m_state)
        {
            m_state = nextState;
            m_stateTimer = 0.0f;
        }
        else
        {
            m_stateTimer += dt;
        }

        std::string attackClip;
        if (m_state == Combat::ActionState::Attack && brain)
        {
            const auto pattern = brain->GetActivePattern();
            attackClip = brain->GetPatternClip(pattern);
        }

        Combat::ActionFlags flags{};
        flags.hitActive = (m_state == Combat::ActionState::Attack) && sensors.attackWindowActive;
        if (phaseHowlingActive)
            flags.hitActive = false;
        flags.guardActive = false;
        flags.parryWindowActive = false;
        flags.invulnActive = sensors.invulnActive;
        flags.canBeInterrupted = false;
        flags.chargeActive = intent.chargeActive;
        flags.chargeLevel = intent.chargeLevel;
        flags.attackComboIndex = 0;

        out.state = m_state;
        out.flags = flags;
        out.intent = intent;
        out.attackClip = attackClip;
        out.wantsFaceTarget = brain ? brain->WantsFaceTarget() : intent.wantsFaceTarget;
        out.hitstopActive = hitstopActive;
        out.hitReactActive = hitReactActive;
        out.groggyRecoverActive = (m_state == Combat::ActionState::Groggy)
            && m_groggyRecovering
            && !groggyHold;
        m_prevGroggyHold = groggyHold;
        return out;
    }
}
