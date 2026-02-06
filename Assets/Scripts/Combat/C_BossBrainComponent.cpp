#include "C_BossBrainComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
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

        int RandomSign()
        {
            return (RandomRange(0.0f, 1.0f) < 0.5f) ? -1 : 1;
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

    Combat::BossIntent C_BossBrainComponent::Think(float deltaTime, EntityId targetId)
    {
        Combat::BossIntent intent{};
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
        float targetDot = 1.0f;
        float targetSideDot = 0.0f;
        if (hasDir)
        {
            const float yaw = selfTr->rotation.y;
            const float fx = std::sin(yaw);
            const float fz = std::cos(yaw);
            targetDot = fx * toTarget.x + fz * toTarget.y;
            const float rx = fz;
            const float rz = -fx;
            targetSideDot = rx * toTarget.x + rz * toTarget.y;
            targetInFront = (targetDot >= 0.0f);
        }
        m_targetDot = targetDot;
        m_targetSideDot = targetSideDot;
        UpdateDecisionState(dt, dist, hasDir, targetDot, targetSideDot);

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
                    if (next == BrainState::Orbit)
                    {
                        m_patrolDirection = RandomSign();
                        m_traceEnterDist = dist;
                    }
                    if (next != BrainState::Idle)
                        m_idleTargetSec = 0.0f;
                }
            };

        auto BeginAttack = [&](PatternType type)
            {
                m_activePattern = type;
                m_attackIssued = false;
                m_chargeTimer = (type == PatternType::Charge) ? std::max(0.0f, m_chargeHoldSec) : 0.0f;
                m_attackOutcome = AttackOutcome::None;
                m_attackOutcomeSet = false;
                m_lastAttackPattern = type;
                m_lastPattern = type;
                if (type == PatternType::Charge)
                    m_chargePending = false;
                m_decision.lockedSector = m_decision.sector;
                m_decision.sectorLocked = true;
                m_decision.sectorLockedSec = 0.0f;
                MarkPatternUsed(type);
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
                    intent.attackRequested = true;
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
            const float idleMin = std::max(0.0f, m_testIdleMinSec);
            const float idleMax = std::max(idleMin, m_testIdleMaxSec);

            if (m_state == BrainState::Orbit && blocked && m_blockedCooldownTimer <= 0.0f)
            {
                m_patrolDirection = (m_patrolDirection >= 0) ? -1 : 1;
                m_blockedCooldownTimer = std::max(0.0f, m_patrolBlockedCooldownSec);
            }

            if (m_state == BrainState::Attack)
            {
                if (!m_attackIssued)
                {
                    intent.attackRequested = true;
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
                    m_idleTargetSec = (idleMax > 0.0f) ? RandomRange(idleMin, idleMax) : 0.0f;
                    EnterState(BrainState::Idle);
                }
            }
            else if (m_state == BrainState::Idle)
            {
                const float holdSec = std::max(0.0f, m_idleTargetSec);
                if (holdSec <= 0.0f || m_stateTimer >= holdSec)
                {
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

        if (m_usePhaseRules)
        {
            auto PeekQueuedPattern = [&]() -> PatternType
                {
                    return m_patternQueue.empty() ? PatternType::None : m_patternQueue.front();
                };
            auto PopQueuedPattern = [&]() -> PatternType
                {
                    if (m_patternQueue.empty())
                        return PatternType::None;
                    PatternType next = m_patternQueue.front();
                    m_patternQueue.pop_front();
                    return next;
                };
            auto PushQueuedPattern = [&](PatternType type)
                {
                    if (type == PatternType::None)
                        return;
                    if (m_patternQueueMax > 0 && static_cast<int>(m_patternQueue.size()) >= m_patternQueueMax)
                        return;
                    m_patternQueue.push_back(type);
                };
            auto PeekFollowupPattern = [&]() -> PatternType
                {
                    return m_followupQueue.empty() ? PatternType::None : m_followupQueue.front();
                };
            auto PopFollowupPattern = [&]() -> PatternType
                {
                    if (m_followupQueue.empty())
                        return PatternType::None;
                    PatternType next = m_followupQueue.front();
                    m_followupQueue.pop_front();
                    return next;
                };
            auto PushFollowupFront = [&](PatternType type)
                {
                    if (type == PatternType::None)
                        return;
                    m_followupQueue.push_front(type);
                };
            auto StartIntent = [&](PatternType root)
                {
                    if (root == PatternType::None)
                        return;
                    m_intentActive = true;
                    m_intentCompleted = false;
                    m_intentRoot = root;
                    m_followupQueue.clear();
                };
            auto CompleteIntent = [&]()
                {
                    m_intentActive = false;
                    m_intentCompleted = true;
                    m_intentRoot = PatternType::None;
                    m_followupQueue.clear();
                };
            auto StartRetreat = [&](float targetDist)
                {
                    m_retreatTargetDist = std::max(0.0f, targetDist);
                    EnterState(BrainState::Retreat);
                };
            auto PeekPendingPattern = [&]() -> PatternType
                {
                    if (m_intentActive)
                        return PeekFollowupPattern();
                    return PeekQueuedPattern();
                };
            auto PopPendingPattern = [&]() -> PatternType
                {
                    if (m_intentActive)
                        return PopFollowupPattern();
                    PatternType next = PopQueuedPattern();
                    if (next != PatternType::None)
                        StartIntent(next);
                    return next;
                };
            auto ResolveAttackDuration = [&]() -> float
                {
                    if (auto* driver = world->GetComponent<AttackDriverComponent>(selfId))
                    {
                        if (driver->attackStateDurationSec > 0.0f)
                            return driver->attackStateDurationSec;
                        if (driver->attackStateDurationAutoSec > 0.0f)
                            return driver->attackStateDurationAutoSec;
                    }
                    return 0.0f;
                };

            const float phase2Ratio = std::clamp(m_phase2HpRatio, 0.0f, 1.0f);
            if (!m_phase2Active && hpRatio <= phase2Ratio)
            {
                m_phase2Active = true;
                // TODO: Phase2 gimmick sequence (howling + effects) before resuming combat.
            }

            if (m_chargeIntervalSec > 0.0f)
            {
                m_chargeIntervalTimer += dt;
                if (m_chargeIntervalTimer >= m_chargeIntervalSec)
                {
                    m_chargeIntervalTimer -= m_chargeIntervalSec;
                    m_chargePending = true;
                }
            }

            auto IsPatternInRange = [&](PatternType type) -> bool
                {
                    const float minRange = GetPatternMinRange(type);
                    const float maxRange = GetPatternMaxRange(type);
                    if (type == PatternType::Ranged)
                        return dist >= minRange;
                    return dist >= minRange && dist <= maxRange;
                };

            auto PickRandomPattern = [&](PatternType avoid, bool includeCharge) -> PatternType
                {
                    std::vector<PatternType> pool;
                    pool.reserve(4);
                    pool.push_back(PatternType::AttackA);
                    pool.push_back(PatternType::AttackB);
                    pool.push_back(PatternType::AttackC);
                    if (includeCharge)
                        pool.push_back(PatternType::Charge);

                    if (pool.size() > 1 && avoid != PatternType::None)
                    {
                        pool.erase(std::remove(pool.begin(), pool.end(), avoid), pool.end());
                    }
                    if (pool.empty())
                        return PatternType::None;

                    const float r = RandomRange(0.0f, static_cast<float>(pool.size()));
                    size_t idx = static_cast<size_t>(r);
                    if (idx >= pool.size())
                        idx = pool.size() - 1;
                    return pool[idx];
                };

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
                        intent.attackRequested = true;
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
                    case PatternType::Side:
                        intent.attackRequested = true;
                        break;
                    case PatternType::Kick:
                        intent.attackRequested = true;
                        break;
                    default:
                        break;
                    }
                    m_attackIssued = true;
                }

                const float holdSec = std::max(0.0f, m_attackStateHoldSec);
                float minAttackTime = (m_activePattern == PatternType::Charge)
                    ? (std::max(0.0f, m_chargeHoldSec) + holdSec)
                    : holdSec;
                const float driverDuration = ResolveAttackDuration();
                if (driverDuration > 0.0f)
                    minAttackTime = std::max(minAttackTime, driverDuration);

                if (m_attackIssued && m_stateTimer >= minAttackTime)
                {
                    AttackOutcome outcome = m_attackOutcomeSet ? m_attackOutcome : AttackOutcome::Evaded;
                    PatternType followUp = PatternType::None;
                    if (m_intentActive && m_phase2Active && outcome == AttackOutcome::Evaded)
                    {
                        switch (m_lastAttackPattern)
                        {
                        case PatternType::AttackB: followUp = PatternType::AttackA; break;
                        case PatternType::Kick:
                        case PatternType::Side: followUp = PatternType::Charge; break;
                        case PatternType::AttackA: followUp = PatternType::Charge; break;
                        case PatternType::AttackC: followUp = PatternType::AttackC; break;
                        default: break;
                        }
                    }

                    m_attackIssued = false;
                    m_chargeTimer = 0.0f;
                    m_attackOutcomeSet = false;
                    m_attackOutcome = AttackOutcome::None;
                    m_decision.sectorLocked = false;
                    m_decision.sectorLockedSec = 0.0f;
                    m_decision.lockedSector = Sector::Unknown;

                    if (m_rerollAfterAttack)
                    {
                        m_rerollAfterAttack = false;
                        m_activePattern = PatternType::None;
                        m_patternQueue.clear();
                        CompleteIntent();
                        m_attackCooldownTimer = 0.0f;
                        EnterState(BrainState::Orbit);
                    }
                    else if (followUp != PatternType::None)
                    {
                        m_activePattern = PatternType::None;
                        m_forceWalkAfterAttack = false;
                        PushFollowupFront(followUp);
                        EnterState(BrainState::Orbit);
                    }
                    else
                    {
                        m_activePattern = PatternType::None;
                        m_attackCooldownTimer = std::max(0.0f, m_attackCooldown);
                        const bool intentJustCompleted = m_intentActive;
                        if (intentJustCompleted)
                            CompleteIntent();

                        const float retreatTrigger = std::max(0.0f, m_postAttackRetreatTriggerDist);
                        const float retreatStep = std::max(0.0f, m_postAttackRetreatStepDist);
                        if (intentJustCompleted && retreatTrigger > 0.0f && retreatStep > 0.0f && dist <= retreatTrigger)
                        {
                            m_forceWalkAfterAttack = false;
                            StartRetreat(dist + retreatStep);
                        }
                        else if (m_forceWalkAfterAttack)
                        {
                            m_forceWalkAfterAttack = false;
                            EnterState(BrainState::Approach);
                        }
                        else
                        {
                            EnterState(BrainState::Idle);
                        }
                    }
                }
            }
            else if (m_state == BrainState::Orbit)
            {
                const bool rangedReady = (m_rangedCooldownSec <= 0.0f)
                    || (m_decision.timeSinceRangedSec >= m_rangedCooldownSec);
                const float traceHoldSec = (m_traceDelaySec > 0.0f) ? m_traceDelaySec : 5.0f;
                const float traceFarThreshold = m_traceEnterDist + 2.0f;
                const float traceNearThreshold = std::max(0.0f, m_traceEnterDist - 1.0f);
                const bool traceFar = (dist >= traceFarThreshold);
                const bool traceNear = (dist <= traceNearThreshold);
                const bool traceTimeReady = (traceHoldSec <= 0.0f) || (m_stateTimer >= traceHoldSec);
                bool allowDecision = traceNear || traceTimeReady;

                if (traceFar && rangedReady && PeekQueuedPattern() == PatternType::None)
                {
                    PushQueuedPattern(PatternType::Ranged);
                    m_forceWalkAfterAttack = true;
                    allowDecision = true;
                }
                if (traceNear)
                    m_attackCooldownTimer = 0.0f;

                if (allowDecision && PeekQueuedPattern() == PatternType::None && m_attackCooldownTimer <= 0.0f)
                {
                    const float closeRange = std::max(0.0f, m_kickRange);
                    PatternType nextPattern = PatternType::None;
                    if (dist <= closeRange)
                    {
                        nextPattern = targetInFront ? PatternType::Kick : PatternType::Side;
                    }
                    else if (m_chargePending && m_lastPattern != PatternType::Charge)
                    {
                        nextPattern = PatternType::Charge;
                        m_chargePending = false;
                    }
                    else
                    {
                        const bool includeCharge = m_chargePending;
                        nextPattern = PickRandomPattern(m_lastPattern, includeCharge);
                        if (nextPattern == PatternType::Charge)
                            m_chargePending = false;
                    }
                    PushQueuedPattern(nextPattern);
                }

                PatternType pending = PeekPendingPattern();
                if (pending != PatternType::None && allowDecision)
                {
                    if (pending == PatternType::Dash || IsPatternInRange(pending))
                    {
                        if (pending == PatternType::Ranged)
                            m_forceWalkAfterAttack = true;
                        m_activePattern = PopPendingPattern();
                        BeginAttack(m_activePattern);
                    }
                    else
                    {
                        EnterState(BrainState::Approach);
                    }
                }
            }
            else if (m_state == BrainState::Approach || m_state == BrainState::Chase)
            {
                PatternType pending = PeekPendingPattern();
                if (pending == PatternType::None)
                {
                    if (!m_intentActive)
                        EnterState(BrainState::Orbit);
                }
                else if (IsPatternInRange(pending))
                {
                    if (pending == PatternType::Ranged)
                        m_forceWalkAfterAttack = true;
                    m_activePattern = PopPendingPattern();
                    BeginAttack(m_activePattern);
                }
                else
                {
                    const float approachCompleteDist = std::max(0.0f, m_approachCompleteDist);
                    if (approachCompleteDist > 0.0f && dist <= approachCompleteDist)
                    {
                        if (pending == PatternType::Ranged)
                            m_forceWalkAfterAttack = true;
                        m_activePattern = PopPendingPattern();
                        BeginAttack(m_activePattern);
                    }
                    else
                    {
                        const float dashTimeout = std::max(0.0f, m_walkDashTimeoutSec);
                        const float rerollSec = std::max(0.0f, m_chargeRerollSec);
                        if (pending == PatternType::Charge && rerollSec > 0.0f && m_stateTimer >= rerollSec)
                        {
                            PopPendingPattern();
                            m_chargePending = false;
                            m_attackCooldownTimer = 0.0f;
                            CompleteIntent();
                            EnterState(BrainState::Orbit);
                        }
                        else if (dashTimeout > 0.0f && m_stateTimer >= dashTimeout)
                        {
                            PopPendingPattern();
                            CompleteIntent();
                            m_rerollAfterAttack = true;
                            BeginAttack(PatternType::Dash);
                        }
                    }
                }
            }
            else if (m_state == BrainState::Retreat)
            {
                if (m_retreatTargetDist <= 0.0f || dist >= m_retreatTargetDist)
                {
                    m_retreatTargetDist = 0.0f;
                    EnterState(BrainState::Orbit);
                }
            }
            else if (m_state == BrainState::Idle)
            {
                if (m_attackCooldownTimer <= 0.0f)
                    EnterState(BrainState::Orbit);
            }

            if (m_state == BrainState::Orbit)
            {
                if (hasDir)
                    intent.move = { right.x * static_cast<float>(m_patrolDirection), right.y * static_cast<float>(m_patrolDirection) };
            }
            else if (m_state == BrainState::Approach || m_state == BrainState::Chase)
            {
                if (hasDir)
                    intent.move = { toTarget.x, toTarget.y };
            }

            // runHeld is ignored for boss intents
            m_wantsFaceTarget = (m_state != BrainState::Gimmick);

            if (m_intentActive && m_state != BrainState::Attack
                && m_activePattern == PatternType::None
                && m_followupQueue.empty())
            {
                CompleteIntent();
            }

            std::string label = GetStateLabel(m_state);
            if (m_state == BrainState::Orbit)
                label += (m_patrolDirection >= 0) ? " (Right)" : " (Left)";
            label += m_phase2Active ? " | Phase2" : " | Phase1";
            label += " | Active: ";
            const PatternType debugPattern = (m_state == BrainState::Attack) ? m_activePattern : PeekPendingPattern();
            label += GetPatternLabel(debugPattern);
            label += " | Intent: ";
            label += GetPatternLabel(m_intentRoot);
            label += m_intentActive ? " (Active)" : " (Idle)";
            if (!m_followupQueue.empty())
            {
                label += " | Follow: [";
                for (size_t i = 0; i < m_followupQueue.size(); ++i)
                {
                    if (i > 0)
                        label += ", ";
                    label += GetPatternLabel(m_followupQueue[i]);
                }
                label += "]";
            }
            else
            {
                label += " | Follow: None";
            }
            if (m_chargePending)
                label += " | ChargePending";
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

        m_backAttackCooldownTimer = std::max(0.0f, m_backAttackCooldownTimer - dt);
        const float backRange = std::max(0.0f, m_backAttackRange);
        const float backDotThreshold = std::clamp(m_backAttackDotThreshold, -1.0f, 1.0f);
        const bool targetBehind = hasDir && (targetDot <= backDotThreshold);
        if (targetBehind && dist <= backRange)
        {
            m_backAttackTimer += dt;
        }
        else
        {
            m_backAttackTimer = 0.0f;
        }
        if (m_backAttackTriggerSec > 0.0f
            && m_backAttackCooldownTimer <= 0.0f
            && targetBehind
            && dist <= backRange
            && m_backAttackTimer >= m_backAttackTriggerSec)
        {
            m_backAttackPending = true;
            m_backAttackTimer = 0.0f;
            m_backAttackCooldownTimer = std::max(0.0f, m_backAttackCooldownSec);
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
        if (m_backAttackPending && static_cast<int>(m_patternQueue.size()) < queueMax)
        {
            if (PushPattern(PatternType::Side, false))
                m_backAttackPending = false;
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
            if (!m_attackIssued)
            {
                intent.attackRequested = true;
                m_attackIssued = true;
            }
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
                        intent.attackRequested = true;
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
                    case PatternType::Side:
                        intent.attackRequested = true;
                        break;
                    case PatternType::Kick:
                        intent.attackRequested = true;
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
            m_decision.sectorLocked = false;
            m_decision.sectorLockedSec = 0.0f;
            m_decision.lockedSector = Sector::Unknown;
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

        // runHeld is ignored for boss intents

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
                // TODO: Stuck recovery when unable to move for N seconds (teleport, reset orbit, or force ranged pattern).
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

        intent.wantsFaceTarget = m_wantsFaceTarget;
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
        case PatternType::Side:
            return 0.0f;
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
        case PatternType::Side:
            return std::max(0.0f, m_backAttackRange);
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
        case PatternType::Side: return "Side";
        default: return "None";
        }
    }

    float C_BossBrainComponent::GetTurnInPlaceDotThreshold() const
    {
        constexpr float kDegToRad = 0.01745329252f;
        const float angle = std::clamp(m_turnInPlaceAngleDeg, 0.0f, 180.0f);
        return std::cos(angle * kDegToRad);
    }

    const std::string& C_BossBrainComponent::GetPatternClip(PatternType type) const
    {
        static const std::string kEmpty{};
        switch (type)
        {
        case PatternType::AttackA: return m_clipAttackA;
        case PatternType::AttackB: return m_clipAttackBC;
        case PatternType::AttackC: return m_clipAttackABC;
        case PatternType::Dash: return m_clipDash;
        case PatternType::Ranged: return m_clipSoul;
        case PatternType::Kick: return m_clipKick;
        case PatternType::Charge: return m_clipCharge;
        case PatternType::Special: return m_clipPhaseHowling;
        case PatternType::Side: return m_clipSide;
        default: break;
        }
        return kEmpty;
    }

    void C_BossBrainComponent::NotifyDamageTaken(float amount)
    {
        if (amount <= 0.0f)
            return;
        const float windowSec = std::max(0.0f, m_damageWindowSec);
        if (windowSec <= 0.0f)
            return;
        if (m_decision.damageWindowTimer <= 0.0f)
        {
            m_decision.damageWindowTimer = windowSec;
            m_decision.damageWindowTotal = 0.0f;
            m_decision.damageWindowTriggered = false;
        }
        m_decision.damageWindowTotal += amount;
        if (m_damageWindowThreshold > 0.0f
            && m_decision.damageWindowTotal >= m_damageWindowThreshold)
        {
            m_decision.damageWindowTriggered = true;
        }
    }

    void C_BossBrainComponent::NotifyPlayerParry(bool success)
    {
        m_decision.lastParrySuccess = success;
        m_decision.lastParryFailed = !success;
        m_decision.lastGuardOrEvade = false;
        m_decision.lastFeedbackTimer = std::max(0.0f, m_feedbackHoldSec);
    }

    void C_BossBrainComponent::NotifyPlayerGuardOrEvade()
    {
        m_decision.lastGuardOrEvade = true;
        m_decision.lastParrySuccess = false;
        m_decision.lastParryFailed = false;
        m_decision.lastFeedbackTimer = std::max(0.0f, m_feedbackHoldSec);
    }

    void C_BossBrainComponent::NotifyAttackOutcome(bool evaded)
    {
        if (m_activePattern == PatternType::None)
            return;
        if (evaded)
        {
            if (!m_attackOutcomeSet)
            {
                m_attackOutcome = AttackOutcome::Evaded;
                m_attackOutcomeSet = true;
            }
            return;
        }
        m_attackOutcome = AttackOutcome::HitOrParry;
        m_attackOutcomeSet = true;
    }

    void C_BossBrainComponent::ForceCompleteIntent()
    {
        m_intentActive = false;
        m_intentCompleted = true;
        m_intentRoot = PatternType::None;
        m_followupQueue.clear();
        m_forceWalkAfterAttack = false;
        m_rerollAfterAttack = false;
        m_attackOutcome = AttackOutcome::None;
        m_attackOutcomeSet = false;
    }

    C_BossBrainComponent::DistanceBand C_BossBrainComponent::ComputeDistanceBand(float dist) const
    {
        float d2 = std::max(0.0f, m_distBand2);
        float d4 = std::max(d2, m_distBand4);
        float d6 = std::max(d4, m_distBand6);
        float d8 = std::max(d6, m_distBand8);
        float d10 = std::max(d8, m_distBand10);

        if (dist <= d2) return DistanceBand::Within2;
        if (dist <= d4) return DistanceBand::Within4;
        if (dist <= d6) return DistanceBand::Within6;
        if (dist <= d8) return DistanceBand::Within8;
        if (dist <= d10) return DistanceBand::Within10;
        return DistanceBand::Over10;
    }

    C_BossBrainComponent::Sector C_BossBrainComponent::ComputeSector(float dot, float side) const
    {
        if (!std::isfinite(dot) || !std::isfinite(side))
            return Sector::Unknown;
        constexpr float kRadToDeg = 57.2957795f;
        const float halfAngle = std::clamp(m_sectorAngleDeg * 0.5f, 1.0f, 180.0f);
        const float angleDeg = std::atan2(side, dot) * kRadToDeg;
        if (angleDeg >= -halfAngle && angleDeg <= halfAngle)
            return Sector::Front;
        if (angleDeg > halfAngle && angleDeg <= 180.0f - halfAngle)
            return Sector::Right;
        if (angleDeg < -halfAngle && angleDeg >= -180.0f + halfAngle)
            return Sector::Left;
        return Sector::Back;
    }

    void C_BossBrainComponent::UpdateDecisionState(float dt, float dist, bool hasDir, float dot, float side)
    {
        auto& ds = m_decision;
        ds.dist = dist;
        ds.distBand = ComputeDistanceBand(dist);

        const Sector newSector = hasDir ? ComputeSector(dot, side) : Sector::Unknown;
        if (newSector != ds.sector)
        {
            ds.sector = newSector;
            ds.sectorStaySec = 0.0f;
        }
        else
        {
            ds.sectorStaySec += dt;
        }

        if (ds.sectorLocked)
            ds.sectorLockedSec += dt;
        ds.sectorDifferentFromLocked = ds.sectorLocked && (ds.sector != ds.lockedSector);

        const float behindDot = std::clamp(m_backAttackDotThreshold, -1.0f, 1.0f);
        ds.targetBehind = hasDir && (dot <= behindDot);

        ds.timeSinceDashSec += dt;
        ds.timeSinceRangedSec += dt;
        ds.timeSinceChargeSec += dt;
        ds.timeSinceSpecialSec += dt;

        if (ds.damageWindowTimer > 0.0f)
        {
            ds.damageWindowTimer = std::max(0.0f, ds.damageWindowTimer - dt);
            if (ds.damageWindowTimer <= 0.0f)
            {
                ds.damageWindowTotal = 0.0f;
                ds.damageWindowTriggered = false;
            }
        }

        if (ds.lastFeedbackTimer > 0.0f)
        {
            ds.lastFeedbackTimer = std::max(0.0f, ds.lastFeedbackTimer - dt);
            if (ds.lastFeedbackTimer <= 0.0f)
            {
                ds.lastParrySuccess = false;
                ds.lastParryFailed = false;
                ds.lastGuardOrEvade = false;
            }
        }
    }

    void C_BossBrainComponent::MarkPatternUsed(PatternType type)
    {
        switch (type)
        {
        case PatternType::Dash:
            m_decision.timeSinceDashSec = 0.0f;
            break;
        case PatternType::Ranged:
            m_decision.timeSinceRangedSec = 0.0f;
            break;
        case PatternType::Charge:
            m_decision.timeSinceChargeSec = 0.0f;
            break;
        case PatternType::Special:
            m_decision.timeSinceSpecialSec = 0.0f;
            break;
        default:
            break;
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
        m_chargeIntervalTimer = 0.0f;
        m_backAttackTimer = 0.0f;
        m_backAttackCooldownTimer = 0.0f;
        m_attackIssued = false;
        m_specialPending = false;
        m_backAttackPending = false;
        m_gimmickActive = false;
        m_phase2Active = false;
        m_chargePending = false;
        m_forceWalkAfterAttack = false;
        m_rerollAfterAttack = false;
        m_wantsFaceTarget = false;
        m_targetDot = 1.0f;
        m_targetSideDot = 0.0f;
        m_decision = {};
        m_testTraceTimer = 0.0f;
        m_testTraceTargetSec = 0.0f;
        m_testRetreatTimer = 0.0f;
        m_idleTargetSec = 0.0f;
        m_retreatTargetDist = 0.0f;
        m_debugLabel = "Idle";
        m_traceEnterDist = 0.0f;
        m_lastPattern = PatternType::None;
        m_lastAttackPattern = PatternType::None;
        m_attackOutcome = AttackOutcome::None;
        m_attackOutcomeSet = false;
        m_intentActive = false;
        m_intentCompleted = true;
        m_intentRoot = PatternType::None;
        m_followupQueue.clear();
    }
}
