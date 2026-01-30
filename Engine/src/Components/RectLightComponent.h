#pragma once

#include <DirectXMath.h>

namespace Alice
{
    /// 사각형(면) 라이트 컴포넌트 (Transform 필요)
    struct RectLightComponent
    {
        DirectX::XMFLOAT3 color { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
        float width { 1.0f };
        float height { 1.0f };
        float range { 10.0f };
        bool enabled { true };
    };
}
