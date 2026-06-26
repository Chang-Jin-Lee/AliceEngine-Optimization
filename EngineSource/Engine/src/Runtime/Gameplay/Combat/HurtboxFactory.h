#pragma once

#include <string>
#include <cstdint>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"

namespace Alice
{
    class World;

    struct HurtboxDesc
    {
        std::string name;

        std::uint64_t ownerGuid = 0;
        std::string ownerNameDebug;
        EntityId ownerId = InvalidEntityId;

        std::string socketName;
        bool followScale = true;

        DirectX::XMFLOAT3 extraPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 extraRotRad{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 extraScale{ 1.0f, 1.0f, 1.0f };

        ColliderType shapeType = ColliderType::Sphere;
        DirectX::XMFLOAT3 halfExtents{ 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        float capsuleRadius = 0.5f;
        float capsuleHalfHeight = 0.5f;
        bool capsuleAlignYAxis = true;

        uint32_t layerBits = 0u;
        uint32_t ignoreLayers = 0u;

        uint32_t teamId = 0;
        uint32_t part = 0;
        float damageScale = 1.0f;
    };

    EntityId CreateHurtbox(World& world, const HurtboxDesc& desc);
}
