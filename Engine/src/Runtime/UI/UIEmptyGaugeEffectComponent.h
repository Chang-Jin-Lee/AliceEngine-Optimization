#pragma once

#include <DirectXMath.h>
#include <string>

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

    struct UIPencilComponent
    {
        std::string pencilTexturePath{ "" };  // 연필 질감 텍스처 경로
        float pencilTileScale{ 5.0f };        // 연필 질감 타일링 크기
        float pencilJitterStrength{ 0.002f }; // UV 왜곡 강도
        float pencilContrast{ 1.2f };         // 대비 강도
    };
}
