#pragma once

// 카메라 컬링 수치
namespace Alice::CullingTuning
{
    // Camera frustum widening for conservative culling.
    inline constexpr float FrustumFovScale = 1.3f;
    inline constexpr float FrustumFovClampEpsilon = 0.02f;

    // Static cube-mesh local bounds: [-Extent, +Extent].
    inline constexpr float StaticMeshLocalBoundsExtent = 1.0f;

    // Sphere culling defaults.
    inline constexpr float MinCullingSphereRadius = 0.05f;
    inline constexpr float SkinnedFallbackRadiusScale = 1.5f;
    inline constexpr float DecalCullingRadiusScale = 1.8f;

    // Shadow-space coverage inflation (directional shadow pass).
    inline constexpr float ShadowSceneRadiusScale = 1.5f;

    // Local light shadow caster culling.
    // false: draw all candidates in local shadow pass (safe; avoids missing shadows).
    // true : apply light-space sphere/frustum culling.
    inline constexpr bool EnableLocalShadowCasterCulling = false;
    inline constexpr float LocalShadowCasterBoundsInflation = 2.0f;
}
