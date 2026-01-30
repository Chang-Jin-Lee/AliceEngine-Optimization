#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>

namespace Alice
{
    enum class SocketPoseSource : std::uint8_t
    {
        None = 0,
        AdvancedAnim,
        SkinnedAnim,
        AnimBlueprint,
        FbxPreview,
        FallbackLocal,
    };

    struct SocketPose
    {
        std::string name;
        DirectX::XMFLOAT4X4 world
        {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
    };

    // Runtime-only socket world outputs (not serialized).
    struct SocketPoseOutputComponent
    {
        SocketPoseSource source = SocketPoseSource::None;
        std::vector<SocketPose> poses;
    };
}
