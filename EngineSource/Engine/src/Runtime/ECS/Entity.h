#pragma once

#include <cstdint>

namespace Alice
{
    /// 엔티티를 식별하기 위한 단순 ID 타입입니다.
    /// - 실제 데이터는 컴포넌트에 저장되고,
    /// - 엔티티는 해당 데이터를 참조하기 위한 핸들 역할만 합니다.
    using EntityId = std::uint32_t;

    constexpr EntityId InvalidEntityId = 0;
}


