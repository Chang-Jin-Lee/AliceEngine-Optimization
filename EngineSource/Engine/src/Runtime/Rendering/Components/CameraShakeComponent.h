#pragma once

#include <DirectXMath.h>

namespace Alice
{
    /// 카메라 쉐이크 파라미터
    struct CameraShakeComponent
    {
        bool enabled{ true };
        float amplitude{ 0.0f };
        float frequency{ 20.0f };
        float duration{ 0.0f };
        float decay{ 1.0f };

        // 런타임
        float elapsed{ 0.0f };
        
        // 드리프트 방지: 이전 프레임에 적용했던 오프셋 저장
        DirectX::XMFLOAT3 prevOffset{};

        // 쉐이크가 활성 상태인지 확인
        bool IsActive() const 
        { 
            return enabled && amplitude > 0.0f && duration > 0.0f && elapsed < duration; 
        }
    };
}
