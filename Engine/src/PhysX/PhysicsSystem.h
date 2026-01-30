#pragma once

#include "IPhysicsWorld.h"
#include "Components/Phy_RigidBodyComponent.h"
#include "Components/Phy_ColliderComponent.h"
#include "Components/Phy_MeshColliderComponent.h"
#include "Components/Phy_TerrainHeightFieldComponent.h"
#include "Components/Phy_CCTComponent.h"
#include "Components/Phy_SettingsComponent.h"
#include "Components/Phy_JointComponent.h"
#include <Core/World.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <array>
#include <string>
#include <limits>
#include <cmath>

namespace Alice { class SkinnedMeshRegistry; }

// PhysicsSystem: ECS 컴포넌트와 PhysX를 연결하는 브릿지
// - 컴포넌트 추가/제거 시 물리 액터 자동 생성/삭제
// - Game → Physics 동기화 (Transform 변경)
// - Physics → Game 동기화 (위치/회전)
// - 이벤트 라우팅
class PhysicsSystem
{
public:
    PhysicsSystem(Alice::World& world);
    ~PhysicsSystem();

    // 매 프레임 호출: 컴포넌트 변경 감지 및 동기화
    void Update(float deltaTime);

    // 물리 월드 설정 (씬 전환 시 호출)
    void SetPhysicsWorld(IPhysicsWorld* physicsWorld);
    
    // 현재 물리 월드 가져오기
    IPhysicsWorld* GetPhysicsWorld() const { return m_physicsWorld; }

    // 이벤트 콜백 타입
    using EventCallback = void(*)(const PhysicsEvent& event, void* userData);
    void SetEventCallback(EventCallback callback, void* userData);

    // Physics → Game 동기화 (외부에서 호출 가능)
    void SyncPhysicsToGame(const ActiveTransform& transform);

    // MeshCollider용 스키닝 메시 레지스트리
    void SetSkinnedMeshRegistry(class Alice::SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }

    // 현재 추적 중인 엔티티인지 확인 (씬 전환 중 stale userData 방지)
    bool IsTrackedEntity(Alice::EntityId id) const noexcept;

    // 타입 안전 핸들 검증 및 접근
    // 컴포넌트의 void* 핸들을 안전하게 IPhysicsActor*로 변환
    // worldEpoch 검증 + IsValid() 체크를 강제함
    IPhysicsActor* ValidateAndGetActor(void* handle, Alice::EntityId entityId) const noexcept;
    IRigidBody* ValidateAndGetRigidBody(void* handle, Alice::EntityId entityId) const noexcept;
    IPhysicsJoint* ValidateAndGetJoint(void* handle, Alice::EntityId entityId) const noexcept;
    ICharacterController* ValidateAndGetController(void* handle, Alice::EntityId entityId) const noexcept;

    // 유틸리티: Quat → Euler 변환
    static DirectX::XMFLOAT3 ToEulerRadians(const Quat& q);

    // 레이어 마스크 유틸리티
    static constexpr uint32_t kMaxLayers = MAX_PHYSICS_LAYERS;
    using LayerMaskArray = std::array<uint32_t, kMaxLayers>;

    static constexpr uint32_t AllLayersMask() noexcept
	{
		constexpr uint32_t W = std::numeric_limits<uint32_t>::digits; // 보통 32
		constexpr uint32_t n = (kMaxLayers > W ? W : kMaxLayers);      // 0..W로 클램프
		return static_cast<uint32_t>((1ull << n) - 1ull);
	}

    static LayerMaskArray MakeAllMaskArray() noexcept;

private:
    // 컴포넌트 → 물리 액터 생성
    void CreatePhysicsActor(Alice::EntityId entityId);
    void DestroyPhysicsActor(Alice::EntityId entityId);
    
    // HeightField 전용 생성 (Phy_TerrainHeightFieldComponent)
    void CreateTerrainHeightField(Alice::EntityId entityId);

    // Game → Physics 동기화
    void SyncGameToPhysics(Alice::EntityId entityId, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation);


    // Transform을 Vec3/Quat로 변환
    static Vec3 ToVec3(const DirectX::XMFLOAT3& v);
    static Quat ToQuat(const DirectX::XMFLOAT3& eulerRadians);
    static DirectX::XMFLOAT3 ToXMFLOAT3(const Vec3& v);

private:
    Alice::World& m_world;
    // shared_ptr로 보관하여 수명 안전성 확보 (씬 전환 시 old world가 먼저 파괴되는 것을 방지)
    std::shared_ptr<IPhysicsWorld> m_physicsWorldShared;
    IPhysicsWorld* m_physicsWorld = nullptr; // m_physicsWorldShared.get()과 동기화

    struct ActorHandle
    {
        std::unique_ptr<IPhysicsActor> owned;
        IRigidBody* rigid = nullptr;
        
        ActorHandle() = default;
        
        explicit ActorHandle(std::unique_ptr<IPhysicsActor> actor)
            : owned(std::move(actor))
            , rigid(nullptr)
        {
        }
        
        explicit ActorHandle(std::unique_ptr<IRigidBody> body)
            : owned(std::move(body))
            , rigid(static_cast<IRigidBody*>(owned.get()))
        {
        }
        
        bool IsValid() const { return owned && owned->IsValid(); }
        
        IPhysicsActor* GetActor() const { return owned.get(); }
        
        IRigidBody* GetRigidBody() const { return rigid; }
        
        void Destroy()
        {
            if (owned)
            {
                owned->Destroy();
                owned.reset();
            }
            rigid = nullptr;
        }
    };
    std::unordered_map<Alice::EntityId, ActorHandle> m_entityToActor;

    // 이전 프레임의 Transform 상태 (변경 감지용)
    struct TransformState
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 rotation{};
        DirectX::XMFLOAT3 scale{};
    };
    std::unordered_map<Alice::EntityId, TransformState> m_lastTransforms;

    // 이전 프레임의 Collider 상태 (변경 감지 및 Shape 재구성용)
    struct ColliderState    
    {
        ColliderType type{};
        DirectX::XMFLOAT3 halfExtents{};
        DirectX::XMFLOAT3 offset{};
        float radius{};
        float capsuleRadius{};
        float capsuleHalfHeight{};
        bool capsuleAlignYAxis{};
        float staticFriction{};
        float dynamicFriction{};
        float restitution{};
        uint32_t layerBits{};
        // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
        uint32_t ignoreLayers{}; // ignoreLayers 변경 감지 추가
        bool isTrigger{};
        DirectX::XMFLOAT3 scale{}; // Transform scale 포함
    };
    std::unordered_map<Alice::EntityId, ColliderState> m_lastColliders;

    // MeshCollider 파라미터 변경 감지용
    struct MeshColliderState
    {
        MeshColliderType type{};
        std::string meshAssetPath;
        float staticFriction{};
        float dynamicFriction{};
        float restitution{};
        uint32_t layerBits{};
        // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
        uint32_t ignoreLayers{};
        bool isTrigger{};
        bool flipNormals{};
        bool doubleSidedQueries{};
        bool validate{};
        bool shiftVertices{};
        uint32_t vertexLimit{};
        DirectX::XMFLOAT3 scale{};
    };
    std::unordered_map<Alice::EntityId, MeshColliderState> m_lastMeshColliders;

    // RigidBody 파라미터 변경 감지용
    struct RigidBodyState
    {
        float density{};
        float massOverride{};
        bool isKinematic{};
        bool gravityEnabled{};
        bool startAwake{};
        bool enableCCD{};
        bool enableSpeculativeCCD{};
        RigidBodyLockFlags lockFlags{};
        float linearDamping{};
        float angularDamping{};
        float maxLinearVelocity{};
        float maxAngularVelocity{};
        uint32_t solverPositionIterations{};
        uint32_t solverVelocityIterations{};
        float sleepThreshold{};
        float stabilizationThreshold{};
    };
    std::unordered_map<Alice::EntityId, RigidBodyState> m_lastRigidBodies;

    // Terrain 파라미터 변경 감지용
    struct TerrainState
    {
        uint32_t layerBits{};
        uint32_t ignoreLayers{};
        // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
        
        // 지형 형상이 바뀌었는지 감지용
        uint64_t lastGeomKey = 0;

        TerrainState() = default;

        TerrainState(uint32_t inLayerBits,
                     uint32_t inIgnoreLayers,
                     uint64_t inGeomKey) noexcept
            : layerBits(inLayerBits)
            , ignoreLayers(inIgnoreLayers)
            , lastGeomKey(inGeomKey)
        {}

        bool GeometryChanged(const TerrainState& prev) const noexcept
        {
            return lastGeomKey != prev.lastGeomKey;
        }

        bool MasksChanged(const TerrainState& prev) const noexcept
        {
            return layerBits != prev.layerBits ||
                   ignoreLayers != prev.ignoreLayers;
        }
    };
    std::unordered_map<Alice::EntityId, TerrainState> m_lastTerrains;

    // Character Controller 상태 변경 감지용
    struct CCTState
    {
        float radius{};
        float halfHeight{};
        float stepOffset{};
        float contactOffset{};
        float slopeLimitRadians{};
        CCTNonWalkableMode nonWalkableMode{};
        CCTCapsuleClimbingMode climbingMode{};
        float density{};
        bool enableQueries{};
        uint32_t layerBits{};
        // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
        uint32_t ignoreLayers{}; // ignoreLayers 변경 감지 추가
        bool hitTriggers{};
        DirectX::XMFLOAT3 scale{}; // Transform scale 포함
        // 참고: applyGravity, gravity, jumpSpeed는 매 프레임 직접 사용되므로 변경 감지 불필요

        CCTState() = default;

        // TransformComponent 의존 없애려고 scale만 받음 (헤더에서 TransformComponent 몰라도 됨)
        explicit CCTState(const Phy_CCTComponent& ccc,
                         const DirectX::XMFLOAT3& inScale) noexcept
            : radius(ccc.radius)
            , halfHeight(ccc.halfHeight)
            , stepOffset(ccc.stepOffset)
            , contactOffset(ccc.contactOffset)
            , slopeLimitRadians(ccc.slopeLimitRadians)
            , nonWalkableMode(ccc.nonWalkableMode)
            , climbingMode(ccc.climbingMode)
            , density(ccc.density)
            , enableQueries(ccc.enableQueries)
            , layerBits(ccc.layerBits)
            , ignoreLayers(ccc.ignoreLayers)
            , hitTriggers(ccc.hitTriggers)
            , scale(inScale)
        {}

        // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
        // OverrideMasks는 더 이상 사용되지 않음

        bool NeedsRebuild(const CCTState& prev) const noexcept
        {
            // Float 비교를 위한 epsilon (PhysicsSystem.cpp의 kFloatEpsilon과 동일)
            constexpr float kEpsilon = 1e-5f;
            auto FloatEqual = [](float a, float b) { return std::abs(a - b) < kEpsilon; };
            
            return !FloatEqual(radius, prev.radius) ||
                   !FloatEqual(halfHeight, prev.halfHeight) ||
                   !FloatEqual(stepOffset, prev.stepOffset) ||
                   !FloatEqual(contactOffset, prev.contactOffset) ||
                   !FloatEqual(slopeLimitRadians, prev.slopeLimitRadians) ||
                   nonWalkableMode != prev.nonWalkableMode ||
                   climbingMode != prev.climbingMode ||
                   !FloatEqual(density, prev.density) ||
                   enableQueries != prev.enableQueries ||
                   !FloatEqual(scale.x, prev.scale.x) ||
                   !FloatEqual(scale.y, prev.scale.y) ||
                   !FloatEqual(scale.z, prev.scale.z);
        }

        bool NeedsMaskUpdate(const CCTState& prev) const noexcept
        {
            return layerBits != prev.layerBits ||
                   ignoreLayers != prev.ignoreLayers ||
                   hitTriggers != prev.hitTriggers;
        }
    };
    std::unordered_map<Alice::EntityId, CCTState> m_lastCCTs;

    // Character Controller 핸들
    struct CCTHandle
    {
        std::unique_ptr<ICharacterController> owned;
        ICharacterController* cct = nullptr;

        CCTHandle() = default;
        explicit CCTHandle(std::unique_ptr<ICharacterController> in)
            : owned(std::move(in)), cct(owned.get()) {}

        bool IsValid() const { return owned && owned->IsValid(); }
        void Destroy()
        {
            if (owned) { owned->Destroy(); owned.reset(); }
            cct = nullptr;
        }
    };
    std::unordered_map<Alice::EntityId, CCTHandle> m_entityToCCT;

    // Character Controller 생성/삭제
    void CreateCharacterController(Alice::EntityId entityId);
    void DestroyCharacterController(Alice::EntityId entityId);

    // Collider/Scale 변경 시 Shape 재구성
    void RebuildShapes(Alice::EntityId entityId);
    void RebuildMeshShapes(Alice::EntityId entityId);

    // Joint 관리
    struct JointState
    {
        Phy_JointComponent snapshot{};
        Alice::EntityId targetId = Alice::InvalidEntityId;
        std::string targetName; // 캐싱: targetName이 같으면 재탐색 생략
    };
    std::unordered_map<Alice::EntityId, std::unique_ptr<IPhysicsJoint>> m_entityToJoint;
    std::unordered_map<Alice::EntityId, JointState> m_lastJoints;
    void DestroyJoint(Alice::EntityId entityId);

    // 이벤트 콜백
    EventCallback m_eventCallback = nullptr;
    void* m_eventCallbackUserData = nullptr;

    // 전역 필터 매트릭스 변경 감지용
    uint32_t m_lastFilterRevision = 0;

    // Ground Plane 상태
    struct GroundPlaneState
    {
        bool enabled{};
        float staticFriction{};
        float dynamicFriction{};
        float restitution{};
        uint32_t layerBits{};
        uint32_t collideMask{};
        uint32_t queryMask{};
        uint32_t ignoreLayers{};
        bool isTrigger{};
    };
    std::unique_ptr<IPhysicsActor> m_groundPlaneActor;
    GroundPlaneState m_lastGroundPlane{};

    // Mesh asset access
    class Alice::SkinnedMeshRegistry* m_skinnedRegistry = nullptr;

    // 성능 최적화: 매 프레임 재사용할 임시 컨테이너들
    mutable std::unordered_set<Alice::EntityId> m_tempEntitiesWithRigidBody;
    mutable std::unordered_set<Alice::EntityId> m_tempEntitiesWithMeshCollider;
    mutable std::unordered_set<Alice::EntityId> m_tempEntitiesWithJoint;

    // CCT 경고 엔티티 추적 (씬/월드 경계를 넘어서 상태가 남지 않도록 멤버로 관리)
    std::unordered_set<Alice::EntityId> m_warnedMissingCCT;

    // 런타임 마스크 캐시 (레이어 매트릭스 반영 결과)
    // 컴포넌트의 collideMask/queryMask는 authoring 데이터로 유지하고,
    // 실제 적용되는 필터는 이 캐시에서 관리
    struct RuntimeMasks
    {
        uint32_t collideMask = 0xFFFFFFFFu;
        uint32_t queryMask = 0xFFFFFFFFu;
    };
    std::unordered_map<Alice::EntityId, RuntimeMasks> m_runtimeColliderMasks;
    std::unordered_map<Alice::EntityId, RuntimeMasks> m_runtimeMeshColliderMasks;
    std::unordered_map<Alice::EntityId, RuntimeMasks> m_runtimeTerrainMasks;
    std::unordered_map<Alice::EntityId, RuntimeMasks> m_runtimeCCTMasks;
    
    // 레이어 매트릭스 기반으로 런타임 마스크 계산
    RuntimeMasks ComputeRuntimeMasks(uint32_t layerBits, uint32_t ignoreLayers) const;
};
