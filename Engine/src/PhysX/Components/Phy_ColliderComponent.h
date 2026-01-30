#pragma once

#include "../IPhysicsWorld.h"
#include <DirectXMath.h>

// Collider 타입
enum class ColliderType : uint8_t
{
    Box,
    Sphere,
    Capsule,
};

// Collider 컴포넌트
// Phy_RigidBodyComponent와 함께 사용되거나, Static Actor로 사용 가능
struct Phy_ColliderComponent
{
    ColliderType type = ColliderType::Box;

    // Box 파라미터
    DirectX::XMFLOAT3 halfExtents = { 0.5f, 0.5f, 0.5f };

    // Shape local offset (pivot-based). Applied in local space before rotation.
    DirectX::XMFLOAT3 offset = { 0.0f, 0.0f, 0.0f };

    // Sphere 파라미터
    float radius = 0.5f;

    // Capsule 파라미터
    float capsuleRadius = 0.5f;
    float capsuleHalfHeight = 0.5f;
    bool capsuleAlignYAxis = true;

    // Debug draw toggle (editor/runtime overlay)
    bool debugDraw = false;

    // Material
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;

    // Filtering
    uint32_t layerBits = 1u << 0;
    // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
    uint32_t ignoreLayers = 0u; // 이그노어 레이어 비트마스크 (충돌/쿼리 모두 무시)

    // Trigger 여부
    bool isTrigger = false;

    // 내부 사용: 물리 액터 핸들 (PhysicsSystem이 관리)
    // Phy_RigidBodyComponent가 있으면 그 바디에 shape이 추가됨
    // 없으면 Static Actor로 생성됨
    //  직접 접근 금지: PhysicsSystem::ValidateAndGetActor()를 통해 접근하세요
    IPhysicsActor* physicsActorHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};
