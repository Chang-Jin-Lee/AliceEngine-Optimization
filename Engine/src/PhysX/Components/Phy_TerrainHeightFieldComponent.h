#pragma once

#include "../IPhysicsWorld.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

// Terrain HeightField 컴포넌트
// HeightField는 지형용 전용 컴포넌트로, Phy_ColliderComponent와 분리됨
// - RigidStatic으로만 생성 (움직이지 않는 지형)
// - Trigger 불가 (PhysX 제약)
// - Non-kinematic dynamic body에 붙일 수 없음
struct Phy_TerrainHeightFieldComponent
{
    // HeightField 데이터
    // TODO: 나중에 AssetHandle로 리소스 시스템과 연동 가능
    // 현재는 직접 데이터를 보관
    std::vector<float> heightSamples;  // Row-major: samples[i * numCols + j]
    uint32_t numRows = 0;              // Grid rows (must be >= 2)
    uint32_t numCols = 0;              // Grid columns (must be >= 2)

    // Scale parameters
    float rowScale = 1.0f;    // Size along rows (Y-axis spacing) in world units, must be > 0
    float colScale = 1.0f;    // Size along columns (X-axis spacing) in world units, must be > 0
    float heightScale = 0.01f; // Height quantization: world height per int16 step (must be > 0)
                               // Example: 0.01f means 1 int16 unit = 1cm in world space

    // Pivot settings
    bool centerPivot = true;  // If true, entity origin is at terrain center
                             // If false, entity origin is at corner (0,0)

    // Query settings
    bool doubleSidedQueries = true;  // Enable double-sided ray/sweep queries

    // Material (overrides default material)
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;

    // Filtering
    uint32_t layerBits = 1u << 0;
    // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
    uint32_t ignoreLayers = 0u; // 이그노어 레이어 비트마스크 (충돌/쿼리 모두 무시)

    // 내부 사용: 물리 액터 핸들 (PhysicsSystem이 관리)
    // HeightField는 항상 RigidStatic으로 생성됨
    // 직접 접근 금지: PhysicsSystem::ValidateAndGetActor()를 통해 접근하세요
    IPhysicsActor* physicsActorHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};
