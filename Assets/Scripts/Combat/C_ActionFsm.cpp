#include "C_ActionFsm.h"

#include <cmath>
#include <algorithm>

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
        m_parrySuccessLatched = false;
        m_guardReentryRequiresRelease = false;
    }

    void ActionFsm::Enter(ActionState next, bool force)
    {
        if (m_state != next || force)
        {
            if (next == ActionState::Guard)
                m_parrySuccessLatched = false;
            else if (m_state == ActionState::Guard)
                m_parrySuccessLatched = false;
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

        const bool guardBreak = HasEvent(events, CombatEventType::OnGuardBreak) && m_state != ActionState::Dead;
        if (guardBreak)
        {
            Enter(ActionState::GuardBreakWeak);
        }
        const bool groggyEvent = HasEvent(events, CombatEventType::OnGroggy) && m_state != ActionState::Dead;
        if (groggyEvent)
        {
            Enter(ActionState::Groggy);
        }
        const bool gotHit = HasEvent(events, CombatEventType::OnHit)
            && m_state != ActionState::Dead
            && !groggyEvent
            && m_state != ActionState::Groggy;

        if (gotHit)
        {
            if (sensors.canBeHitstunned)
                Enter(ActionState::Hitstun);
        }

        float guardEnterDurationSec = std::max(0.0f, sensors.guardEnterDurationSec);
        if (guardEnterDurationSec <= 0.0f)
            guardEnterDurationSec = std::max(0.0f, intent.parryTapWindowSec);

        if (m_guardReentryRequiresRelease && !intent.guardHeld && !intent.guardPressed)
            m_guardReentryRequiresRelease = false;
        const bool guardHeldInput = !m_guardReentryRequiresRelease && intent.guardHeld;
        const bool guardPressedInput = !m_guardReentryRequiresRelease && intent.guardPressed;

        const bool parrySuccess = HasEvent(events, CombatEventType::OnParrySuccess);
        if (parrySuccess && m_state == ActionState::Guard)
            m_parrySuccessLatched = true;

        const bool hasMove = (Abs(intent.move.x) + Abs(intent.move.y)) > 0.001f;
        if (m_state == ActionState::Guard && guardPressedInput && !sensors.weakActive)
        {
            Enter(ActionState::Guard, true);
        }
        const bool guardEnterActive = (m_state == ActionState::Guard)
            && (guardEnterDurationSec > 0.0f)
            && (m_stateTime < guardEnterDurationSec);
        const bool wantsGuard = ((guardHeldInput || guardPressedInput || sensors.guardLockActive) || guardEnterActive)
            && !sensors.weakActive;
        const bool wantsInteract = intent.interactPressed && sensors.interactAvailable;
        const bool wantsHealStart = intent.itemPressed && sensors.healAllowed;
        const bool healHeld = intent.itemHeld && sensors.healAllowed;

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
            if (!m_dodgeDirValid && sensors.dodgeFallbackValid)
            {
                m_dodgeDirValid = Normalize(sensors.dodgeFallbackDir, m_dodgeDir);
            }
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

        const float interactionDuration = (sensors.interactionDurationSec > 0.0f) ? sensors.interactionDurationSec : 0.5f;
        const float healEnterDuration = (sensors.healEnterDurationSec > 0.0f) ? sensors.healEnterDurationSec : interactionDuration;
        const float healExitDuration = (sensors.healExitDurationSec > 0.0f) ? sensors.healExitDurationSec : interactionDuration;

        if (m_state == ActionState::Interaction)
        {
            if (m_stateTime >= interactionDuration)
                EnterIdleOrMove();
        }
        else if (m_state == ActionState::HealEnter)
        {
            if (intent.dodgePressed && sensors.stamina >= 10.0f)
            {
                BeginDodge();
            }
            else if (m_stateTime >= healEnterDuration)
            {
                if (healHeld)
                    Enter(ActionState::HealLoop);
                else
                    Enter(ActionState::HealExit);
            }
        }
        else if (m_state == ActionState::HealLoop)
        {
            if (!healHeld)
                Enter(ActionState::HealExit);
        }
        else if (m_state == ActionState::HealExit)
        {
            if (intent.dodgePressed && sensors.stamina >= 10.0f)
            {
                BeginDodge();
            }
            else if (m_stateTime >= healExitDuration)
            {
                EnterIdleOrMove();
            }
        }

        if (m_state != ActionState::Dead
            && m_state != ActionState::Hitstun
            && m_state != ActionState::Groggy
            && m_state != ActionState::GuardBreakWeak
            && m_state != ActionState::Interaction
            && m_state != ActionState::HealEnter
            && m_state != ActionState::HealLoop
            && m_state != ActionState::HealExit)
        {
            const bool wantsAttack = intent.lightAttackPressed
                || intent.heavyAttackPressed
                || (intent.attackPressed && !intent.attackHeld);
            const bool canStartInteraction = (m_state == ActionState::Idle
                || m_state == ActionState::Move
                || m_state == ActionState::Guard);
            const bool canStartHeal = canStartInteraction;

            if (wantsInteract && canStartInteraction)
            {
                Enter(ActionState::Interaction);
                out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
            }
            else if (wantsHealStart && canStartHeal)
            {
                Enter(ActionState::HealEnter);
                out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
            }
            else if (m_state == ActionState::Dodge)
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
            else if (m_state == ActionState::Guard
                && m_parrySuccessLatched
                && !guardEnterActive)
            {
                Enter(ActionState::Idle);
                m_guardReentryRequiresRelease = true;
                out.parryRecoverToIdle = true;
                out.commands.push_back({ CommandType::RequestMove, CmdRequestMove{ self, {0.0f, 0.0f}, 0.0f, true, false } });
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
        // Guard/Parry are sequential phases:
        // - GuardEnter (parry) phase: parry window active, guard window disabled.
        // - GuardLoop phase: guard window active, parry window disabled.
        flags.guardActive = (m_state == ActionState::Guard)
            && !guardEnterActive
            && sensors.guardWindowActive
            && !sensors.weakActive;
        flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
        flags.parryWindowActive = (m_state == ActionState::Guard)
            && guardEnterActive
            && !sensors.weakActive;
        flags.canBeInterrupted = (m_state != ActionState::Dodge)
            && (m_state != ActionState::Dead)
            && (m_state != ActionState::Groggy)
            && (m_state != ActionState::GuardBreakWeak);

        if (m_state == ActionState::Interaction)
        {
            flags.hitActive = false;
            flags.guardActive = false;
            flags.parryWindowActive = false;
            flags.invulnActive = true;
            flags.canBeInterrupted = false;
        }
        else if (m_state == ActionState::HealEnter
            || m_state == ActionState::HealLoop
            || m_state == ActionState::HealExit)
        {
            flags.hitActive = false;
            flags.guardActive = false;
            flags.parryWindowActive = false;
        }

        if (m_state == ActionState::Hitstun)
        {
            flags.canBeInterrupted = false;
            const float hitstunDuration = (sensors.hitstunDurationSec > 0.0f)
                ? sensors.hitstunDurationSec
                : 0.4f;
            if (m_stateTime > hitstunDuration)
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
