#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>

namespace Alice
{
    struct SocketDef
    {
        std::string name;
        std::string parentBone;

        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };

        // 런타임 갱신 (로컬/월드 행렬)
        DirectX::XMFLOAT4X4 local =
        {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
        DirectX::XMFLOAT4X4 world =
        {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
    };

    /// 스키닝 소켓 목록 (무기/이펙트 부착용)
    struct SocketComponent
    {
        std::vector<SocketDef> sockets;
    };
}

