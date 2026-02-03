#include "C_CombatApply.h"

#include <algorithm>
#include <cmath>

#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
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
            case CommandType::ConsumeWeaponDurability:
            {
                auto p = std::get<CmdConsumeWeaponDurability>(cmd.payload);
                if (auto it = fighters.find(p.target); it != fighters.end())
                    it->second->weaponDurability = std::max(0.0f, it->second->weaponDurability - p.amount);
                if (auto* hc = world.GetComponent<HealthComponent>(p.target))
                    hc->weaponDurability = std::max(0.0f, hc->weaponDurability - p.amount);
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
            case CommandType::StartGuardLock:
            {
                auto p = std::get<CmdStartGuardLock>(cmd.payload);
                if (auto* driver = world.GetComponent<AttackDriverComponent>(p.target))
                {
                    if (p.durationSec > 0.0f)
                        driver->guardLockRemainingSec = std::max(driver->guardLockRemainingSec, p.durationSec);
                }
                break;
            }
            case CommandType::ConsumeParry:
            {
                auto p = std::get<CmdConsumeParry>(cmd.payload);
                if (auto* driver = world.GetComponent<AttackDriverComponent>(p.target))
                {
                    driver->parryUsedThisPress = true;
                    driver->parryOverrideRemainingSec = 0.0f;
                }
                break;
            }
            case CommandType::AddGroggy:
            {
                auto p = std::get<CmdAddGroggy>(cmd.payload);
                if (auto* hc = world.GetComponent<HealthComponent>(p.target))
                {
                    if (hc->groggyMax > 0.0f && p.amount > 0.0f)
                    {
                        if (hc->groggy >= hc->groggyMax)
                            break;

                        const float prev = hc->groggy;
                        hc->groggy = std::min(hc->groggy + p.amount, hc->groggyMax);
                        if (prev < hc->groggyMax && hc->groggy >= hc->groggyMax)
                        {
                            hc->groggy = hc->groggyMax;
                            if (auto* driver = world.GetComponent<AttackDriverComponent>(p.target))
                            {
                                if (driver->attackCancelable)
                                    driver->cancelAttackRequested = true;
                            }
                            EntityId traceId = ResolveTraceEntity(world, p.target);
                            if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                                trace->active = false;

                            bus.PushDeferred({ CombatEventType::OnGroggy, p.target, InvalidEntityId, 0, 0.0f });
                        }
                    }
                }
                break;
            }
            case CommandType::EnterWeakState:
            {
                auto p = std::get<CmdEnterWeakState>(cmd.payload);
                if (auto* hc = world.GetComponent<HealthComponent>(p.target))
                {
                    if (p.durationSec > 0.0f)
                        hc->weakRemainingSec = std::max(hc->weakRemainingSec, p.durationSec);
                }
                if (auto* driver = world.GetComponent<AttackDriverComponent>(p.target))
                {
                    driver->guardLockRemainingSec = 0.0f;
                    driver->parryOverrideRemainingSec = 0.0f;
                    driver->parryUsedThisPress = false;
                }
                break;
            }
            case CommandType::ApplyPushback:
            {
                auto p = std::get<CmdApplyPushback>(cmd.payload);
                if (p.durationSec <= 0.0f || p.speed <= 0.0f)
                    break;

                auto apply = [&](EntityId target, const DirectX::XMFLOAT3& dir) {
                    if (auto* hc = world.GetComponent<HealthComponent>(target))
                    {
                        hc->pushbackRemainingSec = std::max(hc->pushbackRemainingSec, p.durationSec);
                        hc->pushbackDir = dir;
                        hc->pushbackSpeed = p.speed;
                    }
                };

                DirectX::XMFLOAT3 dir{ 0.0f, 0.0f, 0.0f };
                if (auto* victimTr = world.GetComponent<TransformComponent>(p.victim))
                {
                    if (auto* attackerTr = world.GetComponent<TransformComponent>(p.attacker))
                    {
                        const float dx = victimTr->position.x - attackerTr->position.x;
                        const float dz = victimTr->position.z - attackerTr->position.z;
                        const float len = std::sqrt(dx * dx + dz * dz);
                        if (len > 0.0001f)
                        {
                            dir.x = dx / len;
                            dir.z = dz / len;
                        }
                    }
                }

                apply(p.victim, dir);
                break;
            }
            case CommandType::ApplyPushbackToBoth:
            {
                auto p = std::get<CmdApplyPushbackToBoth>(cmd.payload);
                if (p.durationSec <= 0.0f || p.speed <= 0.0f)
                    break;

                auto apply = [&](EntityId target, const DirectX::XMFLOAT3& dir) {
                    if (auto* hc = world.GetComponent<HealthComponent>(target))
                    {
                        hc->pushbackRemainingSec = std::max(hc->pushbackRemainingSec, p.durationSec);
                        hc->pushbackDir = dir;
                        hc->pushbackSpeed = p.speed;
                    }
                };

                DirectX::XMFLOAT3 dir{ 0.0f, 0.0f, 0.0f };
                if (auto* victimTr = world.GetComponent<TransformComponent>(p.victim))
                {
                    if (auto* attackerTr = world.GetComponent<TransformComponent>(p.attacker))
                    {
                        const float dx = victimTr->position.x - attackerTr->position.x;
                        const float dz = victimTr->position.z - attackerTr->position.z;
                        const float len = std::sqrt(dx * dx + dz * dz);
                        if (len > 0.0001f)
                        {
                            dir.x = dx / len;
                            dir.z = dz / len;
                        }
                    }
                }

                apply(p.victim, dir);
                apply(p.attacker, DirectX::XMFLOAT3{ -dir.x, 0.0f, -dir.z });
                break;
            }
            default:
                break;
            }
        }
    }
}
