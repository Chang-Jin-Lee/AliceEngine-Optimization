#pragma once

#include "Runtime/ECS/World.h"

namespace Alice
{
    class DebugDrawSystem;

    /// DebugDraw 관련 컴포넌트를 모아 DebugDrawSystem에 그려주는 시스템
    class DebugDrawComponentSystem
    {
    public:
        void Build(World& world,
                   DebugDrawSystem* overlay,
                   DebugDrawSystem* depth,
                   EntityId selectedEntity,
                   bool debugEnabled,
                   bool editorMode);
    };
}
