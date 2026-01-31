#pragma once

#include <string>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    /// 3인칭 팔로우 + 락온 기본 파라미터
    struct CameraFollowComponent
    {
        bool enabled{ true };

        // 타깃
        std::string targetName{};
        float heightOffset{ 1.5f };
        float shoulderOffset{ 0.3f };
        float shoulderSide{ 1.0f };

        // 입력/회전
        bool enableInput{ true };
        float sensitivity{ 0.2f };
        float yawDeg{ 0.0f };
        float pitchDeg{ 20.0f };
        float pitchMinDeg{ -45.0f };
        float pitchMaxDeg{ 80.0f };

        // 거리/줌 (SpringArm과 연동)
        float baseDistance{ 35.0f };
        float minDistance{ 8.0f };
        float maxDistance{ 60.0f };

        // 스무딩
        float positionDamping{ 8.0f };
        float rotationDamping{ 12.0f };
        float fastTurnYawThresholdDeg{ 90.0f };
        float fastTurnMultiplier{ 2.5f };

        // 모드 (Explore/Combat/LockOn/Aim/BossIntro/Death)
        int mode{ 0 };
        float exploreDistance{ 35.0f };
        float combatDistance{ 28.0f };
        float lockOnDistance{ 24.0f };
        float aimDistance{ 18.0f };
        float bossIntroDistance{ 45.0f };
        float deathDistance{ 55.0f };

        float exploreFovDeg{ 60.0f };
        float combatFovDeg{ 65.0f };
        float lockOnFovDeg{ 55.0f };
        float aimFovDeg{ 45.0f };
        float bossIntroFovDeg{ 75.0f };
        float deathFovDeg{ 50.0f };
        float fovDamping{ 6.0f };

        // 락온
        bool enableLockOn{ true };
        float lockOnMaxDistance{ 35.0f };
        float lockOnMaxAngleDeg{ 50.0f };
        float lockOnSwitchAngleDeg{ 45.0f };
        float lockOnAngleWeight{ 1.5f };
        float lockOnRotationDamping{ 10.0f };
        bool allowManualOrbitInLockOn{ false };

        // 연출
        float cameraTimeScale{ 1.0f };

        // 런타임 상태 (직렬화/편집 대상 아님)
        EntityId lockOnTargetId{ InvalidEntityId };
        bool lockOnActive{ false };
        bool initialized{ false };
        DirectX::XMFLOAT3 smoothedPosition{};
        DirectX::XMFLOAT3 smoothedRotation{};
        
        // 마우스 잠금 상태 (커서 숨김 + 회전 활성화)
        bool mouseLocked{ true };
    };
}
