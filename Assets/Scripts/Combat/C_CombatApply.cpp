#include "C_CombatApply.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "C_CombatEventBus.h"
#include "C_Fighter.h"

namespace Alice::Combat
{
    namespace
    {
        std::uint32_t GetDriverTraceSlotCount(const AttackDriverComponent& driver)
        {
            const std::size_t total = std::size_t(1) + driver.traceGuids.size();
            return static_cast<std::uint32_t>(std::min<std::size_t>(total, 32));
        }

        EntityId ResolveDriverTraceSlot(World& world, AttackDriverComponent& driver, EntityId owner, std::uint32_t slotIndex)
        {
            if (slotIndex == 0u)
            {
                if (driver.traceGuid == 0)
                {
                    driver.traceCached = InvalidEntityId;
                    return owner;
                }

                if (driver.traceCached != InvalidEntityId)
                {
                    if (const auto* idc = world.GetComponent<IDComponent>(driver.traceCached))
                    {
                        if (idc->guid == driver.traceGuid)
                            return driver.traceCached;
                    }
                    driver.traceCached = InvalidEntityId;
                }

                EntityId resolved = world.FindEntityByGuid(driver.traceGuid);
                if (resolved != InvalidEntityId)
                    driver.traceCached = resolved;
                return (resolved != InvalidEntityId) ? resolved : owner;
            }

            const std::uint32_t extraIndex = slotIndex - 1u;
            if (extraIndex >= driver.traceGuids.size())
                return InvalidEntityId;

            const std::uint64_t guid = driver.traceGuids[extraIndex];
            if (guid == 0)
                return InvalidEntityId;

            if (driver.traceCachedList.size() != driver.traceGuids.size())
                driver.traceCachedList.assign(driver.traceGuids.size(), InvalidEntityId);

            EntityId& cached = driver.traceCachedList[extraIndex];
            if (cached != InvalidEntityId)
            {
                if (const auto* idc = world.GetComponent<IDComponent>(cached))
                {
                    if (idc->guid == guid)
                        return cached;
                }
                cached = InvalidEntityId;
            }

            EntityId resolved = world.FindEntityByGuid(guid);
            if (resolved != InvalidEntityId)
                cached = resolved;
            return resolved;
        }

        template <typename Fn>
        void ForEachTraceEntity(World& world, EntityId ownerOrWeapon, std::uint32_t slotMask, Fn&& fn)
        {
            if (world.GetComponent<WeaponTraceComponent>(ownerOrWeapon))
            {
                fn(ownerOrWeapon);
                return;
            }

            auto* driver = world.GetComponent<AttackDriverComponent>(ownerOrWeapon);
            if (!driver)
            {
                fn(ownerOrWeapon);
                return;
            }

            const std::uint32_t slotCount = GetDriverTraceSlotCount(*driver);
            if (slotCount == 0)
                return;

            const bool allSlots = (slotMask == 0u);
            std::vector<EntityId> visited;
            visited.reserve(slotCount);
            for (std::uint32_t slot = 0; slot < slotCount; ++slot)
            {
                if (!allSlots)
                {
                    const std::uint32_t bit = (1u << slot);
                    if ((slotMask & bit) == 0u)
                        continue;
                }

                EntityId traceId = ResolveDriverTraceSlot(world, *driver, ownerOrWeapon, slot);
                if (traceId == InvalidEntityId)
                    continue;
                if (std::find(visited.begin(), visited.end(), traceId) != visited.end())
                    continue;
                visited.push_back(traceId);
                fn(traceId);
            }
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
                    driver->cancelAttackRequested = true;
                }
                ForEachTraceEntity(world, p.target, 0u, [&](EntityId traceId) {
                    if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                    {
                        trace->active = false;
                        trace->activeElapsedSec = 0.0f;
                        trace->activeWindowDurationSec = 0.0f;
                    }
                });
                break;
            }
            case CommandType::DisableTrace:
            {
                auto p = std::get<CmdDisableTrace>(cmd.payload);
                ForEachTraceEntity(world, p.weaponOrOwner, 0u, [&](EntityId traceId) {
                    if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                    {
                        trace->active = false;
                        trace->activeElapsedSec = 0.0f;
                        trace->activeWindowDurationSec = 0.0f;
                    }
                });
                break;
            }
            case CommandType::EnableTrace:
            {
                auto p = std::get<CmdEnableTrace>(cmd.payload);
                std::uint32_t traceMask = p.traceSlotMask;
                float windowDurationSec = std::max(0.0f, p.activeWindowDurationSec);
                if (auto* driver = world.GetComponent<AttackDriverComponent>(p.weaponOrOwner))
                {
                    if (traceMask == 0u)
                        traceMask = driver->attackTraceMaskActive;
                    if (windowDurationSec <= 0.0f)
                        windowDurationSec = std::max(0.0f, driver->attackTraceWindowDurationSec);
                }
                ForEachTraceEntity(world, p.weaponOrOwner, traceMask, [&](EntityId traceId) {
                    if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                    {
                        trace->attackInstanceId++;
                        trace->active = true;
                        trace->hasPrevBasis = false;
                        trace->hasPrevShapes = false;
                        trace->prevCentersWS.clear();
                        trace->prevRotsWS.clear();
                        trace->hitVictims.clear();
                        trace->lastAttackInstanceId = trace->attackInstanceId;
                        trace->activeElapsedSec = 0.0f;
                        trace->activeWindowDurationSec = windowDurationSec;
                    }
                });
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
                                driver->forceCancelRequested = true;
                                driver->cancelAttackRequested = true;
                            }
                            ForEachTraceEntity(world, p.target, 0u, [&](EntityId traceId) {
                                if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                                {
                                    trace->active = false;
                                    trace->activeElapsedSec = 0.0f;
                                    trace->activeWindowDurationSec = 0.0f;
                                }
                            });

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
