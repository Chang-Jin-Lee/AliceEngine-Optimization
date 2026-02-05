#pragma once
/*
* 보스 AI. 타겟(플레이어)과의 거리/각도 보고 공격·이동 등 Combat::Intent 생성.
* 보스/적 캐릭터 엔티티 (보스 엔티티).
*/
#include <string>
#include <deque>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include "C_CombatContracts.h"

namespace Alice
{
    class C_BossBrainComponent : public IScript
    {
        ALICE_BODY(C_BossBrainComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::Intent Think(float deltaTime, EntityId targetId);

        const std::string& GetDebugLabel() const { return m_debugLabel; }
        bool WantsFaceTarget() const { return m_wantsFaceTarget; }

        // 공격/패턴 튜닝
        ALICE_PROPERTY(float, m_attackCooldown, 1.0f);
        ALICE_PROPERTY(float, m_attackStateHoldSec, 0.45f);
        ALICE_PROPERTY(float, m_specialPatternHoldSec, 1.2f);
        ALICE_PROPERTY(float, m_specialIntervalSec, 60.0f);
        ALICE_PROPERTY(int, m_patternQueueTarget, 2);
        ALICE_PROPERTY(int, m_patternQueueMax, 3);
        ALICE_PROPERTY(bool, m_testSwingLoop, true);
        ALICE_PROPERTY(bool, m_stationaryAttackBoss, false);
        ALICE_PROPERTY(float, m_testTraceDurationSec, 0.8f);
        ALICE_PROPERTY(float, m_testTraceMinSec, 0.4f);
        ALICE_PROPERTY(float, m_testTraceMaxSec, 1.2f);
        ALICE_PROPERTY(float, m_testApproachDistance, 0.5f);
        ALICE_PROPERTY(float, m_testRetreatDistance, 1.6f);
        ALICE_PROPERTY(float, m_testRetreatMaxSec, 0.4f);
        ALICE_PROPERTY(float, m_testIdleMinSec, 1.0f);
        ALICE_PROPERTY(float, m_testIdleMaxSec, 3.0f);
        ALICE_PROPERTY(float, m_patrolDistance, 4.0f);
        ALICE_PROPERTY(float, m_patrolTolerance, 0.8f);
        ALICE_PROPERTY(float, m_meleeDistance, 2.4f);
        ALICE_PROPERTY(float, m_chaseDistance, 7.0f);
        ALICE_PROPERTY(float, m_patrolBlockedCooldownSec, 0.25f);
        ALICE_PROPERTY(float, m_stuckTimeoutSec, 3.0f);

        // 공격 사거리 (단순 디버그용)
        ALICE_PROPERTY(float, m_dashRangeMin, 4.0f);
        ALICE_PROPERTY(float, m_dashRangeMax, 6.5f);
        ALICE_PROPERTY(float, m_rangedRangeMin, 6.0f);
        ALICE_PROPERTY(float, m_chargeRangeMax, 4.0f);
        ALICE_PROPERTY(float, m_kickRange, 2.0f);

        // 차징 공격 (디버그용)
        ALICE_PROPERTY(float, m_chargeHoldSec, 0.5f);
        ALICE_PROPERTY(int, m_chargeLevel, 2);

        // 특수 기믹 페이지
        ALICE_PROPERTY(bool, m_enableGimmickPhase, false);
        ALICE_PROPERTY(float, m_gimmickHpRatio, 0.35f);
        ALICE_PROPERTY(bool, m_startPatrolRight, true);

    private:
        enum class BrainState : uint8_t
        {
            Idle,
            Orbit,
            Approach,
            Retreat,
            Chase,
            Attack,
            Gimmick
        };

        enum class PatternType : uint8_t
        {
            None,
            AttackA,
            AttackB,
            AttackC,
            Dash,
            Ranged,
            Kick,
            Charge,
            Special
        };

        PatternType PickNextPattern(float dist,
                                    bool targetInFront,
                                    float hpRatio,
                                    bool stuckRisk,
                                    PatternType avoidA,
                                    PatternType avoidB);
        bool IsPatternInRange(PatternType type, float dist) const;
        float GetPatternMinRange(PatternType type) const;
        float GetPatternMaxRange(PatternType type) const;
        const char* GetStateLabel(BrainState state) const;
        const char* GetPatternLabel(PatternType type) const;
        void ResetBrain();

        BrainState m_state = BrainState::Orbit;
        PatternType m_activePattern = PatternType::None;
        std::deque<PatternType> m_patternQueue;
        int m_attackCycleIndex = 0;
        int m_patrolDirection = 1;
        float m_stateTimer = 0.0f;
        float m_attackCooldownTimer = 0.0f;
        float m_blockedCooldownTimer = 0.0f;
        float m_stuckTimer = 0.0f;
        float m_lastDistance = 0.0f;
        float m_chargeTimer = 0.0f;
        float m_specialTimer = 0.0f;
        float m_testTraceTimer = 0.0f;
        float m_testTraceTargetSec = 0.0f;
        float m_testRetreatTimer = 0.0f;
        float m_idleTargetSec = 0.0f;
        bool m_attackIssued = false;
        bool m_specialPending = false;
        bool m_gimmickActive = false;
        bool m_wantsFaceTarget = false;
        std::string m_debugLabel;
    };
}
