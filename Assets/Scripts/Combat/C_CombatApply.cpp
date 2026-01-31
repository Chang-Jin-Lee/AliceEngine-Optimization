#include "C_CombatApply.h"

#include <algorithm>

#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "C_CombatEventBus.h"
#include "C_Fighter.h"

namespace Alice::Combat
{
    namespace
    {
        EntityId ResolveTraceEntity(World& world, EntityId ownerOrWeapon)
        {
            if (world.GetComponent<WeaponTraceComponent>(ownerOrWeapon))
                return ownerOrWeapon;

            auto* driver = world.GetComponent<AttackDriverComponent>(ownerOrWeapon);
            if (!driver || driver->traceGuid == 0)
                return ownerOrWeapon;

            EntityId resolved = world.FindEntityByGuid(driver->traceGuid);
            return (resolved != InvalidEntityId) ? resolved : ownerOrWeapon;
        }
    }

    void CombatApply::ApplyImmediate(World& world,
                                     std::unordered_map<EntityId, Fighter*>& fighters,
                                     CombatEventBus& bus,
                                     const std::vector<Command>& cmds,
                                     bool skipDamage)
    {
        for (const auto& cmd : cmds)
        {
            switch (cmd.type)
            {
            case CommandType::ApplyDamage:
            {
                if (skipDamage)
                    break;

                auto p = std::get<CmdApplyDamage>(cmd.payload);
                if (auto it = fighters.find(p.target); it != fighters.end())
                {
                    it->second->hp -= p.amount;
                    if (auto* hc = world.GetComponent<HealthComponent>(p.target))
                    {
                        hc->currentHealth -= p.amount;
                        if (hc->currentHealth <= 0.0f)
                        {
                            hc->currentHealth = 0.0f;
                            hc->alive = false;
                            bus.PushDeferred({ CombatEventType::OnDeath, p.target, InvalidEntityId, 0, 0.0f });
                        }

                        if (hc->invulnDuration > 0.0f)
                            hc->invulnRemaining = hc->invulnDuration;
                    }
                }
                break;
            }
            case CommandType::ConsumeStamina:
            {
                auto p = std::get<CmdConsumeStamina>(cmd.payload);
                if (auto it = fighters.find(p.target); it != fighters.end())
                    it->second->stamina = std::max(0.0f, it->second->stamina - p.amount);
                break;
            }
            case CommandType::ForceCancelAttack:
            {
                auto p = std::get<CmdForceCancelAttack>(cmd.payload);
                if (auto* driver = world.GetComponent<AttackDriverComponent>(p.target))
                {
                    if (driver->attackCancelable)
                        driver->cancelAttackRequested = true;
                }
                EntityId traceId = ResolveTraceEntity(world, p.target);
                if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                    trace->active = false;
                break;
            }
            case CommandType::DisableTrace:
            {
                auto p = std::get<CmdDisableTrace>(cmd.payload);
                EntityId traceId = ResolveTraceEntity(world, p.weaponOrOwner);
                if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                    trace->active = false;
                break;
            }
            case CommandType::EnableTrace:
            {
                auto p = std::get<CmdEnableTrace>(cmd.payload);
                EntityId traceId = ResolveTraceEntity(world, p.weaponOrOwner);
                if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                {
                    if (!trace->active)
                    {
                        trace->attackInstanceId++;
                        trace->active = true;
                        trace->hasPrevBasis = false;
                        trace->hasPrevShapes = false;
                        trace->prevCentersWS.clear();
                        trace->prevRotsWS.clear();
                        trace->hitVictims.clear();
                        trace->lastAttackInstanceId = trace->attackInstanceId;
                    }
                }
                break;
            }
            case CommandType::EnterHitstun:
                // TODO: hitstun timer/state hook (anim/driver cancel)
                break;
            default:
                break;
            }
        }
    }
}
