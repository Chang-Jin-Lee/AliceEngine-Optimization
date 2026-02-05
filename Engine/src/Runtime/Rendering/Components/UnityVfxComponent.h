#pragma once

#include <string>

namespace Alice
{
    /// Unity VFX(ParticleSystem) JSON 기반 이펙트 컴포넌트
    /// - effect.json 경로를 지정하면 런타임에서 ComputeEffect로 매핑합니다.
    struct UnityVfxComponent
    {
        bool enabled{ true };
        std::string effectPath{};     // effect.json logical path (Assets/...)

        // Render path toggles (v2 mesh renderer vs v1 compute overlay)
        bool useMeshRenderer{ true };
        bool useComputeEffect{ false };

        // Global playback/override controls
        float timeScale{ 1.0f };
        float lifetimeScale{ 1.0f };
        bool overrideLoop{ false };
        bool loop{ true };

        // 매핑 보정값 (Unity 단위 -> 엔진 파티클 파라미터)
        float sizeScale{ 10.0f };      // Unity size * sizeScale => sizePx
        float speedScale{ 1.0f };
        float intensityScale{ 1.0f };  // 컬러/밝기 증폭
        float spawnRateScale{ 1.0f };  // Emission rate 스케일 (0..1)

        // Rendering/material overrides
        float colorScale{ 1.0f };      // RGB scale
        float alphaScale{ 1.0f };
        float hdrColorClamp{ 1.0f };   // 0 = off, otherwise clamp max RGB
        float uvScrollScale{ 1.0f };
        float dissolveOffset{ 0.0f };
        float noiseScale{ 1.0f };
        float rampScale{ 1.0f };

        // Trail overrides
        bool enableTrails{ true };
        float trailWidthScale{ 1.0f };
        float trailLifeScale{ 1.0f };
    };
}
