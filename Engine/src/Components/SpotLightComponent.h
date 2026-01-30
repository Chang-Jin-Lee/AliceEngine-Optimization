#pragma once

#include <DirectXMath.h>

namespace Alice
{
    /// 스폿 라이트 컴포넌트 (Transform 필요)
    struct SpotLightComponent
    {
        DirectX::XMFLOAT3 color { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
        float range { 15.0f };
        float innerAngleDeg { 15.0f };
        float outerAngleDeg { 30.0f };
        bool enabled { true };
    };
}
