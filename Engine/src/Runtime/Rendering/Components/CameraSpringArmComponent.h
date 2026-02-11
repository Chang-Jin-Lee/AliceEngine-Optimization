#pragma once
#include <cstdint>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    /// 스프링 암 (카메라-타깃 거리/충돌/줌)
    struct CameraSpringArmComponent
    {
        bool enabled{ true };
        bool enableCollision{ true };
        bool enableZoom{ true };
        bool hideFollowTargetAtCollisionMin{ false };
        uint32_t collisionLayerMask{ 0xFFFFFFFFu };
        uint32_t collisionQueryMask{ 0xFFFFFFFFu };
        float collisionMinDistance{ -1.0f };

        float distance{ 35.0f };
        float minDistance{ 8.0f };
        float maxDistance{ 60.0f };
        float zoomSpeed{ 0.01f };
        float distanceDamping{ 6.0f };

        float probeRadius{ 0.35f };
        float probePadding{ 0.2f };
        float minHeight{ -1000.0f };

        // 런타임
        float desiredDistance{ 35.0f };
        bool initialized{ false };
        EntityId hiddenFollowTargetId{ InvalidEntityId };
        bool hiddenFollowTargetPrevVisible{ true };
    };
}
