#pragma once

#include <cstdint>
#include <string>

#include "Core/Entity.h"

namespace Alice
{
    struct HurtboxComponent
    {
        // 실제 피격 주체 (캐릭터 루트 GUID)
        std::uint64_t ownerGuid = 0;
        std::string ownerNameDebug;

        // 런타임 캐시 (직렬화 금지)
        EntityId ownerCached = InvalidEntityId;

        // 팀 구분 (0: 기본)
        uint32_t teamId = 0;

        // 부위 ID (head/arm/leg 등)
        uint32_t part = 0;

        // 옵션: 부위 데미지 배율
        float damageScale = 1.0f;
    };
}
