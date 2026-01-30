#include "Game/CombatSystem.h"

#include <algorithm>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Components/HealthComponent.h"
#include "Game/CombatHitEvent.h"

namespace Alice
{
    void CombatSystem::BeginFrame(World& world)
    {
        for (auto&& [id, health] : world.GetComponents<HealthComponent>())
        {
            (void)id;
            health.hitThisFrame = false;
            health.guardHitThisFrame = false;
            health.dodgeAvoidedThisFrame = false;
        }
    }

    void CombatSystem::Update(World& world, float dtSec)
    {
        if (dtSec <= 0.0f)
            return;

        for (auto&& [id, health] : world.GetComponents<HealthComponent>())
        {
            if (health.invulnRemaining > 0.0f)
            {
                health.invulnRemaining = std::max(0.0f, health.invulnRemaining - dtSec);
            }

            if (health.currentHealth <= 0.0f)
            {
                health.currentHealth = 0.0f;
                health.alive = false;
            }
        }
    }

    void CombatSystem::ProcessHits(World& world, const std::vector<CombatHitEvent>& hits)
    {
        for (const auto& hit : hits)
        {
            if (hit.victimOwner == InvalidEntityId)
                continue;

            auto* health = world.GetComponent<HealthComponent>(hit.victimOwner);
            if (!health || !health->alive)
                continue;

            if (health->dodgeActive)
            {
                health->dodgeAvoidedThisFrame = true;
                health->lastHitDamage = 0.0f;
                health->lastHitAttacker = hit.attackerOwner;
                health->lastHitPart = hit.part;
                health->lastHitPosWS = hit.hitPosWS;
                health->lastHitNormalWS = hit.hitNormalWS;
                continue;
            }

            if (health->invulnRemaining > 0.0f)
                continue;

            float damage = hit.damage;
            const bool guarded = health->guardActive;
            if (guarded)
            {
                const float scale = std::clamp(health->guardDamageScale, 0.0f, 1.0f);
                damage *= scale;
            }

            health->hitThisFrame = true;
            health->guardHitThisFrame = health->guardHitThisFrame || guarded;
            health->lastHitDamage = damage;
            health->lastHitAttacker = hit.attackerOwner;
            health->lastHitPart = hit.part;
            health->lastHitPosWS = hit.hitPosWS;
            health->lastHitNormalWS = hit.hitNormalWS;

            health->currentHealth -= damage;
            if (health->currentHealth <= 0.0f)
            {
                health->currentHealth = 0.0f;
                health->alive = false;
            }

            if (health->invulnDuration > 0.0f)
            {
                health->invulnRemaining = health->invulnDuration;
            }

            if (hit.debugLog)
            {
                ALICE_LOG_INFO("[Combat] Hit attacker=%llu victim=%llu part=%u dmg=%.2f pos=(%.2f,%.2f,%.2f)",
                    static_cast<unsigned long long>(hit.attackerOwner),
                    static_cast<unsigned long long>(hit.victimOwner),
                    hit.part,
                    hit.damage,
                    hit.hitPosWS.x, hit.hitPosWS.y, hit.hitPosWS.z);
            }
        }
    }
}
