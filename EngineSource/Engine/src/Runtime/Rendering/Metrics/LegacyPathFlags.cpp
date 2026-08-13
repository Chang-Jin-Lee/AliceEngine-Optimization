#include "Runtime/Rendering/Metrics/LegacyPathFlags.h"

namespace Alice
{
    LegacyPathFlags& LegacyPathFlags::Get() noexcept
    {
        static LegacyPathFlags instance;
        return instance;
    }

    void LegacyPathFlags::SetAll(bool value) noexcept
    {
        fullBoneConstantBuffer = value;
        copyPaletteEveryFrame = value;
        noCameraMatrixCache = value;
        animateWhenNotPlaying = value;
        heapAllocWorldMatrix = value;
        perParticleDrawCall = value;
        staticMeshThroughSkinning = value;
        outlineOnByDefault = value;
        opaqueInTransparentPass = value;
    }

    bool LegacyPathFlags::AnyEnabled() const noexcept
    {
        return fullBoneConstantBuffer || copyPaletteEveryFrame ||
            noCameraMatrixCache || animateWhenNotPlaying ||
            heapAllocWorldMatrix || perParticleDrawCall ||
            staticMeshThroughSkinning || outlineOnByDefault ||
            opaqueInTransparentPass;
    }

    bool LegacyPathFlags::AllEnabled() const noexcept
    {
        return fullBoneConstantBuffer && copyPaletteEveryFrame &&
            noCameraMatrixCache && animateWhenNotPlaying &&
            heapAllocWorldMatrix && perParticleDrawCall &&
            staticMeshThroughSkinning && outlineOnByDefault &&
            opaqueInTransparentPass;
    }
}
