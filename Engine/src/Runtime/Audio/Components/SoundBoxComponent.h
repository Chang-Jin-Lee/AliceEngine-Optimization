#pragma once

#include <string>

#include <DirectXMath.h>
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    enum class SoundBoxType
    {
        BGM,
        SFX
    };

    /// 영역 기반 3D 사운드 박스
    struct SoundBoxComponent
    {
        // 사운드 키(캐시 키). 비어있으면 soundPath 사용
        std::string soundKey;
        // 논리 경로 (Assets/... or Resource/...)
        std::string soundPath;

        SoundBoxType type{ SoundBoxType::BGM };
        bool loop{ true };
        bool playOnEnter{ true };
        bool stopOnExit{ true };

        // 로컬 AABB
        DirectX::XMFLOAT3 boundsMin{ -10.0f, -10.0f, -10.0f };
        DirectX::XMFLOAT3 boundsMax{  10.0f,  10.0f,  10.0f };

        // 볼륨 커브
        float edgeVolume{ 0.0f };
        float centerVolume{ 1.0f };
        float curve{ 1.0f };

        // 3D 감쇠
        float minDistance{ 1.0f };
        float maxDistance{ 50.0f };

        // 디버그 드로우 활성화 여부
        bool debugDraw{ false };

        // 반응할 타겟 엔티티 (기본값: Invalid -> 카메라/리스너에 반응)
        EntityId targetEntity = InvalidEntityId;

        // 타겟 설정 함수
        void SetTarget(EntityId id)
        {
            targetEntity = id;
        }
    };
}

