#pragma once

#include <cstdint>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    struct CombatHitEvent
    {
        EntityId attackerOwner = InvalidEntityId;
        EntityId victimOwner = InvalidEntityId;
        EntityId hurtboxEntity = InvalidEntityId;

        uint32_t part = 0;
        uint32_t attackInstanceId = 0;
        uint32_t subShapeIndex = 0;
        float damage = 0.0f;
        bool debugLog = false;

        float sweepFraction = 0.0f;
        bool hasSweepFraction = false;

        DirectX::XMFLOAT3 hitPosWS{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 hitNormalWS{ 0.0f, 1.0f, 0.0f };
    };
}
