#pragma once

#include <Core/World.h>
#include <Rendering/DebugDrawSystem.h>

namespace Alice
{
    namespace PhysicsDebug
    {
        // ColliderComponent를 기준으로 와이어프레임 그리기
        void DrawColliders(World& world, DebugDrawSystem& debugDraw);
    }
}
