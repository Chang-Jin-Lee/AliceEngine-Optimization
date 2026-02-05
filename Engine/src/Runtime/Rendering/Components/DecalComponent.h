#pragma once

#include <DirectXMath.h>
#include <string>

namespace Alice
{
    /// 데칼 컴포넌트
    /// - Decal Entity에 부착하여 투영 데칼을 렌더링합니다.
    /// - 위치/회전/스케일은 TransformComponent로 제어합니다.
    struct DecalComponent
    {
        bool enabled{ true };
        std::string albedoTexturePath; // 데칼 알베도 텍스처 (RGBA, A=마스크)

        DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f }; // 틴트 컬러
        float opacity{ 1.0f };                        // 전역 불투명도 (0~1)

        int sortOrder{ 0 };                           // 데칼 정렬 우선순위 (작을수록 먼저)

        DirectX::XMFLOAT2 uvScale{ 1.0f, 1.0f };
        DirectX::XMFLOAT2 uvOffset{ 0.0f, 0.0f };
    };
}
