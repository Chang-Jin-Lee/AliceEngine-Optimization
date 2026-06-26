#pragma once

#include <string>
#include <unordered_map>

namespace Alice
{
    enum class AnimParamType
    {
        Bool,
        Int,
        Float,
        Trigger
    };

    struct AnimParamValue
    {
        AnimParamType type{ AnimParamType::Float };
        bool  b{ false };
        int   i{ 0 };
        float f{ 0.0f };
        bool  trigger{ false };
    };

    /// 애니메이션 블루프린트 컴포넌트 (FSM + 파라미터)
    struct AnimBlueprintComponent
    {
        // AnimBlueprint JSON 경로 (논리 경로: Assets/... or Resource/...)
        std::string blueprintPath;

        bool  playing{ true };
        float speed{ 1.0f };

        // 스크립트에서 직접 제어할 파라미터 저장소
        std::unordered_map<std::string, AnimParamValue> params;
    };
}

