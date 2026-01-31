#include "C_Fighter.h"

#include <DirectXMath.h>
#include <cmath>

#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/IPhysicsWorld.h"

namespace Alice::Combat
{
    Sensors Fighter::BuildSensors(World& world, EntityId targetId, float dt)
    {
        Sensors s{};
        s.dt = dt;
        s.hp = hp;
        s.stamina = stamina;
        s.moveSpeed = moveSpeed;
        s.targetId = targetId;

        if (auto* cct = world.GetComponent<Phy_CCTComponent>(id))
        {
            s.grounded = cct->onGround;
            const uint8_t flags = cct->collisionFlags;
            s.blocked = (flags & static_cast<uint8_t>(CCTCollisionFlags::Sides)) != 0u;
        }

        if (auto* hc = world.GetComponent<HealthComponent>(id))
        {
            s.hp = hc->currentHealth;
            s.guardWindowActive = hc->guardActive;
            s.dodgeWindowActive = hc->dodgeActive;
            s.invulnActive = hc->invulnRemaining > 0.0f;
            s.groggyDuration = hc->groggyDuration;
        }

        if (auto* driver = world.GetComponent<AttackDriverComponent>(id))
        {
            s.attackWindowActive = driver->attackActive;
            s.guardWindowActive = s.guardWindowActive || driver->guardActive;
            s.dodgeWindowActive = s.dodgeWindowActive || driver->dodgeActive;
        }

        const auto* selfTr = world.GetComponent<TransformComponent>(id);
        const auto* targetTr = world.GetComponent<TransformComponent>(targetId);
        if (selfTr && targetTr)
        {
            const float dx = targetTr->position.x - selfTr->position.x;
            const float dy = targetTr->position.y - selfTr->position.y;
            const float dz = targetTr->position.z - selfTr->position.z;
            s.distToTarget = std::sqrt(dx * dx + dy * dy + dz * dz);

            DirectX::XMVECTOR forward = DirectX::XMVector3TransformCoord(
                DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                DirectX::XMMatrixRotationRollPitchYaw(selfTr->rotation.x, selfTr->rotation.y, selfTr->rotation.z));
            DirectX::XMFLOAT3 f{};
            DirectX::XMStoreFloat3(&f, forward);
            const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
            const float fx = (len > 0.0f) ? (f.x / len) : 0.0f;
            const float fz = (len > 0.0f) ? (f.z / len) : 1.0f;
            const float tx = (s.distToTarget > 0.0f) ? (dx / s.distToTarget) : 0.0f;
            const float tz = (s.distToTarget > 0.0f) ? (dz / s.distToTarget) : 1.0f;
            const float dot = fx * tx + fz * tz;
            s.targetInFront = (dot >= 0.0f);
        }

        lastTargetInFront = s.targetInFront;
        return s;
    }

    FighterSnapshot Fighter::Snapshot() const
    {
        FighterSnapshot snap{};
        snap.id = id;
        snap.team = team;
        snap.state = state;
        snap.flags = flags;
        snap.hp = hp;
        snap.stamina = stamina;
        snap.targetInFront = lastTargetInFront;
        snap.canBeHitstunned = canBeHitstunned;
        return snap;
    }
}
