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
        m_prevHitActive = false;
        m_attackWindowSeen = false;
        m_lastMoveDir = {};
        m_lastMoveValid = false;
        m_dodgeDir = {};
        m_dodgeDirValid = false;
        m_dodgeMoveTimer = 0.0f;
        m_dodgeMoveStopped = false;
    }

    void ActionFsm::Enter(ActionState next, bool force)
    {
        if (m_state != next || force)
        {
            m_state = next;
            m_stateTime = 0.0f;
            m_prevHitActive = false;
            m_attackWindowSeen = false;
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

        if (HasEvent(events, CombatEventType::OnGuardBreak) && m_state != ActionState::Dead)
        {
            Enter(ActionState::GuardBreakWeak);
        }
        else if (HasEvent(events, CombatEventType::OnParrySuccess) && m_state != ActionState::Dead)
        {
            Enter(ActionState::JustGuardSuccess);
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

        const bool hasMove = (Abs(intent.move.x) + Abs(intent.move.y)) > 0.001f;
        const bool wantsGuard = (intent.guardHeld || sensors.guardLockActive) && !sensors.weakActive;

        auto EnterIdleOrMove = [&]() {
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
        };

        if (m_state == ActionState::JustGuardSuccess)
        {
            if (m_stateTime >= m_justGuardDurationSec)
            {
                if (wantsGuard)
                    Enter(ActionState::Guard);
                else
                    EnterIdleOrMove();
            }
        }

        if (m_state == ActionState::GuardBreakWeak)
        {
            if (sensors.weakRemainingSec <= 0.0f)
            {
                if (wantsGuard)
                    Enter(ActionState::Guard);
                else
                    EnterIdleOrMove();
            }
        }

        if (m_state != ActionState::Dead
            && m_state != ActionState::Hitstun
            && m_state != ActionState::Groggy
            && m_state != ActionState::GuardBreakWeak
            && m_state != ActionState::JustGuardSuccess)
        {
            const bool wantsAttack = intent.lightAttackPressed
                || intent.heavyAttackPressed
                || (intent.attackPressed && !intent.attackHeld);
            auto Normalize = [](const Vec2& v, Vec2& out) -> bool {
                const float len = std::sqrt(v.x * v.x + v.y * v.y);
                if (len < 0.0001f)
                    return false;
                out.x = v.x / len;
                out.y = v.y / len;
                return true;
            };
            Vec2 moveDir{};
            const bool moveDirValid = Normalize(intent.move, moveDir);
            if (moveDirValid)
            {
                m_lastMoveDir = moveDir;
                m_lastMoveValid = true;
            }

            auto BeginDodge = [&]() {
                Enter(ActionState::Dodge);
                m_dodgeMoveTimer = 0.0f;
                m_dodgeMoveStopped = false;
                m_dodgeDirValid = Normalize(intent.move, m_dodgeDir);
                if (!m_dodgeDirValid && m_lastMoveValid)
                {
                    m_dodgeDir = m_lastMoveDir;
                    m_dodgeDirValid = true;
                }
                const float moveDuration = (m_dodgeMoveDurationSec > 0.0f) ? m_dodgeMoveDurationSec : 0.0f;
                const float dodgeSpeed = (moveDuration > 0.0f)
                    ? (m_dodgeDistance / moveDuration)
                    : sensors.moveSpeed;
                const Vec2 move = m_dodgeDirValid ? m_dodgeDir : Vec2{ 0.0f, 0.0f };
                out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, move, dodgeSpeed, true, true } });
            };

            if (m_state == ActionState::Dodge)
            {
                m_dodgeMoveTimer += dtSec;
                if (!m_dodgeMoveStopped && m_dodgeMoveTimer >= m_dodgeMoveDurationSec)
                {
                    m_dodgeMoveStopped = true;
                    out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
                }
                if (m_stateTime >= m_dodgeDurationSec)
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
            else if (m_state == ActionState::Attack)
            {
                if (sensors.attackWindowActive)
                    m_attackWindowSeen = true;

                const float attackDuration = (sensors.attackStateDurationSec > 0.0f)
                    ? sensors.attackStateDurationSec
                    : m_attackFallbackDurationSec;
                const bool attackFinished = (attackDuration > 0.0f) && (m_stateTime >= attackDuration);
                const bool preWindow = !sensors.attackWindowActive && !m_attackWindowSeen;
                const bool postWindow = !sensors.attackWindowActive && m_attackWindowSeen;
                const bool canGuardCancel = preWindow
                    && sensors.attackCancelable
                    && wantsGuard;
                const bool canDodgeCancel = postWindow
                    && sensors.attackCancelable
                    && intent.dodgePressed
                    && sensors.stamina >= 10.0f;
                const float restartLateRatio = 0.7f;
                const float restartStartSec = std::max(0.0f, attackDuration * restartLateRatio);
                const bool lateWindow = m_attackWindowSeen && (m_stateTime >= restartStartSec);
                const bool canRestartAttack = (postWindow || lateWindow)
                    && sensors.attackCancelable
                    && intent.lightAttackPressed
                    && sensors.stamina >= 15.0f;

                if (canRestartAttack)
                {
                    Enter(ActionState::Attack, true);
                    out.attackRestarted = true;
                }
                else if (canDodgeCancel)
                {
                    BeginDodge();
                }
                else if (canGuardCancel)
                {
                    Enter(ActionState::Guard);
                }
                else if (attackFinished)
                {
                    if (wantsGuard)
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
            else if (intent.dodgePressed && sensors.stamina >= 10.0f)
            {
                BeginDodge();
            }
            else if (wantsGuard)
            {
                if (m_state != ActionState::Guard)
                {
                    Enter(ActionState::Guard);
                }
            }
            else if (wantsAttack && sensors.stamina >= 15.0f)
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
        flags.guardActive = sensors.guardWindowActive && !sensors.weakActive;
        flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
        flags.parryWindowActive = sensors.parryWindowActive && !sensors.weakActive;
        flags.canBeInterrupted = (m_state != ActionState::Dodge)
            && (m_state != ActionState::Dead)
            && (m_state != ActionState::Groggy)
            && (m_state != ActionState::GuardBreakWeak)
            && (m_state != ActionState::JustGuardSuccess);

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
