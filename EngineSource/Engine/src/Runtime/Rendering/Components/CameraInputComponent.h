#pragma once

#include <string>

namespace Alice
{
    /// 카메라 입력/전환 프리셋 (콘텐츠용 최소 설정)
    struct CameraInputComponent
    {
        bool enabled{ true };

        // 카메라 목록 (CSV)
        std::string cameraListCsv{ "Camera1,Camera2,Camera3,Camera4,Camera5" };

        // 전환 시간
        float blendTimeKey3{ 0.6f };
        float blendTimeKey4{ 0.6f };
        float blendTimeKey5{ 0.8f };

        // 4번 키: 블렌드 + 쉐이크
        float shakeAmplitudeKey4{ 0.3f };
        float shakeFrequencyKey4{ 20.0f };
        float shakeDurationKey4{ 0.4f };
        float shakeDecayKey4{ 2.0f };

        // 5번 키: 블렌드 + 슬로우 모션
        float slowTriggerTKey5{ 0.5f };
        float slowDurationKey5{ 0.3f };
        float slowTimeScaleKey5{ 0.2f };

        // L 키: 룩앳 타깃
        std::string lookAtTargetName{ "Enemy" };
    };
}
