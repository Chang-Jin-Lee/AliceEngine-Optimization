#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Alice::LegacyPathDetail
{
    constexpr std::uint32_t kMaxBones = 1023;

    constexpr std::uint32_t BoneMatricesToWrite(
        std::uint32_t boneCount, bool legacyFullBuffer) noexcept
    {
        return legacyFullBuffer ? kMaxBones : (std::min)(boneCount, kMaxBones);
    }

    constexpr std::size_t BoneUploadBytes(
        std::uint32_t boneCount, bool legacyFullBuffer) noexcept
    {
        return static_cast<std::size_t>(BoneMatricesToWrite(boneCount, legacyFullBuffer)) *
            sizeof(float) * 16 + sizeof(std::uint32_t);
    }

    constexpr bool ShouldEvaluateAnimation(
        bool playing, bool poseEvaluated, bool poseInputsChanged,
        bool legacyAnimateWhenStopped) noexcept
    {
        return legacyAnimateWhenStopped || playing || !poseEvaluated || poseInputsChanged;
    }
}

namespace Alice
{
    struct LegacyPathFlags
    {
        bool fullBoneConstantBuffer = false;
        bool copyPaletteEveryFrame = false;
        bool noCameraMatrixCache = false;
        bool animateWhenNotPlaying = false;
        bool heapAllocWorldMatrix = false;
        bool perParticleDrawCall = false;
        bool staticMeshThroughSkinning = false;
        bool outlineOnByDefault = false;
        bool opaqueInTransparentPass = false;

        static LegacyPathFlags& Get() noexcept;
        void SetAll(bool value) noexcept;
        bool AnyEnabled() const noexcept;
        bool AllEnabled() const noexcept;
    };
}
