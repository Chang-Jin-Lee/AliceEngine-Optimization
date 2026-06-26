#pragma once

#include <DirectXMath.h>

namespace Alice
{
    /// 간단한 Debug Draw 박스(AABB) 컴포넌트
    /// - boundsMin/boundsMax: 로컬 AABB 크기
    /// - enabled: 렌더링 on/off
    /// - depthTest: true면 깊이 테스트 적용
    struct DebugDrawBoxComponent
    {
        DirectX::XMFLOAT3 boundsMin{ -0.5f, -0.5f, -0.5f };
        DirectX::XMFLOAT3 boundsMax{  0.5f,  0.5f,  0.5f };
        DirectX::XMFLOAT4 color{ 0.0f, 1.0f, 0.0f, 1.0f };
        bool enabled{ true };
        bool depthTest{ false };
    };
}
