#pragma once

#include "../IPhysicsWorld.h"
#include <string>

// Mesh Collider 타입
enum class MeshColliderType : uint8_t
{
    Triangle,
    Convex,
};

// Mesh Collider 컴포넌트
// - Triangle: 정적(Static)만 사용 권장 (Dynamic 금지)
// - Convex: Dynamic 가능 (단, 오목한 메시 형태는 부정확)
struct Phy_MeshColliderComponent
{
    MeshColliderType type = MeshColliderType::Triangle;

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

    // Debug draw toggle (editor/runtime overlay)
    bool debugDraw = false;

    // Mesh Asset (SkinnedMeshRegistry 키). 비어있으면 동일 엔티티의 SkinnedMeshComponent를 사용.
    std::string meshAssetPath;

    // Triangle 전용 옵션
    bool flipNormals = false;
    bool doubleSidedQueries = false;
    bool validate = false;

    // Convex 전용 옵션
    bool shiftVertices = true;
    uint32_t vertexLimit = 255;

    // 내부 사용: 물리 액터 핸들 (PhysicsSystem이 관리)
    // 직접 접근 금지: PhysicsSystem::ValidateAndGetActor()를 통해 접근하세요
    IPhysicsActor* physicsActorHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};

