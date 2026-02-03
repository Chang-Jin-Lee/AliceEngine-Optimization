#include "C_BossBrainComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/IPhysicsWorld.h"

namespace Alice
{
    REGISTER_SCRIPT(C_BossBrainComponent);

    namespace
    {
        bool Normalize2D(float x, float z, Combat::Vec2& out)
        {
            const float len = std::sqrt(x * x + z * z);
            if (len <= 0.0001f)
            {
                out = {};
                return false;
            }
            out.x = x / len;
            out.y = z / len;
            return true;
        }

        float RandomRange(float minVal, float maxVal)
        {
            if (maxVal <= minVal)
                return minVal;
            static std::mt19937 rng{ std::random_device{}() };
            std::uniform_real_distribution<float> dist(minVal, maxVal);
            return dist(rng);
        }
    }

    void C_BossBrainComponent::Start()
    {
        ResetBrain();
    }

    void C_BossBrainComponent::Update(float deltaTime)
    {
        auto* world = GetWorld();
        if (world && world->IsScriptCombatEnabled())
            return;

        m_attackCooldownTimer = std::max(0.0f, m_attackCooldownTimer - deltaTime);
        m_stateTimer += std::max(0.0f, deltaTime);
    }

    void C_BossBrainComponent::OnDisable()
    {
        ResetBrain();
    }

    Combat::Intent C_BossBrainComponent::Think(float deltaTime, EntityId targetId)
    {
        Combat::Intent intent{};
        World* world = GetWorld();
        if (!world)
            return intent;

        const EntityId selfId = GetOwnerId();
        if (selfId == InvalidEntityId || targetId == InvalidEntityId)
            return intent;

        auto* selfTr = world->GetComponent<TransformComponent>(selfId);
        auto* targetTr = world->GetComponent<TransformComponent>(targetId);
        if (!selfTr || !targetTr)
            return intent;

        const float dt = std::max(0.0f, deltaTime);
        m_stateTimer += dt;
        m_attackCooldownTimer = std::max(0.0f, m_attackCooldownTimer - dt);
        m_blockedCooldownTimer = std::max(0.0f, m_blockedCooldownTimer - dt);

        const float dx = targetTr->position.x - selfTr->position.x;
        const float dz = targetTr->position.z - selfTr->position.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float closeRange = std::max(0.0f, m_kickRange);
        const float meleeRange = std::max(closeRange, m_meleeDistance);
        const bool inMelee = dist <= meleeRange;

        Combat::Vec2 toTarget{};
        const bool hasDir = Normalize2D(dx, dz, toTarget);
        Combat::Vec2 right{};
        if (hasDir)
            right = { toTarget.y, -toTarget.x };

        bool targetInFront = true;
        if (hasDir)
        {
            const float yaw = selfTr->rotation.y;
            const float fx = std::sin(yaw);
            const float fz = std::cos(yaw);
            const float dot = fx * toTarget.x + fz * toTarget.y;
            targetInFront = (dot >= 0.0f);
        }

        bool blocked = false;
        if (auto* cct = world->GetComponent<Phy_CCTComponent>(selfId))
        {
            const uint8_t flags = cct->collisionFlags;
            blocked = (flags & static_cast<uint8_t>(CCTCollisionFlags::Sides)) != 0u;
        }

        float hpRatio = 1.0f;
        if (auto* hc = world->GetComponent<HealthComponent>(selfId))
        {
            const float maxHp = std::max(0.0001f, hc->maxHealth);
            hpRatio = hc->currentHealth / maxHp;
        }

        auto EnterState = [&](BrainState next)
            {
                if (m_state != next)
                {
                    m_state = next;
                    m_stateTimer = 0.0f;
                }
            };

        auto BeginAttack = [&](PatternType type)
            {
                m_activePattern = type;
                m_attackIssued = false;
                m_chargeTimer = (type == PatternType::Charge) ? std::max(0.0f, m_chargeHoldSec) : 0.0f;
                EnterState(BrainState::Attack);
            };

        if (m_stationaryAttackBoss)
        {
            const float holdSec = std::max(0.0f, m_attackStateHoldSec);
            if (m_state != BrainState::Idle && m_state != BrainState::Attack)
                EnterState(BrainState::Idle);

            if (m_state == BrainState::Attack)
            {
                if (!m_attackIssued)
                {
                    intent.lightAttackPressed = true;
                    intent.attackPressed = true;
                    m_attackIssued = true;
                }

                if (m_attackIssued && m_stateTimer >= holdSec)
                {
                    m_attackIssued = false;
                    m_attackCooldownTimer = std::max(0.0f, m_attackCooldown);
                    EnterState(BrainState::Idle);
                }
            }
            else if (m_state == BrainState::Idle)
            {
                if (m_attackCooldownTimer <= 0.0f)
                    EnterState(BrainState::Attack);
            }

            m_wantsFaceTarget = true;
            m_debugLabel = std::string("StationaryAttack | ") + GetStateLabel(m_state);
            return intent;
        }

        if (m_testSwingLoop)
        {
            const float holdSec = std::max(0.0f, m_attackStateHoldSec);
            const float approachDist = std::max(0.1f, m_testApproachDistance);
            const float retreatDist = std::max(approachDist + 0.1f, m_testRetreatDistance);
            const float retreatMax = std::max(0.0f, m_testRetreatMaxSec);
            const float traceMin = std::max(0.0f, m_testTraceMinSec);
            const float traceMax = std::max(traceMin, m_testTraceMaxSec);
            const float traceFallback = std::max(0.0f, m_testTraceDurationSec);

            if (m_state == BrainState::Orbit && blocked && m_blockedCooldownTimer <= 0.0f)
            {
                m_patrolDirection = (m_patrolDirection >= 0) ? -1 : 1;
                m_blockedCooldownTimer = std::max(0.0f, m_patrolBlockedCooldownSec);
            }

            if (m_state == BrainState::Attack)
            {
                if (!m_attackIssued)
                {
                    intent.lightAttackPressed = true;
                    intent.attackPressed = true;
                    m_attackIssued = true;
                }

                if (m_attackIssued && m_stateTimer >= holdSec)
                {
                    m_activePattern = PatternType::None;
                    m_attackIssued = false;
                    m_attackCooldownTimer = 0.0f;
                    m_testTraceTimer = 0.0f;
                    m_testTraceTargetSec = 0.0f;
                    m_testRetreatTimer = 0.0f;
                    EnterState(BrainState::Retreat);
                }
            }
            else if (m_state == BrainState::Approach)
            {
                if (dist <= approachDist)
                {
                    BeginAttack(PatternType::AttackA);
                }
                else if (hasDir)
                {
                    intent.move = { toTarget.x, toTarget.y };
                }
            }
            else if (m_state == BrainState::Retreat)
            {
                if (hasDir)
                    intent.move = { -toTarget.x, -toTarget.y };
                m_testRetreatTimer += dt;
                if (dist >= retreatDist || (retreatMax > 0.0f && m_testRetreatTimer >= retreatMax))
                {
                    m_testRetreatTimer = 0.0f;
                    m_testTraceTimer = 0.0f;
                    m_testTraceTargetSec = 0.0f;
                    EnterState(BrainState::Orbit);
                }
            }
            else
            {
                EnterState(BrainState::Orbit);
                if (hasDir)
                    intent.move = { right.x * static_cast<float>(m_patrolDirection), right.y * static_cast<float>(m_patrolDirection) };
                m_testTraceTimer += dt;
                if (m_testTraceTargetSec <= 0.0f)
                {
                    m_testTraceTargetSec = (traceMax > 0.0f) ? RandomRange(traceMin, traceMax) : traceFallback;
                }
                if (m_testTraceTimer >= std::max(0.0f, m_testTraceTargetSec))
                {
                    m_testTraceTimer = 0.0f;
                    m_testTraceTargetSec = 0.0f;
                    EnterState(BrainState::Approach);
                }
            }

            m_wantsFaceTarget = true;
            std::string label = "TestSwing | ";
            label += GetStateLabel(m_state);
            if (m_state == BrainState::Orbit)
                label += (m_patrolDirection >= 0) ? " (Right)" : " (Left)";
            m_debugLabel = label;
            return intent;
        }

        if (m_enableGimmickPhase && !m_gimmickActive && hpRatio <= m_gimmickHpRatio)
        {
            m_gimmickActive = true;
            m_specialPending = true;
        }

        if (m_specialIntervalSec > 0.0f)
        {
            m_specialTimer += dt;
            if (m_specialTimer >= m_specialIntervalSec)
            {
                m_specialTimer -= m_specialIntervalSec;
                m_specialPending = true;
            }
        }

        const float patrolDist = std::max(0.0f, m_patrolDistance);
        const float patrolTol = std::max(0.0f, m_patrolTolerance);
        const float patrolMin = std::max(0.0f, patrolDist - patrolTol);
        const float patrolMax = patrolDist + patrolTol;
        const float chaseDist = std::max(patrolMax, m_chaseDistance);

        const bool stuckRisk = (m_stuckTimeoutSec > 0.0f)
            && (m_stuckTimer >= std::max(0.1f, m_stuckTimeoutSec * 0.5f));

        const int queueTarget = std::max(0, m_patternQueueTarget);
        int queueMax = std::max(queueTarget, m_patternQueueMax);
        if (queueMax < 1)
            queueMax = 1;

        auto LastQueued = [&]() -> PatternType
            {
                return m_patternQueue.empty() ? m_activePattern : m_patternQueue.back();
            };

        auto PushPattern = [&](PatternType type, bool allowDuplicate) -> bool
            {
                if (type == PatternType::None)
                    return false;
                if (!allowDuplicate && type == LastQueued())
                    return false;
                m_patternQueue.push_back(type);
                return true;
            };

        if (m_specialPending && static_cast<int>(m_patternQueue.size()) < queueMax)
        {
            if (PushPattern(PatternType::Special, false))
                m_specialPending = false;
        }

        while (static_cast<int>(m_patternQueue.size()) < queueTarget)
        {
            const PatternType avoidA = m_activePattern;
            const PatternType avoidB = m_patternQueue.empty() ? PatternType::None : m_patternQueue.back();
            PatternType next = PickNextPattern(dist, targetInFront, hpRatio, stuckRisk, avoidA, avoidB);
            if (next == PatternType::None)
                break;
            if (!PushPattern(next, false))
            {
                if (!PushPattern(next, true))
                    break;
            }
        }

        if (m_activePattern == PatternType::None && m_attackCooldownTimer <= 0.0f && !m_patternQueue.empty())
        {
            m_activePattern = m_patternQueue.front();
            m_patternQueue.pop_front();
            m_attackIssued = false;
            m_chargeTimer = (m_activePattern == PatternType::Charge) ? std::max(0.0f, m_chargeHoldSec) : 0.0f;
            if (m_activePattern == PatternType::Special)
                EnterState(BrainState::Gimmick);
        }

        if (m_state == BrainState::Orbit && blocked && m_blockedCooldownTimer <= 0.0f)
        {
            m_patrolDirection = (m_patrolDirection >= 0) ? -1 : 1;
            m_blockedCooldownTimer = std::max(0.0f, m_patrolBlockedCooldownSec);
        }

        bool finishedPattern = false;

        if (m_activePattern == PatternType::Special)
        {
            if (m_state != BrainState::Gimmick)
                EnterState(BrainState::Gimmick);
            if (m_stateTimer >= std::max(0.0f, m_specialPatternHoldSec))
                finishedPattern = true;
        }
        else if (m_activePattern != PatternType::None)
        {
            if (m_state == BrainState::Attack)
            {
                if (m_activePattern == PatternType::Charge)
                {
                    if (m_chargeTimer > 0.0f)
                    {
                        m_chargeTimer = std::max(0.0f, m_chargeTimer - dt);
                        intent.chargeActive = true;
                        intent.chargeLevel = std::max(0, m_chargeLevel);
                    }

                    if (m_chargeTimer <= 0.0f && !m_attackIssued)
                    {
                        intent.heavyAttackPressed = true;
                        intent.attackPressed = true;
                        intent.chargeLevel = std::max(0, m_chargeLevel);
                        m_attackIssued = true;
                    }
                }
                else if (!m_attackIssued)
                {
                    switch (m_activePattern)
                    {
                    case PatternType::AttackA:
                    case PatternType::AttackB:
                    case PatternType::AttackC:
                    case PatternType::Dash:
                    case PatternType::Ranged:
                        intent.lightAttackPressed = true;
                        intent.attackPressed = true;
                        break;
                    case PatternType::Kick:
                        intent.heavyAttackPressed = true;
                        intent.attackPressed = true;
                        break;
                    default:
                        break;
                    }
                    m_attackIssued = true;
                }

                const float holdSec = std::max(0.0f, m_attackStateHoldSec);
                const float minAttackTime = (m_activePattern == PatternType::Charge)
                    ? (std::max(0.0f, m_chargeHoldSec) + holdSec)
                    : holdSec;
                if (m_attackIssued && m_stateTimer >= minAttackTime)
                    finishedPattern = true;
            }
            else
            {
                const float minRange = GetPatternMinRange(m_activePattern);
                const float maxRange = GetPatternMaxRange(m_activePattern);
                bool inRange = false;
                if (m_activePattern == PatternType::Ranged)
                    inRange = (dist >= minRange);
                else
                    inRange = (dist >= minRange && dist <= maxRange);

                if (inRange)
                {
                    BeginAttack(m_activePattern);
                }
                else
                {
                    if (dist < minRange - patrolTol)
                    {
                        EnterState(BrainState::Retreat);
                    }
                    else if (dist > maxRange + patrolTol)
                    {
                        EnterState((dist > chaseDist) ? BrainState::Chase : BrainState::Approach);
                    }
                    else
                    {
                        EnterState(BrainState::Approach);
                    }
                }
            }
        }
        else
        {
            if (inMelee)
            {
                EnterState(BrainState::Idle);
            }
            else if (dist > chaseDist)
            {
                EnterState(BrainState::Chase);
            }
            else if (dist < patrolMin)
            {
                EnterState(BrainState::Retreat);
            }
            else
            {
                EnterState(BrainState::Orbit);
            }
        }

        if (finishedPattern)
        {
            m_activePattern = PatternType::None;
            m_attackIssued = false;
            m_chargeTimer = 0.0f;
            m_attackCooldownTimer = std::max(0.0f, m_attackCooldown);
            if (inMelee)
                EnterState(BrainState::Idle);
            else
                EnterState(BrainState::Orbit);
        }

        if (m_state == BrainState::Orbit)
        {
            if (hasDir)
                intent.move = { right.x * static_cast<float>(m_patrolDirection), right.y * static_cast<float>(m_patrolDirection) };
        }
        else if (m_state == BrainState::Idle)
        {
            // No movement in idle.
        }
        else if (m_state == BrainState::Approach || m_state == BrainState::Chase)
        {
            if (hasDir)
                intent.move = { toTarget.x, toTarget.y };
        }
        else if (m_state == BrainState::Retreat)
        {
            if (hasDir)
                intent.move = { -toTarget.x, -toTarget.y };
        }

        intent.runHeld = (m_state == BrainState::Chase);

        const bool tryingToClose = (m_state == BrainState::Approach || m_state == BrainState::Chase);
        if (tryingToClose)
        {
            const float epsilon = 0.05f;
            if (m_lastDistance > 0.0f && dist >= m_lastDistance - epsilon)
                m_stuckTimer += dt;
            else
                m_stuckTimer = 0.0f;

            if (m_stuckTimeoutSec > 0.0f && m_stuckTimer >= m_stuckTimeoutSec)
            {
                // TODO: Boss unreachable fallback (teleport, reset orbit, or force ranged pattern).
                m_stuckTimer = 0.0f;
            }
        }
        else
        {
            m_stuckTimer = 0.0f;
        }
        m_lastDistance = dist;

        m_wantsFaceTarget = (m_state != BrainState::Gimmick);

        std::string label = GetStateLabel(m_state);
        if (m_state == BrainState::Orbit)
            label += (m_patrolDirection >= 0) ? " (Right)" : " (Left)";

        label += " | Active: ";
        label += GetPatternLabel(m_activePattern);

        if (!m_patternQueue.empty())
        {
            label += " | Queue: [";
            for (size_t i = 0; i < m_patternQueue.size(); ++i)
            {
                if (i > 0)
                    label += ", ";
                label += GetPatternLabel(m_patternQueue[i]);
            }
            label += "]";
        }
        else
        {
            label += " | Queue: None";
        }

        if (m_specialPending)
            label += " | SpecialPending";

        m_debugLabel = label;
        return intent;
    }

    C_BossBrainComponent::PatternType C_BossBrainComponent::PickNextPattern(float dist,
                                                                             bool targetInFront,
                                                                             float hpRatio,
                                                                             bool stuckRisk,
                                                                             PatternType avoidA,
                                                                             PatternType avoidB)
    {
        std::array<PatternType, 12> candidates{};
        size_t count = 0;
        auto PushUnique = [&](PatternType type)
            {
                if (type == PatternType::None)
                    return;
                for (size_t i = 0; i < count; ++i)
                {
                    if (candidates[i] == type)
                        return;
                }
                candidates[count++] = type;
            };

        const float closeRange = std::max(0.0f, m_kickRange);
        const float meleeRange = std::max(closeRange, m_meleeDistance);
        const float midRange = std::max(meleeRange, m_dashRangeMax);

        const bool inClose = dist <= closeRange;
        const bool inMelee = dist <= meleeRange;
        const bool inMid = dist <= midRange;
        const bool inFar = dist > midRange;

        if (stuckRisk)
            PushUnique(PatternType::Ranged);

        const PatternType cycle[] = { PatternType::AttackA, PatternType::AttackB, PatternType::AttackC };
        auto PushCycle = [&]()
            {
                for (size_t i = 0; i < std::size(cycle); ++i)
                {
                    const size_t idx = (static_cast<size_t>(m_attackCycleIndex) + i) % std::size(cycle);
                    PushUnique(cycle[idx]);
                }
            };

        if (inClose)
        {
            if (targetInFront)
                PushUnique(PatternType::Kick);
            else
                PushUnique(PatternType::Dash);
            PushCycle();
        }
        else if (inMelee)
        {
            if (!targetInFront)
                PushUnique(PatternType::Dash);
            PushCycle();
        }
        else if (inMid)
        {
            PushUnique(PatternType::Dash);
            if (!targetInFront)
                PushUnique(PatternType::Ranged);
        }
        else if (inFar)
        {
            PushUnique(PatternType::Ranged);
        }

        if (dist <= std::max(0.0f, m_chargeRangeMax) && targetInFront)
            PushUnique(PatternType::Charge);
        if (m_enableGimmickPhase && hpRatio <= m_gimmickHpRatio)
            PushUnique(PatternType::Charge);

        if (count == 0)
            return PatternType::AttackA;

        auto Pick = [&](bool allowDuplicate) -> PatternType
            {
                for (size_t i = 0; i < count; ++i)
                {
                    const PatternType type = candidates[i];
                    if (!allowDuplicate && (type == avoidA || type == avoidB))
                        continue;
                    return type;
                }
                return PatternType::None;
            };

        PatternType chosen = Pick(false);
        if (chosen == PatternType::None)
            chosen = Pick(true);

        switch (chosen)
        {
        case PatternType::AttackA:
            m_attackCycleIndex = 1;
            break;
        case PatternType::AttackB:
            m_attackCycleIndex = 2;
            break;
        case PatternType::AttackC:
            m_attackCycleIndex = 0;
            break;
        default:
            break;
        }

        return chosen;
    }

    bool C_BossBrainComponent::IsPatternInRange(PatternType type, float dist) const
    {
        switch (type)
        {
        case PatternType::Ranged:
            return dist >= std::max(0.0f, m_rangedRangeMin);
        case PatternType::Dash:
        {
            const float minRange = std::max(0.0f, m_dashRangeMin);
            const float maxRange = std::max(minRange, m_dashRangeMax);
            return dist >= minRange && dist <= maxRange;
        }
        default:
            return dist <= GetPatternMaxRange(type);
        }
    }

    float C_BossBrainComponent::GetPatternMinRange(PatternType type) const
    {
        switch (type)
        {
        case PatternType::Dash:
            return std::max(0.0f, m_dashRangeMin);
        case PatternType::Ranged:
            return std::max(0.0f, m_rangedRangeMin);
        default:
            return 0.0f;
        }
    }

    float C_BossBrainComponent::GetPatternMaxRange(PatternType type) const
    {
        switch (type)
        {
        case PatternType::Kick:
            return std::max(0.0f, m_kickRange);
        case PatternType::Charge:
            return std::max(0.0f, m_chargeRangeMax);
        case PatternType::Dash:
            return std::max(0.0f, m_dashRangeMax);
        case PatternType::Ranged:
            return std::max(0.0f, m_rangedRangeMin);
        case PatternType::AttackA:
        case PatternType::AttackB:
        case PatternType::AttackC:
        default:
            return std::max(0.0f, m_meleeDistance);
        }
    }

    const char* C_BossBrainComponent::GetStateLabel(BrainState state) const
    {
        switch (state)
        {
        case BrainState::Idle: return "Idle";
        case BrainState::Orbit: return "Orbit";
        case BrainState::Approach: return "Approach";
        case BrainState::Retreat: return "Retreat";
        case BrainState::Chase: return "Chase";
        case BrainState::Attack: return "Attack";
        case BrainState::Gimmick: return "Gimmick";
        default: return "Unknown";
        }
    }

    const char* C_BossBrainComponent::GetPatternLabel(PatternType type) const
    {
        switch (type)
        {
        case PatternType::AttackA: return "Attack A";
        case PatternType::AttackB: return "Attack B";
        case PatternType::AttackC: return "Attack C";
        case PatternType::Dash: return "Dash";
        case PatternType::Ranged: return "Ranged";
        case PatternType::Kick: return "Kick";
        case PatternType::Charge: return "Charge";
        case PatternType::Special: return "Special";
        default: return "None";
        }
    }

    void C_BossBrainComponent::ResetBrain()
    {
        m_state = BrainState::Idle;
        m_activePattern = PatternType::None;
        m_patternQueue.clear();
        m_attackCycleIndex = 0;
        m_patrolDirection = m_startPatrolRight ? 1 : -1;
        m_stateTimer = 0.0f;
        m_attackCooldownTimer = 0.0f;
        m_blockedCooldownTimer = 0.0f;
        m_stuckTimer = 0.0f;
        m_lastDistance = 0.0f;
        m_chargeTimer = 0.0f;
        m_specialTimer = 0.0f;
        m_attackIssued = false;
        m_specialPending = false;
        m_gimmickActive = false;
        m_wantsFaceTarget = false;
        m_testTraceTimer = 0.0f;
        m_testTraceTargetSec = 0.0f;
        m_testRetreatTimer = 0.0f;
        m_debugLabel = "Idle";
    }
}
