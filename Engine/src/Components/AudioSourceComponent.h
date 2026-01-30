#pragma once

#include <string>

namespace Alice
{
    enum class AudioType
    {
        BGM,
        SFX
    };

    /// 2D/3D 오디오 소스
    struct AudioSourceComponent
    {
        // 사운드 키(캐시 키). 비어있으면 soundPath를 사용합니다.
        std::string soundKey;
        // 논리 경로 (Assets/... or Resource/...)
        std::string soundPath;

        AudioType type{ AudioType::SFX };
        bool is3D{ true };
        bool loop{ false };
        bool playOnStart{ true };

        float volume{ 1.0f };
        float pitch{ 1.0f };

        // 3D 거리 감쇠
        float minDistance{ 1.0f };
        float maxDistance{ 50.0f };

        // 재생 요청 플래그 (스크립트에서 토글)
        bool requestPlay{ false };
        bool requestStop{ false };

        // 디버그 드로우 활성화 여부
        bool debugDraw{ false };
    };
}

