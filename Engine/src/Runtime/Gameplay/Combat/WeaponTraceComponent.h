#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    enum class WeaponTraceShapeType : uint8_t
    {
        Sphere = 0,
        Capsule = 1,
        Box = 2,
    };

    enum class WeaponTracePathMode : uint8_t
    {
        Linear = 0,
        QuadraticBezier = 1,
    };

    struct WeaponTraceShape
    {
        std::string name = "Shape";
        bool enabled = true;
        WeaponTraceShapeType type = WeaponTraceShapeType::Sphere;

        DirectX::XMFLOAT3 localPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 localRotDeg{ 0.0f, 0.0f, 0.0f };

        float radius = 0.05f;
        float capsuleHalfHeight = 0.20f;
        DirectX::XMFLOAT3 boxHalfExtents{ 0.05f, 0.05f, 0.20f };

        // Optional local-position sweep while trace is active (A -> B).
        bool pathEnabled = false;
        WeaponTracePathMode pathMode = WeaponTracePathMode::Linear;
        DirectX::XMFLOAT3 pathStartLocalPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 pathControlLocalPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 pathEndLocalPos{ 0.0f, 0.0f, 0.0f };
    };

    struct WeaponTraceDebugSweepSegment
    {
        std::uint32_t shapeIndex = 0;
        DirectX::XMFLOAT3 startCenterWS{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 startRotWS{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 endCenterWS{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 endRotWS{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct WeaponTraceComponent
    {
        // 소켓 제공자 GUID (직렬화 대상)
        std::uint64_t ownerGuid = 0;
        std::string ownerNameDebug;

        // 런타임 캐시 (직렬화 금지)
        EntityId ownerCached = InvalidEntityId;

        // Trace 기준 엔티티 (무기 엔티티 등)
        std::uint64_t traceBasisGuid = 0;
        EntityId traceBasisCached = InvalidEntityId;

        std::vector<WeaponTraceShape> shapes;

        float radius = 0.05f;
        bool active = false;
        bool debugDraw = false;
        float baseDamage = 10.0f;
        float guardDurabilityCost = 0.0f;
        float guardLockSec = 0.25f;
        float parryLockSec = 0.0f;
        float parryGroggyGain = 10.0f;
        float guardBreakWeakSec = 1.5f;
        float guardBreakPushbackSpeed = 3.0f;
        float guardBreakPushbackDuration = 0.25f;
        uint32_t teamId = 0;
        uint32_t attackInstanceId = 0;
        uint32_t targetLayerBits = 0;
        uint32_t queryLayerBits = 0;

        uint32_t subSteps = 1;
        bool debugPathGuide = true;
        uint32_t debugPathGridSteps = 6;
        float debugPathMarkerRadius = 0.05f;
        float activeElapsedSec = 0.0f;
        float activeWindowDurationSec = 0.0f;

        bool hasPrevBasis = false;
        DirectX::XMFLOAT3 prevBasisPos{};
        DirectX::XMFLOAT4 prevBasisRot{};

        bool hasPrevShapes = false;
        std::vector<DirectX::XMFLOAT3> prevCentersWS;
        std::vector<DirectX::XMFLOAT4> prevRotsWS;
        // Runtime-only debug sweep cache used by DebugDrawComponentSystem.
        std::vector<WeaponTraceDebugSweepSegment> debugSweepSegments;

        uint32_t lastAttackInstanceId = 0;
        std::unordered_set<std::uint64_t> hitVictims;
    };
}
