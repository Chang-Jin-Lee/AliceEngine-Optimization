#pragma once

#include <cstdint>

namespace Alice
{
    enum class RootMotionMode : std::uint8_t
    {
        NoExtraction = 0,
        Ignore = 1,
        FromEverything = 2
    };

    enum class RootLockMode : std::uint8_t
    {
        AnimFirstFrame = 0,
        Zero = 1
    };
}
