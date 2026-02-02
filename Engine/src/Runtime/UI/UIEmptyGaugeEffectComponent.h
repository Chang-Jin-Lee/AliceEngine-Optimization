#pragma once

#include <DirectXMath.h>

namespace Alice
{
    struct UIEmptyGaugeEffectComponent
    {
        bool enabled{ true };
        DirectX::XMFLOAT4 color{ 0.2f, 0.2f, 0.2f, 1.0f };
        float frequency{ 6.0f };
        float speed{ 1.0f };
        float intensity{ 0.35f };
    };
}
