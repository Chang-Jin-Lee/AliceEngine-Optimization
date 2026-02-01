#pragma once
/*
* 전투 루프 총괄. 플레이어/보스 Fighter·FSM·이벤트버스·리졸버를 갖고, 매 프레임 Intent → Sensors → FSM → Command → 적용 순서로 돌림.
* 씬의 매니저용 엔티티 (빈 오브젝트 등). m_playerGuid, m_bossGuid로 플레이어/보스 엔티티를 찾음.
*/

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include <memory>
#include <string>
#include "C_CombatContracts.h"

namespace Alice
{
    class C_CombatSessionComponent : public IScript
    {
        ALICE_BODY(C_CombatSessionComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void PostCombatUpdate(float deltaTime) override;
        void OnEnable() override;
        void OnDisable() override;
        ~C_CombatSessionComponent() override;

        Combat::ActionState GetPlayerState() const;
        Combat::ActionState GetBossState() const;
        Combat::ActionFlags GetPlayerFlags() const;
        Combat::ActionFlags GetBossFlags() const;

        // Entity resolution (GUID preferred, name fallback when enabled)
        ALICE_PROPERTY(uint64_t, m_playerGuid, 0);
        ALICE_PROPERTY(uint64_t, m_bossGuid, 0);
        ALICE_PROPERTY(bool, m_autoResolveByName, true);
        ALICE_PROPERTY(std::string, m_playerName, "Player");
        ALICE_PROPERTY(std::string, m_bossName, "Enemy");

        // Logging
        ALICE_PROPERTY(bool, m_enableLogs, false);
        ALICE_PROPERTY(bool, m_enableCombatLogs, false);

        // Combat rules
        ALICE_PROPERTY(bool, m_playerCanBeHitstunned, true);
        ALICE_PROPERTY(bool, m_bossCanBeHitstunned, false);

        // Animation blending
        ALICE_PROPERTY(float, m_animBlendSec, 0.12f);
        ALICE_PROPERTY(float, m_moveBlendSpeed, 8.0f);

        // Default animation clips (shared fallback)
        ALICE_PROPERTY(std::string, m_idleClip, "Idle");
        ALICE_PROPERTY(std::string, m_moveClip, "Walk");
        ALICE_PROPERTY(std::string, m_lightAttackClip, "alice-Apose_arm|Swing");
        ALICE_PROPERTY(std::string, m_heavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_heavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_dodgeClip, "");
        ALICE_PROPERTY(std::string, m_guardEnterClip, "");
        ALICE_PROPERTY(std::string, m_guardLoopClip, "");
        ALICE_PROPERTY(std::string, m_guardExitClip, "");
        ALICE_PROPERTY(float, m_guardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_guardExitDurationSec, 0.0f);

        // Per-entity animation overrides (optional)
        ALICE_PROPERTY(std::string, m_playerIdleClip, "");
        ALICE_PROPERTY(std::string, m_playerMoveClip, "");
        ALICE_PROPERTY(std::string, m_playerLightAttackClip, "");
        ALICE_PROPERTY(std::string, m_playerHeavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_playerHeavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_playerDodgeClip, "");
        ALICE_PROPERTY(std::string, m_playerGuardEnterClip, "");
        ALICE_PROPERTY(std::string, m_playerGuardLoopClip, "");
        ALICE_PROPERTY(std::string, m_playerGuardExitClip, "");
        ALICE_PROPERTY(float, m_playerGuardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_playerGuardExitDurationSec, 0.0f);
        ALICE_PROPERTY(std::string, m_bossIdleClip, "");
        ALICE_PROPERTY(std::string, m_bossMoveClip, "");
        ALICE_PROPERTY(std::string, m_bossLightAttackClip, "");
        ALICE_PROPERTY(std::string, m_bossHeavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_bossHeavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_bossDodgeClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardEnterClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardLoopClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardExitClip, "");
        ALICE_PROPERTY(float, m_bossGuardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_bossGuardExitDurationSec, 0.0f);

        // TODO: temp feel-tuning; move to per-attack data.
        ALICE_PROPERTY(float, m_lightAttackMoveDistance, 0.7f);
        ALICE_PROPERTY(float, m_heavyAttackMoveDistance, 1.2f);
        ALICE_PROPERTY(float, m_lightAttackMoveStartSec, 1.75f);
        ALICE_PROPERTY(float, m_heavyAttackMoveStartSec, 2.2f);
        ALICE_PROPERTY(float, m_lightAttackMoveDurationSec, 0.05f);
        ALICE_PROPERTY(float, m_heavyAttackMoveDurationSec, 0.05f);
        ALICE_PROPERTY(bool, m_debugAttackMoveTime, false);

        // Attack clip slow-motion was removed; keep commented for reference.
        // ALICE_PROPERTY(std::string, m_attackSlowClipName, "swing");
        // ALICE_PROPERTY(float, m_attackSlowSpeed, 0.7f);

        // Movement facing offset (degrees)
        ALICE_PROPERTY(float, m_rotationOffsetDeg, 180.0f);

        void ForceReset();
        ALICE_FUNC(ForceReset);

    private:
        EntityId ResolveEntity(uint64_t guid) const;
        EntityId ResolveEntityByName(const std::string& name) const;

        struct SessionState;
        struct SessionStateDeleter
        {
            void operator()(SessionState* ptr) const;
        };
        std::unique_ptr<SessionState, SessionStateDeleter> m_state;
    };
}
