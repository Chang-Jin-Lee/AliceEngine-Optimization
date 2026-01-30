#include "Game/HurtboxFactory.h"

#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/SocketAttachmentComponent.h"
#include "Components/HurtboxComponent.h"
#include "Components/IDComponent.h"
#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "Game/CombatPhysicsLayers.h"

namespace Alice
{
    EntityId CreateHurtbox(World& world, const HurtboxDesc& desc)
    {
        const EntityId eid = world.CreateEntity();

        if (!desc.name.empty())
            world.SetEntityName(eid, desc.name);

        auto& tr = world.AddComponent<TransformComponent>(eid);
        tr.parent = InvalidEntityId;

        auto& attach = world.AddComponent<SocketAttachmentComponent>(eid);
        attach.ownerGuid = desc.ownerGuid;
        attach.ownerNameDebug = desc.ownerNameDebug;
        attach.socketName = desc.socketName;
        attach.followScale = desc.followScale;
        attach.extraPos = desc.extraPos;
        attach.extraRotRad = desc.extraRotRad;
        attach.extraScale = desc.extraScale;

        std::uint64_t resolvedGuid = desc.ownerGuid;
        EntityId resolvedOwner = desc.ownerId;
        if (resolvedOwner != InvalidEntityId)
        {
            if (auto* idc = world.GetComponent<IDComponent>(resolvedOwner))
                resolvedGuid = idc->guid;
        }
        else if (resolvedGuid != 0)
        {
            resolvedOwner = world.FindEntityByGuid(resolvedGuid);
        }

        attach.ownerCached = resolvedOwner;

        auto& hurtbox = world.AddComponent<HurtboxComponent>(eid);
        hurtbox.ownerGuid = resolvedGuid;
        hurtbox.ownerNameDebug = desc.ownerNameDebug;
        hurtbox.ownerCached = resolvedOwner;
        hurtbox.teamId = desc.teamId;
        hurtbox.part = desc.part;
        hurtbox.damageScale = desc.damageScale;

        auto& rb = world.AddComponent<Phy_RigidBodyComponent>(eid);
        rb.isKinematic = true;
        rb.gravityEnabled = false;

        auto& col = world.AddComponent<Phy_ColliderComponent>(eid);
        col.isTrigger = true;
        col.type = desc.shapeType;
        col.halfExtents = desc.halfExtents;
        col.radius = desc.radius;
        col.capsuleRadius = desc.capsuleRadius;
        col.capsuleHalfHeight = desc.capsuleHalfHeight;
        col.capsuleAlignYAxis = desc.capsuleAlignYAxis;
        col.ignoreLayers = desc.ignoreLayers;

        col.layerBits = desc.layerBits != 0u ? desc.layerBits : CombatPhysicsLayers::HurtboxLayerBit(desc.teamId);

        return eid;
    }
}
