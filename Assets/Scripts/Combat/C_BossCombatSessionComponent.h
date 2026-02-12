#pragma once
/*
* Boss combat session: simplified boss execution loop (intent -> state/flags).
*/

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include "BossCombatTypes.h"

namespace Alice
{
    class World;
    class C_BossBrainComponent;

    class C_BossCombatSessionComponent : public IScript
    {
        ALICE_BODY(C_BossCombatSessionComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::BossOutput Tick(World& world,
                                float deltaTime,
                                EntityId bossId,
                                EntityId targetId,
                                C_BossBrainComponent* brain,
                                const Combat::Sensors& sensors,
                                bool hitstopActive,
                                const Combat::BossSignals& signals);

        ALICE_PROPERTY(bool, m_hitReactUseFullClipDuration, true);
        ALICE_PROPERTY(float, m_hitReactDurationSec, 0.25f);
        ALICE_PROPERTY(float, m_groggyRecoverIdleHoldSec, 1.0f);

    private:
        void Reset();

        Combat::ActionState m_state = Combat::ActionState::Idle;
        float m_stateTimer = 0.0f;
        float m_hitReactTimer = 0.0f;
        float m_groggyTimer = 0.0f;
        float m_groggyDurationSec = 0.0f;
        bool m_groggyRecovering = false;
        float m_groggyRecoverTimer = 0.0f;
        float m_groggyRecoverDurationSec = 0.0f;
        float m_postGroggyIdleHoldTimer = 0.0f;
        bool m_prevGroggyHold = false;
    };
}
