#include "C_CombatResolver.h"

namespace Alice::Combat
{
    ResolveOutput CombatResolver::ResolveBatch(const std::vector<HitEvent>& hits,
                                               const FighterSnapshot& attackerSnap,
                                               const FighterSnapshot& victimSnap) const
    {
        ResolveOutput out{};
        for (const auto& h : hits)
        {
            auto one = ResolveOneDetailed(h, attackerSnap, victimSnap).output;
            out.immediate.insert(out.immediate.end(), one.immediate.begin(), one.immediate.end());
            out.deferred.insert(out.deferred.end(), one.deferred.begin(), one.deferred.end());
        }
        return out;
    }

    ResolveOutput CombatResolver::ResolveOne(const HitEvent& hit,
                                             const FighterSnapshot& attacker,
                                             const FighterSnapshot& victim) const
    {
        return ResolveOneDetailed(hit, attacker, victim).output;
    }

    ResolveDetail CombatResolver::ResolveOneDetailed(const HitEvent& hit,
                                                     const FighterSnapshot& attacker,
                                                     const FighterSnapshot& victim) const
    {
        ResolveDetail detail{};
        ResolveOutput& out = detail.output;
        if (hit.victimOwner != victim.id)
            return detail;

        if (victim.flags.invulnActive)
            return detail;

        const bool targetInFront = victim.targetInFront;
        const bool weakActive = victim.weakActive;

        if (victim.flags.parryWindowActive && targetInFront && !weakActive)
        {
            detail.result = ResolveResult::Parry;
            out.deferred.push_back({ CombatEventType::OnParrySuccess, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            out.deferred.push_back({ CombatEventType::OnGotParried, attacker.id, victim.id, hit.attackInstanceId, 0.0f });
            out.immediate.push_back({ CommandType::ConsumeParry, CmdConsumeParry{ victim.id } });
            if (hit.parryLockSec > 0.0f)
                out.immediate.push_back({ CommandType::StartGuardLock, CmdStartGuardLock{ victim.id, hit.parryLockSec } });
            if (hit.parryGroggyGain > 0.0f)
                out.immediate.push_back({ CommandType::AddGroggy, CmdAddGroggy{ attacker.id, hit.parryGroggyGain } });
            out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ attacker.id } });
            if (attacker.flags.canBeInterrupted)
                out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ attacker.id } });
            return detail;
        }

        if (victim.flags.guardActive && targetInFront && !weakActive)
        {
            const float guardCost = (hit.guardDurabilityCost > 0.0f)
                ? hit.guardDurabilityCost
                : ((hit.damage > 0.0f) ? hit.damage : 0.0f);
            if (guardCost > 0.0f)
                out.immediate.push_back({ CommandType::ConsumeWeaponDurability, CmdConsumeWeaponDurability{ victim.id, guardCost } });

            if (victim.weaponDurability - guardCost <= 0.0f)
            {
                detail.result = ResolveResult::GuardBreak;
                out.deferred.push_back({ CombatEventType::OnGuardBreak, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
                if (hit.guardBreakPushbackSpeed > 0.0f && hit.guardBreakPushbackDuration > 0.0f)
                {
                    out.immediate.push_back({ CommandType::ApplyPushbackToBoth,
                        CmdApplyPushbackToBoth{ attacker.id, victim.id, hit.guardBreakPushbackSpeed, hit.guardBreakPushbackDuration } });
                }
                if (hit.guardBreakWeakSec > 0.0f)
                    out.immediate.push_back({ CommandType::EnterWeakState, CmdEnterWeakState{ victim.id, hit.guardBreakWeakSec } });
                if (victim.flags.canBeInterrupted && victim.canBeHitstunned)
                {
                    out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ victim.id } });
                    out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ victim.id } });
                }
            }
            else
            {
                if (hit.guardLockSec > 0.0f)
                    out.immediate.push_back({ CommandType::StartGuardLock, CmdStartGuardLock{ victim.id, hit.guardLockSec } });
                detail.result = ResolveResult::Guard;
                out.deferred.push_back({ CombatEventType::OnGuarded, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            }
            return detail;
        }

        detail.result = ResolveResult::Hit;
        out.immediate.push_back({ CommandType::ApplyDamage, CmdApplyDamage{ victim.id, hit.damage } });
        if (victim.flags.canBeInterrupted && victim.canBeHitstunned)
        {
            out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ victim.id } });
            out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ victim.id } });
        }
        out.deferred.push_back({ CombatEventType::OnHit, victim.id, attacker.id, hit.attackInstanceId, hit.damage });
        return detail;
    }
}
