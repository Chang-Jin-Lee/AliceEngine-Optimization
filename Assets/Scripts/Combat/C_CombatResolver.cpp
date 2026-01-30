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
            auto one = ResolveOne(h, attackerSnap, victimSnap);
            out.immediate.insert(out.immediate.end(), one.immediate.begin(), one.immediate.end());
            out.deferred.insert(out.deferred.end(), one.deferred.begin(), one.deferred.end());
        }
        return out;
    }

    ResolveOutput CombatResolver::ResolveOne(const HitEvent& hit,
                                             const FighterSnapshot& attacker,
                                             const FighterSnapshot& victim) const
    {
        ResolveOutput out{};
        if (hit.victimOwner != victim.id)
            return out;

        if (victim.flags.invulnActive)
            return out;

        const bool targetInFront = victim.targetInFront;

        if (victim.flags.parryWindowActive && targetInFront)
        {
            out.deferred.push_back({ CombatEventType::OnParried, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ attacker.id } });
            if (attacker.flags.canBeInterrupted)
                out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ attacker.id } });
            return out;
        }

        if (victim.flags.guardActive && targetInFront)
        {
            const float staminaCost = (hit.damage > 0.0f) ? hit.damage : 0.0f;
            if (staminaCost > 0.0f)
                out.immediate.push_back({ CommandType::ConsumeStamina, CmdConsumeStamina{ victim.id, staminaCost } });

            if (victim.stamina - staminaCost <= 0.0f)
            {
                out.deferred.push_back({ CombatEventType::OnGuardBreak, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
                out.immediate.push_back({ CommandType::ApplyDamage, CmdApplyDamage{ victim.id, hit.damage } });
                if (victim.flags.canBeInterrupted && victim.canBeHitstunned)
                {
                    out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ victim.id } });
                    out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ victim.id } });
                }
                out.deferred.push_back({ CombatEventType::OnHit, victim.id, attacker.id, hit.attackInstanceId, hit.damage });
            }
            else
            {
                out.deferred.push_back({ CombatEventType::OnGuarded, victim.id, attacker.id, hit.attackInstanceId, 0.0f });
            }
            return out;
        }

        out.immediate.push_back({ CommandType::ApplyDamage, CmdApplyDamage{ victim.id, hit.damage } });
        if (victim.flags.canBeInterrupted && victim.canBeHitstunned)
        {
            out.immediate.push_back({ CommandType::ForceCancelAttack, CmdForceCancelAttack{ victim.id } });
            out.immediate.push_back({ CommandType::DisableTrace, CmdDisableTrace{ victim.id } });
        }
        out.deferred.push_back({ CombatEventType::OnHit, victim.id, attacker.id, hit.attackInstanceId, hit.damage });
        return out;
    }
}
