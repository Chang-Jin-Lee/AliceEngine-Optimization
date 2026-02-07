#pragma once
/*
* 보스 AI. 타겟(플레이어)과의 거리/각도 보고 공격·이동 등 Combat::Intent 생성.
* 보스/적 캐릭터 엔티티 (보스 엔티티).
*/
#include <string>
#include <deque>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include "BossCombatTypes.h"

namespace Alice
{
    class C_BossBrainComponent : public IScript
    {
        ALICE_BODY(C_BossBrainComponent);

    public:
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
            Special,
            Side,
        };

        enum class Sector : uint8_t
        {
            Unknown,
            Front,
            Right,
            Back,
            Left,
        };

        enum class DistanceBand : uint8_t
        {
            Unknown,
            Within2,
            Within4,
            Within6,
            Within8,
            Within10,
            Over10,
        };

        struct DecisionState
        {
            float dist = 0.0f;
            DistanceBand distBand = DistanceBand::Unknown;
            Sector sector = Sector::Unknown;
            Sector lockedSector = Sector::Unknown;
            bool sectorLocked = false;
            float sectorStaySec = 0.0f;
            float sectorLockedSec = 0.0f;
            bool sectorDifferentFromLocked = false;
            bool targetBehind = false;

            float timeSinceDashSec = 0.0f;
            float timeSinceRangedSec = 0.0f;
            float timeSinceChargeSec = 0.0f;
            float timeSinceSpecialSec = 0.0f;

            float damageWindowTimer = 0.0f;
            float damageWindowTotal = 0.0f;
            bool damageWindowTriggered = false;

            bool lastParrySuccess = false;
            bool lastParryFailed = false;
            bool lastGuardOrEvade = false;
            float lastFeedbackTimer = 0.0f;
        };

        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::BossIntent Think(float deltaTime, EntityId targetId);

        const std::string& GetDebugLabel() const { return m_debugLabel; }
        bool WantsFaceTarget() const { return m_wantsFaceTarget; }
        BrainState GetBrainState() const { return m_state; }
        PatternType GetActivePattern() const { return m_activePattern; }
        float GetTargetDot() const { return m_targetDot; }
        float GetTargetSideDot() const { return m_targetSideDot; }
        float GetTurnInPlaceDotThreshold() const;
        const DecisionState& GetDecisionState() const { return m_decision; }

        const std::string& GetIdleClip() const { return m_clipIdle; }
        const std::string& GetWalkForwardClip() const { return m_clipWalkForward; }
        const std::string& GetWalkSideClip() const { return m_clipWalkSide; }
        const std::string& GetTurnLeftClip() const { return m_clipTurnLeft; }
        const std::string& GetTurnRightClip() const { return m_clipTurnRight; }
        const std::string& GetHitClip() const { return m_clipHit; }
        const std::string& GetGroggyClip() const { return m_clipGroggy; }
        const std::string& GetGroggyRecoverClip() const { return m_clipGroggyRecover; }
        const std::string& GetDieClip() const { return m_clipDie; }
        const std::string& GetPatternClip(PatternType type) const;
        void NotifyDamageTaken(float amount);
        void NotifyPlayerParry(bool success);
        void NotifyPlayerGuardOrEvade();
        void NotifyAttackOutcome(bool evaded);
        void ForceCompleteIntent();

        // 공격/패턴 튜닝
        ALICE_PROPERTY(float, m_attackCooldown, 1.0f);
        ALICE_PROPERTY(float, m_attackStateHoldSec, 0.45f);
        ALICE_PROPERTY(float, m_specialPatternHoldSec, 1.2f);
        ALICE_PROPERTY(float, m_specialIntervalSec, 60.0f);
        ALICE_PROPERTY(bool, m_usePhaseRules, true);
        ALICE_PROPERTY(float, m_phase2HpRatio, 0.7f);
        ALICE_PROPERTY(float, m_chargeIntervalSec, 30.0f);
        ALICE_PROPERTY(float, m_chargeRerollSec, 5.0f);
        ALICE_PROPERTY(float, m_walkDashTimeoutSec, 5.0f);
        ALICE_PROPERTY(int, m_patternQueueTarget, 2);
        ALICE_PROPERTY(int, m_patternQueueMax, 3);
        ALICE_PROPERTY(bool, m_testSwingLoop, false);
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
        ALICE_PROPERTY(float, m_meleeDistance, 2.5f);
        ALICE_PROPERTY(float, m_chaseDistance, 7.0f);
        ALICE_PROPERTY(float, m_patrolBlockedCooldownSec, 0.25f);
        ALICE_PROPERTY(float, m_stuckTimeoutSec, 3.0f);
        ALICE_PROPERTY(float, m_approachCompleteDist, 2.5f);
        ALICE_PROPERTY(float, m_postAttackRetreatTriggerDist, 1.0f);
        ALICE_PROPERTY(float, m_postAttackRetreatStepDist, 1.0f);

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

        // 뒤쪽 체류 패턴
        ALICE_PROPERTY(float, m_backAttackTriggerSec, 1.2f);
        ALICE_PROPERTY(float, m_backAttackCooldownSec, 3.0f);
        ALICE_PROPERTY(float, m_backAttackRange, 2.5f);
        ALICE_PROPERTY(float, m_backAttackDotThreshold, -0.3f);
        ALICE_PROPERTY(float, m_turnInPlaceAngleDeg, 65.0f);

        // 판단/섹터/거리 분류용
        ALICE_PROPERTY(float, m_sectorAngleDeg, 90.0f);
        ALICE_PROPERTY(float, m_distBand2, 2.0f);
        ALICE_PROPERTY(float, m_distBand4, 4.0f);
        ALICE_PROPERTY(float, m_distBand6, 6.0f);
        ALICE_PROPERTY(float, m_distBand8, 8.0f);
        ALICE_PROPERTY(float, m_distBand10, 10.0f);
        ALICE_PROPERTY(float, m_attackSectorHoldSec, 0.6f);
        ALICE_PROPERTY(float, m_traceDelaySec, 0.5f);
        ALICE_PROPERTY(float, m_actionDelaySec, 0.3f);
        ALICE_PROPERTY(float, m_rotationOffsetDeg, 180.0f);
        ALICE_PROPERTY(float, m_dashCooldownSec, 10.0f);
        ALICE_PROPERTY(float, m_rangedCooldownSec, 10.0f);
        ALICE_PROPERTY(int, m_rangedRepeatCount, 3);
        ALICE_PROPERTY(float, m_damageWindowSec, 0.0f);
        ALICE_PROPERTY(float, m_damageWindowThreshold, 0.0f);
        ALICE_PROPERTY(float, m_feedbackHoldSec, 0.5f);

        // 보스 애니메이션 세트
        ALICE_PROPERTY(std::string, m_clipIdle, "Boss|Boss|Idle");
        ALICE_PROPERTY(std::string, m_clipWalkForward, "Boss|Boss|Walk_Forward");
        ALICE_PROPERTY(std::string, m_clipWalkSide, "Boss|Boss|Walk_Side");
        ALICE_PROPERTY(std::string, m_clipTurnLeft, "");
        ALICE_PROPERTY(std::string, m_clipTurnRight, "");
        ALICE_PROPERTY(std::string, m_clipHit, "Boss|Boss|Hit");
        ALICE_PROPERTY(std::string, m_clipGroggy, "Boss|Boss|Groggy_Attacked");
        ALICE_PROPERTY(std::string, m_clipGroggyRecover, "Boss|Boss|Groggy_Recovery");
        ALICE_PROPERTY(std::string, m_clipDie, "Boss|Boss|Die");
        ALICE_PROPERTY(std::string, m_clipAttackA, "Boss|Boss|Attack_A");
        ALICE_PROPERTY(std::string, m_clipAttackBC, "Boss|Boss|Attack_BC");
        ALICE_PROPERTY(std::string, m_clipAttackABC, "Boss|Boss|Attack_ABC");
        ALICE_PROPERTY(std::string, m_clipCharge, "Boss|Boss|Charge_Attack");
        ALICE_PROPERTY(std::string, m_clipDash, "Boss|Boss|Dash_Attack");
        ALICE_PROPERTY(std::string, m_clipKick, "Boss|Boss|Kick_Attack");
        ALICE_PROPERTY(std::string, m_clipSide, "Boss|Boss|Side_Attack");
        ALICE_PROPERTY(std::string, m_clipSoul, "Boss|Boss|Soul_Attack");
        ALICE_PROPERTY(std::string, m_clipPhaseHowling, "Boss|Boss|Phase_Howling");

    private:
        enum class AttackOutcome : uint8_t
        {
            None,
            HitOrParry,
            Evaded
        };

        DistanceBand ComputeDistanceBand(float dist) const;
        Sector ComputeSector(float dot, float side) const;
        void UpdateDecisionState(float dt, float dist, bool hasDir, float dot, float side);
        void MarkPatternUsed(PatternType type);

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
        float m_chargeIntervalTimer = 0.0f;
        float m_backAttackTimer = 0.0f;
        float m_backAttackCooldownTimer = 0.0f;
        float m_testTraceTimer = 0.0f;
        float m_testTraceTargetSec = 0.0f;
        float m_testRetreatTimer = 0.0f;
        float m_idleTargetSec = 0.0f;
        bool m_attackIssued = false;
        bool m_specialPending = false;
        bool m_backAttackPending = false;
        bool m_gimmickActive = false;
        bool m_phase2Active = false;
        bool m_chargePending = false;
        bool m_forceWalkAfterAttack = false;
        bool m_rerollAfterAttack = false;
        bool m_wantsFaceTarget = false;
        float m_targetDot = 1.0f;
        float m_targetSideDot = 0.0f;
        DecisionState m_decision{};
        std::string m_debugLabel;
        float m_traceEnterDist = 0.0f;
        PatternType m_lastPattern = PatternType::None;
        PatternType m_lastAttackPattern = PatternType::None;
        AttackOutcome m_attackOutcome = AttackOutcome::None;
        bool m_attackOutcomeSet = false;
        bool m_intentActive = false;
        bool m_intentCompleted = true;
        PatternType m_intentRoot = PatternType::None;
        std::deque<PatternType> m_followupQueue;
        float m_retreatTargetDist = 0.0f;
    };
}
