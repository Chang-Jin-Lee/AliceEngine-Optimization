#include "C_BossCombatSessionComponent.h"

#include <algorithm>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"

#include "C_BossBrainComponent.h"

namespace Alice
{
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
        }

        if (!isDead && signals.groggyTriggered)
        {
            m_state = Combat::ActionState::Groggy;
            m_stateTimer = 0.0f;
            m_groggyTimer = 0.0f;
            m_hitReactTimer = 0.0f;
            m_groggyDurationSec = sensors.groggyDuration;
            if (m_groggyDurationSec <= 0.0f)
                m_groggyDurationSec = 1.0f;
        }

        if (m_state == Combat::ActionState::Groggy)
        {
            m_groggyTimer += dt;
            if (m_groggyDurationSec > 0.0f && m_groggyTimer >= m_groggyDurationSec)
            {
                m_state = Combat::ActionState::Idle;
                m_stateTimer = 0.0f;
                m_groggyTimer = 0.0f;
            }
        }

        Combat::BossIntent intent{};
        if (!isDead && m_state != Combat::ActionState::Groggy)
        {
            if (brain)
                intent = brain->Think(dt, targetId);
        }

        if (signals.hitThisFrame && !signals.wasAttacking
            && !isDead && m_state != Combat::ActionState::Groggy)
        {
            const float duration = std::max(0.0f, m_hitReactDurationSec);
            if (duration > 0.0f)
                m_hitReactTimer = std::max(m_hitReactTimer, duration);
        }
        m_hitReactTimer = std::max(0.0f, m_hitReactTimer - dt);
        const bool hitReactActive = (m_hitReactTimer > 0.0f);

        Combat::ActionState nextState = m_state;
        if (!isDead && m_state != Combat::ActionState::Groggy)
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
        return out;
    }
}
