#pragma once
/*
Intent + Sensors + 이벤트로 상태(Idle/Move/Attack/Dodge/Guard/Hitstun/Groggy/Dead) 전이, FsmOutput(상태·플래그·Command 목록) 반환.
C_CombatSession이 playerFsm, bossFsm 2개 보유.
*/
#include <vector>

#include "C_CombatContracts.h"
namespace Alice::Combat
{
    class ActionFsm
    {
    public:
        ActionFsm() = default;

        void Reset();

        FsmOutput Update(EntityId self,
                         const Intent& intent,
                         const Sensors& sensors,
                         const std::vector<CombatEvent>& events,
                         float dtSec);

        ActionState State() const { return m_state; }
        float StateTime() const { return m_stateTime; }

    private:
        ActionState m_state = ActionState::Idle;
        float m_stateTime = 0.0f;
        bool m_attackCommitted = false;
        bool m_prevHitActive = false;
        Vec2 m_lastMoveDir{};
        bool m_lastMoveValid = false;
        Vec2 m_dodgeDir{};
        bool m_dodgeDirValid = false;
        float m_dodgeDurationSec = 0.6f;
        float m_dodgeMoveDurationSec = 0.5f;
        float m_dodgeDistance = 3.0f;
        float m_dodgeMoveTimer = 0.0f;
        bool m_dodgeMoveStopped = false;

        void Enter(ActionState next);
    };
}
