#pragma once

namespace Alice
{
    class World;
    class InputSystem;

    /// 카메라 관련 컴포넌트를 처리하는 간단한 시스템
    class CameraSystem
    {
    public:
        void Update(World& world, InputSystem& input, float deltaTime);
    };
}
