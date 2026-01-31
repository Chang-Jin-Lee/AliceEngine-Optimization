#include "C_ActionFsm.h"

#include <cmath>

namespace Alice::Combat
{
    namespace
    {
        bool HasEvent(const std::vector<CombatEvent>& events, CombatEventType type)
        {
            for (const auto& ev : events)
                if (ev.type == type)
                    return true;
            return false;
        }

        float Abs(float v) { return (v < 0.0f) ? -v : v; }
    }

    void ActionFsm::Reset()
    {
        m_state = ActionState::Idle;
        m_stateTime = 0.0f;
        m_attackCommitted = false;
        m_prevHitActive = false;
    }

    void ActionFsm::Enter(ActionState next)
    {
        if (m_state != next)
        {
            m_state = next;
            m_stateTime = 0.0f;
            if (m_state != ActionState::Attack)
                m_attackCommitted = false;
            else
                m_attackCommitted = false;
            m_prevHitActive = false;
        }
    }

    FsmOutput ActionFsm::Update(EntityId self,
                                const Intent& intent,
                                const Sensors& sensors,
                                const std::vector<CombatEvent>& events,
                                float dtSec)
    {
        FsmOutput out{};

        m_stateTime += dtSec;

        if (sensors.hp <= 0.0f || HasEvent(events, CombatEventType::OnDeath))
        {
            Enter(ActionState::Dead);
        }

        if (HasEvent(events, CombatEventType::OnGroggy) && m_state != ActionState::Dead)
        {
            Enter(ActionState::Groggy);
        }

        if (HasEvent(events, CombatEventType::OnHit) && m_state != ActionState::Dead)
        {
            if (sensors.canBeHitstunned)
                Enter(ActionState::Hitstun);
        }

        if (m_state != ActionState::Dead && m_state != ActionState::Hitstun && m_state != ActionState::Groggy)
        {
            const bool hasMove = (Abs(intent.move.x) + Abs(intent.move.y)) > 0.001f;

            if (m_state == ActionState::Attack)
            {
                if (sensors.attackStateDurationSec > 0.0f)
                {
                    if (m_stateTime >= sensors.attackStateDurationSec)
                    {
                        if (hasMove)
                        {
                            Enter(ActionState::Move);
                            out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, intent.move, sensors.moveSpeed, true, true } });
                        }
                        else
                        {
                            Enter(ActionState::Idle);
                            out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
                        }
                    }
                }
                else if (!m_attackCommitted)
                {
                    if (sensors.attackWindowActive)
                        m_attackCommitted = true;
                    else
                    {
                        if (intent.dodgePressed && sensors.stamina >= 10.0f)
                        {
                            Enter(ActionState::Dodge);
                        }
                        else if (intent.guardHeld)
                        {
                            Enter(ActionState::Guard);
                        }
                        else if (hasMove)
                        {
                            Enter(ActionState::Move);
                            out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, intent.move, sensors.moveSpeed, true, true } });
                        }
                        else
                        {
                            Enter(ActionState::Idle);
                            out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
                        }
                    }
                }
                else
                {
                    if (!sensors.attackWindowActive && m_stateTime > 0.05f)
                        Enter(ActionState::Idle);
                }
            }
            else if (intent.dodgePressed && sensors.stamina >= 10.0f)
            {
                Enter(ActionState::Dodge);
            }
            else if (intent.guardHeld)
            {
                if (m_state != ActionState::Guard)
                {
                    Enter(ActionState::Guard);
                }
            }
            else if (intent.attackPressed && sensors.stamina >= 15.0f)
            {
                Enter(ActionState::Attack);
            }
            else
            {
                if (hasMove)
                {
                    Enter(ActionState::Move);
                    out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, intent.move, sensors.moveSpeed, true, true } });
                }
                else
                {
                    Enter(ActionState::Idle);
                    out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
                }
            }
        }

        ActionFlags flags{};
        // Flags are pass-through windows from sensors (single source of truth).
        flags.hitActive = sensors.attackWindowActive;
        flags.guardActive = sensors.guardWindowActive;
        flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
        flags.parryWindowActive = false;
        flags.canBeInterrupted = (m_state != ActionState::Dodge) && (m_state != ActionState::Dead) && (m_state != ActionState::Groggy);

        if (m_state == ActionState::Hitstun)
        {
            flags.canBeInterrupted = false;
            if (m_stateTime > 0.4f)
                Enter(ActionState::Idle);
        }

        if (m_state == ActionState::Groggy)
        {
            flags.canBeInterrupted = false;
            if (m_stateTime > sensors.groggyDuration)
                Enter(ActionState::Idle);
        }

        if (flags.hitActive != m_prevHitActive)
        {
            if (flags.hitActive)
                out.commands.push_back({ CommandType::EnableTrace, CmdEnableTrace{ self } });
            else
                out.commands.push_back({ CommandType::DisableTrace, CmdDisableTrace{ self } });
            m_prevHitActive = flags.hitActive;
        }

        out.state = m_state;
        out.flags = flags;
        return out;
    }
}
