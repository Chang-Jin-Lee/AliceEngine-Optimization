#pragma once

#include <string>
#include <cstdint>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    struct SocketAttachmentComponent
    {
        // 소켓을 제공하는 엔티티 GUID (직렬화 대상)
        std::uint64_t ownerGuid = 0;
        std::string ownerNameDebug;

        // 런타임 캐시 (직렬화 금지)
        EntityId ownerCached = InvalidEntityId;

        // 따라갈 소켓 이름
        std::string socketName;

        // 옵션: 스케일 동기화 여부
        bool followScale = true;

        // 옵션: 추가 오프셋
        DirectX::XMFLOAT3 extraPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 extraRotRad{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 extraScale{ 1.0f, 1.0f, 1.0f };
        // 추가 회전 적용 공간 (true=월드/부모 기준, false=소켓 로컬 기준)
        bool extraRotInWorld = true;
    };
}
