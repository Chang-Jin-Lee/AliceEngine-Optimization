#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Alice
{
    class ResourceManager;

    enum class AnimBPParamType { Bool, Int, Float, Trigger };
    enum class AnimBPCmpOp { EQ, NEQ, GT, LT, GTE, LTE, IsSet };

    struct AnimBPParam
    {
        std::string name;
        AnimBPParamType type{ AnimBPParamType::Float };
        bool  b{ false };
        int   i{ 0 };
        float f{ 0.0f };
        bool  trigger{ false };
    };

    struct AnimBPCond
    {
        std::string param;
        AnimBPParamType type{ AnimBPParamType::Float };
        AnimBPCmpOp op{ AnimBPCmpOp::GT };
        bool  b{ false };
        int   i{ 0 };
        float f{ 0.0f };
    };

    struct AnimBPState
    {
        std::uint64_t id{ 0 };
        std::uint64_t pinIn{ 0 };
        std::uint64_t pinOut{ 0 };
        std::string name;
        std::string clip;
        float playRate{ 1.0f };
    };

    struct AnimBPTransition
    {
        std::uint64_t id{ 0 };
        std::uint64_t fromOut{ 0 };
        std::uint64_t toIn{ 0 };
        float blendTime{ 0.2f };
        bool canInterrupt{ true };
        std::vector<AnimBPCond> conds;
    };

    /// 간단한 2-way 블렌드 정보를 상태 단위로 캐시한 구조체입니다.
    /// - Blend 그래프에서 Clip 노드 2개가 하나의 Blend 노드로 들어가는 패턴만 인식합니다.
    /// - 런타임에서는 이 정보를 기반으로 "Speed" 파라미터(0~1)로 두 클립을 섞습니다.
    struct AnimBPStateBlend
    {
        std::uint64_t stateId{ 0 };
        std::string clipA;
        std::string clipB;
    };

    struct AnimBlueprintAsset
    {
        std::vector<AnimBPParam> params;
        std::vector<AnimBPState> states;
        std::vector<AnimBPTransition> transitions;
        std::vector<AnimBPStateBlend> stateBlends; // 상태별 간단 2-way 블렌드 정의
        std::string targetMesh; // 에디터용 힌트 (선택)
    };

    bool LoadAnimBlueprintAssetAuto(const ResourceManager& resources,
                                    const std::filesystem::path& logicalPath,
                                    AnimBlueprintAsset& out);
}

