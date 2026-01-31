#pragma once

#include <string>

namespace Alice
{
    /// 지정한 타깃을 바라보는 룩앳 컴포넌트
    struct CameraLookAtComponent
    {
        bool enabled{ false };
        std::string targetName{ "Enemy" };
        float rotationDamping{ 8.0f };
    };
}
