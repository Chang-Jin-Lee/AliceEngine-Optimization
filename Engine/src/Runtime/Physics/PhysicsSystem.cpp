#include "PhysicsSystem.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Physics/Components/Phy_SettingsComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Foundation/ThreadSafety.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/Data/Vertex.h"
#include <DirectXMath.h>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <bit>
#include <cstring>
#include <cassert>
#include <vector>

using namespace DirectX;
using namespace Alice;

// 레이어 비트에서 첫 번째 레이어 인덱스를 찾는 헬퍼 함수
static int FirstLayerIndex(uint32_t bits)
{
	if (bits == 0) return -1;
	return (int)std::countr_zero(bits); // C++20
}

// layerBits를 단일 비트로 강제하는 sanitize 함수
// 여러 비트가 설정되어 있으면 첫 번째 비트만 사용하고 경고 출력
static uint32_t SanitizeLayerBits(uint32_t bits, const char* what, EntityId id)
{
	if (bits == 0)
	{
		ALICE_LOG_WARN("[PhysicsSystem] %s: layerBits is 0 (entity: %llu). Using default layer 0.",
			what, (unsigned long long)id);
		return 1u << 0; // 기본값: 레이어 0
	}
	
	// 여러 비트가 설정되어 있는지 확인 (bits & (bits-1)) != 0
	if ((bits & (bits - 1)) != 0)
	{
		int firstIdx = FirstLayerIndex(bits);
		uint32_t sanitized = 1u << firstIdx;
		ALICE_LOG_WARN("[PhysicsSystem] %s: layerBits has multiple bits set (0x%08X, entity: %llu). Using first layer %d (0x%08X).",
			what, bits, (unsigned long long)id, firstIdx, sanitized);
		return sanitized;
	}
	
	return bits; // 단일 비트만 설정되어 있으면 그대로 반환
}

static void* MakeUserData(uint64_t worldEpoch, EntityId entityId) noexcept
{
	const uint64_t combined = (worldEpoch << 32) | (static_cast<uint64_t>(entityId) + 1u);
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(combined));
}

static uint64_t HashCombine64(uint64_t a, uint64_t b) noexcept
{
	a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
	return a;
}

static uint64_t MakeTerrainGeomKey(const Phy_TerrainHeightFieldComponent& t) noexcept
{
	uint64_t key = 0;
	key = HashCombine64(key, static_cast<uint64_t>(t.numRows));
	key = HashCombine64(key, static_cast<uint64_t>(t.numCols));
	
	uint32_t heightScaleBits = 0;
	std::memcpy(&heightScaleBits, &t.heightScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(heightScaleBits));
	
	uint32_t rowScaleBits = 0;
	std::memcpy(&rowScaleBits, &t.rowScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(rowScaleBits));
	
	uint32_t colScaleBits = 0;
	std::memcpy(&colScaleBits, &t.colScale, sizeof(float));
	key = HashCombine64(key, static_cast<uint64_t>(colScaleBits));
	
	key = HashCombine64(key, static_cast<uint64_t>(t.heightSamples.size()));
	
	// heightSamples 내용 해시 추가 (높이값 변경 감지)
	// 성능을 위해 샘플 일부만 해시에 포함 (매 8번째 샘플)
	if (!t.heightSamples.empty())
	{
		const size_t step = std::max<size_t>(1, t.heightSamples.size() / 256); // 최대 256개 샘플만 사용
		for (size_t i = 0; i < t.heightSamples.size(); i += step)
		{
			uint32_t sampleBits = 0;
			std::memcpy(&sampleBits, &t.heightSamples[i], sizeof(float));
			key = HashCombine64(key, static_cast<uint64_t>(sampleBits));
		}
	}
	
	return key;
}

static std::string ResolveMeshAssetPath(const World& world,
	                                    EntityId entityId,
	                                    const Phy_MeshColliderComponent& mesh) 
{
	if (!mesh.meshAssetPath.empty())
		return mesh.meshAssetPath;

	if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId))
	{
		if (!skinned->meshAssetPath.empty())
			return skinned->meshAssetPath;
	}

	return {};
}

static bool BuildMeshBuffers(const SkinnedMeshRegistry* registry,
	                         const std::string& assetPath,
	                         std::vector<Vec3>& outVertices,
	                         std::vector<uint32_t>& outIndices)
{
	outVertices.clear();
	outIndices.clear();

	if (!registry || assetPath.empty())
		return false;

	auto mesh = registry->Find(assetPath);
	if (!mesh || !mesh->sourceModel)
		return false;

	const auto& verts = mesh->sourceModel->GetCPUVertices();
	const auto& indices = mesh->sourceModel->GetCPUIndices();
	if (verts.empty() || indices.empty())
		return false;

	outVertices.reserve(verts.size());
	for (const auto& v : verts)
		outVertices.emplace_back(v.pos.x, v.pos.y, v.pos.z);

	outIndices = indices;
	return !outVertices.empty() && !outIndices.empty();
}

// Float 비교를 위한 epsilon (일반적으로 1e-5 정도)
static constexpr float kFloatEpsilon = 1e-5f;

static bool FloatEqual(float a, float b) noexcept
{
	return std::abs(a - b) < kFloatEpsilon;
}

static bool Float3Equal(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
{
	return FloatEqual(a.x, b.x) && FloatEqual(a.y, b.y) && FloatEqual(a.z, b.z);
}

static bool JointFrameEqual(const Phy_JointFrame& a, const Phy_JointFrame& b) noexcept
{
	return Float3Equal(a.position, b.position) && Float3Equal(a.rotation, b.rotation);
}

static bool RevoluteEqual(const Phy_RevoluteJointSettings& a, const Phy_RevoluteJointSettings& b) noexcept
{
	// 구조적 설정만 비교 (재생성 트리거)
	// driveVelocity, driveForceLimit는 런타임 제어값이므로 재생성 트리거에서 제외
	return a.enableLimit == b.enableLimit &&
		FloatEqual(a.lowerLimit, b.lowerLimit) &&
		FloatEqual(a.upperLimit, b.upperLimit) &&
		FloatEqual(a.limitStiffness, b.limitStiffness) &&
		FloatEqual(a.limitDamping, b.limitDamping) &&
		FloatEqual(a.limitRestitution, b.limitRestitution) &&
		FloatEqual(a.limitBounceThreshold, b.limitBounceThreshold) &&
		a.enableDrive == b.enableDrive &&
		// driveVelocity, driveForceLimit는 런타임 제어이므로 제외
		a.driveFreeSpin == b.driveFreeSpin &&
		a.driveLimitsAreForces == b.driveLimitsAreForces;
}

static bool PrismaticEqual(const Phy_PrismaticJointSettings& a, const Phy_PrismaticJointSettings& b) noexcept
{
	return a.enableLimit == b.enableLimit &&
		FloatEqual(a.lowerLimit, b.lowerLimit) &&
		FloatEqual(a.upperLimit, b.upperLimit) &&
		FloatEqual(a.limitStiffness, b.limitStiffness) &&
		FloatEqual(a.limitDamping, b.limitDamping) &&
		FloatEqual(a.limitRestitution, b.limitRestitution) &&
		FloatEqual(a.limitBounceThreshold, b.limitBounceThreshold);
}

static bool DistanceEqual(const Phy_DistanceJointSettings& a, const Phy_DistanceJointSettings& b) noexcept
{
	return FloatEqual(a.minDistance, b.minDistance) &&
		FloatEqual(a.maxDistance, b.maxDistance) &&
		FloatEqual(a.tolerance, b.tolerance) &&
		a.enableMinDistance == b.enableMinDistance &&
		a.enableMaxDistance == b.enableMaxDistance &&
		a.enableSpring == b.enableSpring &&
		FloatEqual(a.stiffness, b.stiffness) &&
		FloatEqual(a.damping, b.damping);
}

static bool SphericalEqual(const Phy_SphericalJointSettings& a, const Phy_SphericalJointSettings& b) noexcept
{
	return a.enableLimit == b.enableLimit &&
		FloatEqual(a.yLimitAngle, b.yLimitAngle) &&
		FloatEqual(a.zLimitAngle, b.zLimitAngle) &&
		FloatEqual(a.limitStiffness, b.limitStiffness) &&
		FloatEqual(a.limitDamping, b.limitDamping) &&
		FloatEqual(a.limitRestitution, b.limitRestitution) &&
		FloatEqual(a.limitBounceThreshold, b.limitBounceThreshold);
}

static bool D6DriveEqual(const Phy_D6JointDriveSettings& a, const Phy_D6JointDriveSettings& b) noexcept
{
	return FloatEqual(a.stiffness, b.stiffness) &&
		FloatEqual(a.damping, b.damping) &&
		FloatEqual(a.forceLimit, b.forceLimit) &&
		a.isAcceleration == b.isAcceleration;
}

static bool D6LinearEqual(const Phy_D6LinearLimitSettings& a, const Phy_D6LinearLimitSettings& b) noexcept
{
	return FloatEqual(a.lower, b.lower) &&
		FloatEqual(a.upper, b.upper) &&
		FloatEqual(a.stiffness, b.stiffness) &&
		FloatEqual(a.damping, b.damping) &&
		FloatEqual(a.restitution, b.restitution) &&
		FloatEqual(a.bounceThreshold, b.bounceThreshold);
}

static bool D6TwistEqual(const Phy_D6TwistLimitSettings& a, const Phy_D6TwistLimitSettings& b) noexcept
{
	return FloatEqual(a.lower, b.lower) &&
		FloatEqual(a.upper, b.upper) &&
		FloatEqual(a.stiffness, b.stiffness) &&
		FloatEqual(a.damping, b.damping) &&
		FloatEqual(a.restitution, b.restitution) &&
		FloatEqual(a.bounceThreshold, b.bounceThreshold);
}

static bool D6SwingEqual(const Phy_D6SwingLimitSettings& a, const Phy_D6SwingLimitSettings& b) noexcept
{
	return FloatEqual(a.yAngle, b.yAngle) &&
		FloatEqual(a.zAngle, b.zAngle) &&
		FloatEqual(a.stiffness, b.stiffness) &&
		FloatEqual(a.damping, b.damping) &&
		FloatEqual(a.restitution, b.restitution) &&
		FloatEqual(a.bounceThreshold, b.bounceThreshold);
}

static bool D6Equal(const Phy_D6JointSettings& a, const Phy_D6JointSettings& b) noexcept
{
	// 구조적 설정만 비교 (재생성 트리거)
	// drivePose, driveLinearVelocity, driveAngularVelocity도 구조 설정으로 취급 (런타임 갱신 API가 없으므로)
	return a.driveLimitsAreForces == b.driveLimitsAreForces &&
		a.motionX == b.motionX &&
		a.motionY == b.motionY &&
		a.motionZ == b.motionZ &&
		a.motionTwist == b.motionTwist &&
		a.motionSwing1 == b.motionSwing1 &&
		a.motionSwing2 == b.motionSwing2 &&
		D6LinearEqual(a.linearLimitX, b.linearLimitX) &&
		D6LinearEqual(a.linearLimitY, b.linearLimitY) &&
		D6LinearEqual(a.linearLimitZ, b.linearLimitZ) &&
		D6TwistEqual(a.twistLimit, b.twistLimit) &&
		D6SwingEqual(a.swingLimit, b.swingLimit) &&
		D6DriveEqual(a.driveX, b.driveX) &&
		D6DriveEqual(a.driveY, b.driveY) &&
		D6DriveEqual(a.driveZ, b.driveZ) &&
		D6DriveEqual(a.driveSwing, b.driveSwing) &&
		D6DriveEqual(a.driveTwist, b.driveTwist) &&
		D6DriveEqual(a.driveSlerp, b.driveSlerp) &&
		JointFrameEqual(a.drivePose, b.drivePose) &&
		Float3Equal(a.driveLinearVelocity, b.driveLinearVelocity) &&
		Float3Equal(a.driveAngularVelocity, b.driveAngularVelocity);
}

static bool JointSnapshotEqual(const Phy_JointComponent& a, const Phy_JointComponent& b) noexcept
{
	// 구조적 설정만 비교 (재생성 트리거)
	// breakForce, breakTorque, collideConnected는 in-place 업데이트 가능하므로 비교에서 제외
	return a.type == b.type &&
		a.targetName == b.targetName &&
		JointFrameEqual(a.frameA, b.frameA) &&
		JointFrameEqual(a.frameB, b.frameB) &&
		RevoluteEqual(a.revolute, b.revolute) &&
		PrismaticEqual(a.prismatic, b.prismatic) &&
		DistanceEqual(a.distance, b.distance) &&
		SphericalEqual(a.spherical, b.spherical) &&
		D6Equal(a.d6, b.d6);
}

static Phy_JointComponent MakeJointSnapshot(const Phy_JointComponent& src)
{
	Phy_JointComponent snap = src;
	snap.jointHandle = nullptr;
	return snap;
}

PhysicsSystem::LayerMaskArray PhysicsSystem::MakeAllMaskArray() noexcept
{
	LayerMaskArray a{};
	const uint32_t ALL = AllLayersMask();
	for (auto& v : a) v = ALL;
	return a;
}

PhysicsSystem::PhysicsSystem(World& world)
    : m_world(world)
{
}

PhysicsSystem::~PhysicsSystem()
{
    std::vector<EntityId> entityIds;
    entityIds.reserve(m_entityToActor.size());
    for (const auto& [entityId, handle] : m_entityToActor)
    {
        entityIds.push_back(entityId);
    }
    
    for (EntityId entityId : entityIds)
    {
        DestroyPhysicsActor(entityId);
    }
    
    m_entityToActor.clear();

    for (auto& [entityId, handle] : m_entityToCCT)
    {
        handle.Destroy();
    }
    m_entityToCCT.clear();

    m_entityToJoint.clear();
    m_lastJoints.clear();

    if (m_groundPlaneActor)
    {
        m_groundPlaneActor->Destroy();
        m_groundPlaneActor.reset();
    }
}

void PhysicsSystem::SetPhysicsWorld(IPhysicsWorld* physicsWorld)
{
	ThreadSafety::AssertMainThread();
	
	// 1) old world를 shared_ptr로 먼저 잡아둬서 teardown 동안 수명 보장
	auto oldShared = std::move(m_physicsWorldShared);
	IPhysicsWorld* oldWorld = oldShared.get();
	
	// 2) new world shared 보관
	if (physicsWorld != nullptr)
	{
		auto shared = m_world.GetPhysicsWorldShared();
		// 이 함수는 World가 가진 physicsWorld만 받는다 (수명 보장을 위해)
		assert(shared.get() == physicsWorld && "SetPhysicsWorld must receive World-owned physics world");
		m_physicsWorldShared = shared;
	}
	else
	{
		m_physicsWorldShared.reset();
	}
	
	m_physicsWorld = physicsWorld;

	// 3) 씬/월드 경계를 넘어서 상태가 남지 않도록 경고 세트 초기화
	m_warnedMissingCCT.clear();

    // 4) 기존 액터들 정리
    std::vector<EntityId> entityIds;
    entityIds.reserve(m_entityToActor.size());
    for (const auto& [entityId, handle] : m_entityToActor)
    {
        entityIds.push_back(entityId);
    }
    
        for (EntityId entityId : entityIds)
        {
            DestroyPhysicsActor(entityId);
        }
    
    m_entityToActor.clear();
    m_lastTransforms.clear();
    m_lastColliders.clear();
    m_lastMeshColliders.clear();
    m_lastRigidBodies.clear();
    m_lastTerrains.clear();

	// CCT 정리
    for (auto& [entityId, handle] : m_entityToCCT)
	{
		if (handle.IsValid())
    {
        handle.Destroy();
		}
    }
    m_entityToCCT.clear();
    m_lastCCTs.clear();

    // Joint 정리
    for (auto& [entityId, joint] : m_entityToJoint)
    {
        (void)joint;
        if (auto* comp = m_world.GetComponent<Phy_JointComponent>(entityId))
            comp->jointHandle = nullptr;
    }
    m_entityToJoint.clear();
    m_lastJoints.clear();

	// 4) teardown 끝난 뒤 oldWorld flush (누수/잔존 방지)
	if (oldWorld)
		oldWorld->Flush();

	// Ground Plane 정리
	if (m_groundPlaneActor)
	{
		m_groundPlaneActor->Destroy();
		m_groundPlaneActor.reset();
	}
    m_lastGroundPlane = GroundPlaneState{};
	
	m_lastFilterRevision = 0xFFFFFFFFu; // 강제로 다음 Update에서 1회 갱신

	// 런타임 마스크 캐시 정리
	m_runtimeColliderMasks.clear();
	m_runtimeMeshColliderMasks.clear();
	m_runtimeTerrainMasks.clear();
	m_runtimeCCTMasks.clear();

}

void PhysicsSystem::SetEventCallback(EventCallback callback, void* userData)
{
    m_eventCallback = callback;
    m_eventCallbackUserData = userData;
}

void PhysicsSystem::Update(float deltaTime)
{
	ThreadSafety::AssertMainThread();
    // World에서 shared_ptr을 가져와서 비교 (raw pointer 대신)
    // shared_ptr을 통해 수명 보장 (스코프 끝까지 월드가 살아있음)
    auto currentShared = m_world.GetPhysicsWorldShared();
    IPhysicsWorld* current = currentShared.get();
    
    if (current != m_physicsWorld) {
        // shared_ptr을 통해 SetPhysicsWorld 호출 (SetPhysicsWorld 내부에서 shared_ptr 보관)
        SetPhysicsWorld(current); // 바뀌었으면 정리+재바인딩
        // SetPhysicsWorld(nullptr)가 호출되면 m_entityToActor가 모두 클리어됨
        // 이후 로직은 실행할 필요 없음
        if (!m_physicsWorld) return;
    }
    // currentShared가 살아있음으로써 월드 수명 보장 (스코프 끝까지)

    if (!m_physicsWorld) return;

	// (추가) Scene Settings -> 각 컴포넌트 mask로 반영 (전역 매트릭스 기반)
	// 매 프레임 collideByLayer/queryByLayer를 계산하여 layerBits/ignoreLayers 변경 시에도 사용 가능하게 함
	// Phy_SettingsComponent가 없어도 기본값으로 전부 허용
	LayerMaskArray collideByLayer = MakeAllMaskArray();
	LayerMaskArray queryByLayer = MakeAllMaskArray();
	
	// Phy_SettingsComponent가 있으면 매트릭스 기반으로 덮어쓰기
	{
		const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();
		if (!settingsMap.empty())
		{
			const auto& s = settingsMap.begin()->second;

			// filterRevision 변경 감지: 전역 매트릭스가 변경되었는지 확인
			bool filterMatrixChanged = (s.filterRevision != m_lastFilterRevision);

			// 런타임 마스크 계산 헬퍼 함수
			auto ComputeRuntimeMasks = [&](uint32_t layerBits, uint32_t ignoreLayers, EntityId entityId) -> RuntimeMasks
			{
				RuntimeMasks masks{};
				// layerBits sanitize (단일 비트만 허용)
				uint32_t sanitized = SanitizeLayerBits(layerBits, "Update::ComputeRuntimeMasks", entityId);
				int li = FirstLayerIndex(sanitized);
				if (li >= 0 && li < MAX_PHYSICS_LAYERS)
				{
					masks.collideMask = collideByLayer[li];
					masks.queryMask = queryByLayer[li];
					masks.collideMask &= ~ignoreLayers;
					masks.queryMask &= ~ignoreLayers;
				}
				else
				{
					// 레이어가 유효하지 않으면 기본값
					masks.collideMask = 0xFFFFFFFFu;
					masks.queryMask = 0xFFFFFFFFu;
				}
				return masks;
			};

			// 매 프레임 collideByLayer/queryByLayer 계산 (성능 부담 거의 없음)
			// collide: row 기반 (layerCollideMatrix[i][j] = true면 레이어 i와 j가 충돌)
			for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
			{
				uint32_t mask = 0;
				for (int j = 0; j < MAX_PHYSICS_LAYERS; ++j)
					if (s.layerCollideMatrix[i][j]) mask |= (1u << j);

				// 원하면 자기 레이어는 자동 off:
				// mask &= ~(1u << i);

				collideByLayer[i] = mask;
			}

			// query: column 기반 (layerQueryMatrix[querier][target] = true면 querier가 target을 쿼리 가능)
			// target 레이어 입장에서 "누가 나를 쿼리할 수 있는가"를 마스크로 저장
			for (int target = 0; target < MAX_PHYSICS_LAYERS; ++target)
			{
				uint32_t mask = 0;
				for (int querier = 0; querier < MAX_PHYSICS_LAYERS; ++querier)
					if (s.layerQueryMatrix[querier][target]) mask |= (1u << querier);

				queryByLayer[target] = mask;
			}

			if (filterMatrixChanged)
			{
				m_lastFilterRevision = s.filterRevision;

				// Collider들에 적용 (전역 매트릭스 변경 시에만 전체 재계산)
				// 컴포넌트의 collideMask/queryMask는 authoring 데이터로 유지하고, 런타임 마스크는 캐시에서 관리
				auto colliders = m_world.GetComponents<Phy_ColliderComponent>();
				for (auto&& [id, col] : colliders)
				{
					uint32_t sanitized = SanitizeLayerBits(col.layerBits, "Collider", id);
					int li = FirstLayerIndex(sanitized);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~col.ignoreLayers;
					newQuery &= ~col.ignoreLayers;

					RuntimeMasks& runtime = m_runtimeColliderMasks[id];
					bool maskChanged = false;
					if (runtime.collideMask != newCollide)
					{
						runtime.collideMask = newCollide;
						maskChanged = true;
					}
					if (runtime.queryMask != newQuery)
					{
						runtime.queryMask = newQuery;
						maskChanged = true;
					}

					if (maskChanged)
					{
						auto it = m_entityToActor.find(id);
						if (it != m_entityToActor.end())
						{
							ActorHandle& handle = it->second;
							if (handle.IsValid() && handle.GetActor())
							{
								// PhysX에는 sanitize된 layerBits를 전달해야 함
								handle.GetActor()->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
							}
						}
					}
				}

				// MeshCollider에 적용 (전역 매트릭스 변경 시에만)
				// 컴포넌트의 collideMask/queryMask는 authoring 데이터로 유지하고, 런타임 마스크는 캐시에서 관리
				auto meshColliders = m_world.GetComponents<Phy_MeshColliderComponent>();
				for (auto&& [id, mc] : meshColliders)
				{
					uint32_t sanitized = SanitizeLayerBits(mc.layerBits, "MeshCollider", id);
					int li = FirstLayerIndex(sanitized);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~mc.ignoreLayers;
					newQuery &= ~mc.ignoreLayers;

					RuntimeMasks& runtime = m_runtimeMeshColliderMasks[id];
					bool maskChanged = false;
					if (runtime.collideMask != newCollide)
					{
						runtime.collideMask = newCollide;
						maskChanged = true;
					}
					if (runtime.queryMask != newQuery)
					{
						runtime.queryMask = newQuery;
						maskChanged = true;
					}

					if (maskChanged)
					{
						auto it = m_entityToActor.find(id);
						if (it != m_entityToActor.end())
						{
							ActorHandle& handle = it->second;
							if (handle.IsValid() && handle.GetActor())
							{
								// PhysX에는 sanitize된 layerBits를 전달해야 함
								handle.GetActor()->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
							}
						}
					}
				}

				// Terrain에 적용 (전역 매트릭스 변경 시에만)
				// 컴포넌트의 collideMask/queryMask는 authoring 데이터로 유지하고, 런타임 마스크는 캐시에서 관리
				auto terrains = m_world.GetComponents<Phy_TerrainHeightFieldComponent>();
				for (auto&& [id, terrain] : terrains)
				{
					uint32_t sanitized = SanitizeLayerBits(terrain.layerBits, "Terrain", id);
					int li = FirstLayerIndex(sanitized);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~terrain.ignoreLayers;
					newQuery &= ~terrain.ignoreLayers;

					RuntimeMasks& runtime = m_runtimeTerrainMasks[id];
					bool maskChanged = false;
					if (runtime.collideMask != newCollide)
					{
						runtime.collideMask = newCollide;
						maskChanged = true;
					}
					if (runtime.queryMask != newQuery)
					{
						runtime.queryMask = newQuery;
						maskChanged = true;
					}

					// Terrain 필터 변경 시 SetLayerMasks 사용 (재생성 대신)
					if (maskChanged)
					{
						auto itA = m_entityToActor.find(id);
						if (itA != m_entityToActor.end() && itA->second.IsValid() && itA->second.GetActor())
						{
							// PhysX에는 sanitize된 layerBits를 전달해야 함
							itA->second.GetActor()->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
						}
					}
				}

				// CCT에 적용 (전역 매트릭스 변경 시에만)
				// 컴포넌트의 collideMask/queryMask는 authoring 데이터로 유지하고, 런타임 마스크는 캐시에서 관리
				auto ccts = m_world.GetComponents<Phy_CCTComponent>();
				for (auto&& [id, cct] : ccts)
        {
					uint32_t sanitized = SanitizeLayerBits(cct.layerBits, "CCT", id);
					int li = FirstLayerIndex(sanitized);
					if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

					uint32_t newCollide = collideByLayer[li];
					uint32_t newQuery = queryByLayer[li];

					newCollide &= ~cct.ignoreLayers;
					newQuery &= ~cct.ignoreLayers;

					RuntimeMasks& runtime = m_runtimeCCTMasks[id];
					bool maskChanged = false;
					if (runtime.collideMask != newCollide)
					{
						runtime.collideMask = newCollide;
						maskChanged = true;
					}
					if (runtime.queryMask != newQuery)
					{
						runtime.queryMask = newQuery;
						maskChanged = true;
					}

					if (maskChanged)
					{
						auto itCCT = m_entityToCCT.find(id);
						if (itCCT != m_entityToCCT.end() && itCCT->second.IsValid())
						{
							// PhysX에는 sanitize된 layerBits를 전달해야 함
							itCCT->second.cct->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
        }
    }
				}
			}
		}

	}

	// Ground Plane (Scene Settings 기반)
	{
		const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();
		const Phy_SettingsComponent* settings = settingsMap.empty()
			? nullptr
			: &settingsMap.begin()->second;

		if (!settings || !settings->enableGroundPlane)
		{
			if (m_groundPlaneActor)
			{
				m_groundPlaneActor->Destroy();
				m_groundPlaneActor.reset();
			}
			m_lastGroundPlane = GroundPlaneState{};
		}
		else
		{
			// 런타임 마스크 계산 (컴포넌트는 const로만 읽고, 계산 결과는 GroundPlaneState에 저장)
			uint32_t newCollide = 0xFFFFFFFFu;
			uint32_t newQuery = 0xFFFFFFFFu;
			uint32_t sanitized = SanitizeLayerBits(settings->groundLayerBits, "GroundPlane", InvalidEntityId);
			int li = FirstLayerIndex(sanitized);
			if (li >= 0 && li < MAX_PHYSICS_LAYERS)
			{
				newCollide = collideByLayer[li];
				newQuery = queryByLayer[li];

				newCollide &= ~settings->groundIgnoreLayers;
				newQuery &= ~settings->groundIgnoreLayers;
			}

			GroundPlaneState cur{};
			cur.enabled = settings->enableGroundPlane;
			cur.staticFriction = settings->groundStaticFriction;
			cur.dynamicFriction = settings->groundDynamicFriction;
			cur.restitution = settings->groundRestitution;
			cur.layerBits = sanitized; // PhysX에 전달할 sanitize된 값
			cur.collideMask = newCollide; // 런타임 계산 결과
			cur.queryMask = newQuery;     // 런타임 계산 결과
			cur.ignoreLayers = settings->groundIgnoreLayers;
			cur.isTrigger = settings->groundIsTrigger;

			const bool needRebuild =
				!m_groundPlaneActor ||
				cur.enabled != m_lastGroundPlane.enabled ||
				!FloatEqual(cur.staticFriction, m_lastGroundPlane.staticFriction) ||
				!FloatEqual(cur.dynamicFriction, m_lastGroundPlane.dynamicFriction) ||
				!FloatEqual(cur.restitution, m_lastGroundPlane.restitution) ||
				cur.layerBits != m_lastGroundPlane.layerBits ||
				cur.collideMask != m_lastGroundPlane.collideMask ||
				cur.queryMask != m_lastGroundPlane.queryMask ||
				cur.ignoreLayers != m_lastGroundPlane.ignoreLayers ||
				cur.isTrigger != m_lastGroundPlane.isTrigger;

			if (needRebuild)
			{
				if (m_groundPlaneActor)
				{
					m_groundPlaneActor->Destroy();
					m_groundPlaneActor.reset();
				}

				FilterDesc filter{};
				filter.layerBits = cur.layerBits;
				filter.collideMask = cur.collideMask; // GroundPlaneState의 런타임 마스크 사용
				filter.queryMask = cur.queryMask;     // GroundPlaneState의 런타임 마스크 사용
				filter.isTrigger = cur.isTrigger;
				filter.userData = nullptr;

				m_groundPlaneActor = m_physicsWorld->CreateStaticPlaneActor(
					settings->groundStaticFriction,
					settings->groundDynamicFriction,
					settings->groundRestitution,
					filter);

				if (!m_groundPlaneActor)
				{
					ALICE_LOG_WARN("[PhysicsSystem] Ground Plane creation failed.");
				}
			}

			m_lastGroundPlane = cur;
		}
	}


    // 1. 컴포넌트 변경 감지 및 물리 액터 생성/삭제
    {
        // 성능 최적화: 멤버 변수 재사용 (할당/리해시 비용 절감)
        m_tempEntitiesWithRigidBody.clear();
        m_tempEntitiesWithMeshCollider.clear();
        
        auto rigidBodies = m_world.GetComponents<Phy_RigidBodyComponent>();
        for (const auto& [entityId, rb] : rigidBodies)
        {
            m_tempEntitiesWithRigidBody.insert(entityId);
            
            if (rb.physicsActorHandle == nullptr)
            {
                CreatePhysicsActor(entityId);
            }
        }

        auto meshColliders = m_world.GetComponents<Phy_MeshColliderComponent>();
        for (const auto& [entityId, mc] : meshColliders)
        {
            m_tempEntitiesWithMeshCollider.insert(entityId);

            const bool hasRB = (m_tempEntitiesWithRigidBody.find(entityId) != m_tempEntitiesWithRigidBody.end());
            if (!hasRB)
            {
                if (mc.physicsActorHandle == nullptr)
                {
                    if (m_entityToActor.find(entityId) != m_entityToActor.end())
                        DestroyPhysicsActor(entityId);
                    CreatePhysicsActor(entityId);
                }
            }
            else
            {
                auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
                auto* mesh = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
                if (rb && rb->physicsActorHandle && mesh && mesh->physicsActorHandle == nullptr)
                {
                    // IRigidBody*는 IPhysicsActor*로 암시적 변환 가능
                    mesh->physicsActorHandle = static_cast<IPhysicsActor*>(rb->physicsActorHandle);
                    RebuildMeshShapes(entityId);
                }
            }
        }

        auto colliders = m_world.GetComponents<Phy_ColliderComponent>();
        for (const auto& [entityId, collider] : colliders)
        {
            const bool hasRB = (m_tempEntitiesWithRigidBody.find(entityId) != m_tempEntitiesWithRigidBody.end());
            const bool hasMesh = (m_tempEntitiesWithMeshCollider.find(entityId) != m_tempEntitiesWithMeshCollider.end());

            if (hasMesh)
            {
                continue;
            }
            else if (!hasRB)
            {
                if (collider.physicsActorHandle == nullptr)
                    CreatePhysicsActor(entityId);
            }
            else
            {
                auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
				auto* col = m_world.GetComponent<Phy_ColliderComponent>(entityId);
				if (rb && rb->physicsActorHandle && col && col->physicsActorHandle == nullptr)
                {
					// IRigidBody*는 IPhysicsActor*로 암시적 변환 가능
					col->physicsActorHandle = static_cast<IPhysicsActor*>(rb->physicsActorHandle);
                    RebuildShapes(entityId);
                }
            }
        }

        {
            auto ccts = m_world.GetComponents<Phy_CCTComponent>();
            for (const auto& [entityId, cct] : ccts)
            {
                auto* ccc = m_world.GetComponent<Phy_CCTComponent>(entityId);
                if (!ccc) continue;

                auto itCCT = m_entityToCCT.find(entityId);
                if ((itCCT == m_entityToCCT.end() || !itCCT->second.IsValid()) || ccc->controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                }
            }
        }

        std::vector<EntityId> toRemove;
        for (const auto& [entityId, handle] : m_entityToActor)
        {
            auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
            auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
            auto* terrain = m_world.GetComponent<Phy_TerrainHeightFieldComponent>(entityId);
            
            if (!rb && !collider && !meshCollider && !terrain)
            {
                toRemove.push_back(entityId);
            }
        }

        for (EntityId entityId : toRemove)
        {
            DestroyPhysicsActor(entityId);
        }

        {
            std::vector<EntityId> cctToRemove;
            for (const auto& [entityId, h] : m_entityToCCT)
            {
                if (!m_world.GetComponent<Phy_CCTComponent>(entityId))
                    cctToRemove.push_back(entityId);
            }
            for (auto eid : cctToRemove)
                DestroyCharacterController(eid);
        }

        // Joint 생성/삭제 및 변경 감지
        {
            // 성능 최적화: 멤버 변수 재사용
            m_tempEntitiesWithJoint.clear();
            
            auto joints = m_world.GetComponents<Phy_JointComponent>();

            auto getActor = [&](EntityId id) -> IPhysicsActor*
            {
                auto it = m_entityToActor.find(id);
                if (it == m_entityToActor.end()) return nullptr;
                if (!it->second.IsValid()) return nullptr;
                return it->second.GetActor();
            };

            auto toJointFrame = [&](const Phy_JointFrame& f) -> JointFrame
            {
                JointFrame jf{};
                jf.position = ToVec3(f.position);
                jf.rotation = ToQuat(f.rotation);
                return jf;
            };

            auto toD6Motion = [](Phy_D6Motion m) -> D6Motion
            {
                switch (m)
                {
                case Phy_D6Motion::Limited: return D6Motion::Limited;
                case Phy_D6Motion::Free: return D6Motion::Free;
                default: return D6Motion::Locked;
                }
            };

            for (const auto& [entityId, jointComp] : joints)
            {
                m_tempEntitiesWithJoint.insert(entityId);
                auto* joint = m_world.GetComponent<Phy_JointComponent>(entityId);
                if (!joint) continue;

                if (joint->targetName.empty())
                {
                    DestroyJoint(entityId);
                    continue;
                }

                // targetName 캐싱: 이전 상태에서 targetName이 같으면 캐시된 targetId 사용
                EntityId targetId = InvalidEntityId;
                bool needResolve = true;
                auto itState = m_lastJoints.find(entityId);
                if (itState != m_lastJoints.end() && itState->second.targetName == joint->targetName)
                {
                    // 캐시된 targetId 사용
                    targetId = itState->second.targetId;
                    needResolve = false;
                }

                // 캐시가 없거나 targetName이 변경된 경우 재탐색
                if (needResolve)
                {
                    GameObject targetGo = m_world.FindGameObject(joint->targetName);
                    if (!targetGo.IsValid())
                    {
                        DestroyJoint(entityId);
                        continue;
                    }
                    targetId = targetGo.id();
                }

                IPhysicsActor* actorA = getActor(entityId);
                IPhysicsActor* actorB = getActor(targetId);
                if (!actorA || !actorB)
                {
                    DestroyJoint(entityId);
                    continue;
                }

                const Phy_JointComponent snapshot = MakeJointSnapshot(*joint);
                JointState newState{ snapshot, targetId, joint->targetName };

                bool hasJoint = false;
                auto itJoint = m_entityToJoint.find(entityId);
                if (itJoint != m_entityToJoint.end() && itJoint->second && itJoint->second->IsValid())
                    hasJoint = true;

                // breakForce/collideConnected는 in-place 업데이트 가능
                bool needsInPlaceUpdate = false;
                if (hasJoint && itState != m_lastJoints.end())
                {
                    const JointState& prev = itState->second;
                    if (prev.snapshot.breakForce != joint->breakForce ||
                        prev.snapshot.breakTorque != joint->breakTorque ||
                        prev.snapshot.collideConnected != joint->collideConnected)
                    {
                        needsInPlaceUpdate = true;
                    }
                }

                bool needsRebuild = !hasJoint;
                if (itState == m_lastJoints.end())
                {
                    needsRebuild = true;
                }
                else
                {
                    const JointState& prev = itState->second;
                    // targetName 변경 또는 구조적 설정 변경 시 재생성
                    if (prev.targetName != newState.targetName ||
                        prev.targetId != newState.targetId ||
                        !JointSnapshotEqual(prev.snapshot, newState.snapshot))
                    {
                        needsRebuild = true;
                    }
                }

                // In-place 업데이트 (breakForce/collideConnected)
                if (needsInPlaceUpdate && !needsRebuild && itJoint->second && itJoint->second->IsValid())
                {
                    //원래 이거 접근할 때, 씬 락 걸어야하는데 싱글 스레드 루프라 괜찮음 - 그래서 놔둠
                    itJoint->second->SetBreakForce(joint->breakForce, joint->breakTorque);
                    itJoint->second->SetCollideConnected(joint->collideConnected);
                    // 스냅샷 업데이트 (다음 프레임 재업데이트 방지)
                    newState.snapshot.breakForce = joint->breakForce;
                    newState.snapshot.breakTorque = joint->breakTorque;
                    newState.snapshot.collideConnected = joint->collideConnected;
                    m_lastJoints[entityId] = newState;
                    joint->jointHandle = itJoint->second.get();
                    continue;
                }

                if (!needsRebuild)
                {
                    joint->jointHandle = itJoint->second.get();
                    // 스냅샷 업데이트 (targetName은 이미 같음)
                    m_lastJoints[entityId] = newState;
                    continue;
                }

                DestroyJoint(entityId);

                std::unique_ptr<IPhysicsJoint> created{};
                void* userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

                switch (joint->type)
                {
                case Phy_JointType::Fixed:
                {
                    FixedJointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;
                    created = m_physicsWorld->CreateFixedJoint(*actorA, *actorB, desc);
                    break;
                }
                case Phy_JointType::Revolute:
                {
                    RevoluteJointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;

                    desc.enableLimit = joint->revolute.enableLimit;
                    desc.lowerLimit = joint->revolute.lowerLimit;
                    desc.upperLimit = joint->revolute.upperLimit;
                    desc.limitStiffness = joint->revolute.limitStiffness;
                    desc.limitDamping = joint->revolute.limitDamping;
                    desc.limitRestitution = joint->revolute.limitRestitution;
                    desc.limitBounceThreshold = joint->revolute.limitBounceThreshold;
                    desc.enableDrive = joint->revolute.enableDrive;
                    desc.driveVelocity = joint->revolute.driveVelocity;
                    desc.driveForceLimit = joint->revolute.driveForceLimit;
                    desc.driveFreeSpin = joint->revolute.driveFreeSpin;
                    desc.driveLimitsAreForces = joint->revolute.driveLimitsAreForces;

                    created = m_physicsWorld->CreateRevoluteJoint(*actorA, *actorB, desc);
                    break;
                }
                case Phy_JointType::Prismatic:
                {
                    PrismaticJointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;

                    desc.enableLimit = joint->prismatic.enableLimit;
                    desc.lowerLimit = joint->prismatic.lowerLimit;
                    desc.upperLimit = joint->prismatic.upperLimit;
                    desc.limitStiffness = joint->prismatic.limitStiffness;
                    desc.limitDamping = joint->prismatic.limitDamping;
                    desc.limitRestitution = joint->prismatic.limitRestitution;
                    desc.limitBounceThreshold = joint->prismatic.limitBounceThreshold;

                    created = m_physicsWorld->CreatePrismaticJoint(*actorA, *actorB, desc);
                    break;
                }
                case Phy_JointType::Distance:
                {
                    DistanceJointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;

                    desc.minDistance = joint->distance.minDistance;
                    desc.maxDistance = joint->distance.maxDistance;
                    desc.tolerance = joint->distance.tolerance;
                    desc.enableMinDistance = joint->distance.enableMinDistance;
                    desc.enableMaxDistance = joint->distance.enableMaxDistance;
                    desc.enableSpring = joint->distance.enableSpring;
                    desc.stiffness = joint->distance.stiffness;
                    desc.damping = joint->distance.damping;

                    created = m_physicsWorld->CreateDistanceJoint(*actorA, *actorB, desc);
                    break;
                }
                case Phy_JointType::Spherical:
                {
                    SphericalJointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;

                    desc.enableLimit = joint->spherical.enableLimit;
                    desc.yLimitAngle = joint->spherical.yLimitAngle;
                    desc.zLimitAngle = joint->spherical.zLimitAngle;
                    desc.limitStiffness = joint->spherical.limitStiffness;
                    desc.limitDamping = joint->spherical.limitDamping;
                    desc.limitRestitution = joint->spherical.limitRestitution;
                    desc.limitBounceThreshold = joint->spherical.limitBounceThreshold;

                    created = m_physicsWorld->CreateSphericalJoint(*actorA, *actorB, desc);
                    break;
                }
                case Phy_JointType::D6:
                {
                    D6JointDesc desc{};
                    desc.frameA = toJointFrame(joint->frameA);
                    desc.frameB = toJointFrame(joint->frameB);
                    desc.breakForce = joint->breakForce;
                    desc.breakTorque = joint->breakTorque;
                    desc.collideConnected = joint->collideConnected;
                    desc.userData = userData;

                    desc.driveLimitsAreForces = joint->d6.driveLimitsAreForces;
                    desc.motionX = toD6Motion(joint->d6.motionX);
                    desc.motionY = toD6Motion(joint->d6.motionY);
                    desc.motionZ = toD6Motion(joint->d6.motionZ);
                    desc.motionTwist = toD6Motion(joint->d6.motionTwist);
                    desc.motionSwing1 = toD6Motion(joint->d6.motionSwing1);
                    desc.motionSwing2 = toD6Motion(joint->d6.motionSwing2);

                    desc.linearLimitX = { joint->d6.linearLimitX.lower, joint->d6.linearLimitX.upper,
                                          joint->d6.linearLimitX.stiffness, joint->d6.linearLimitX.damping,
                                          joint->d6.linearLimitX.restitution, joint->d6.linearLimitX.bounceThreshold };
                    desc.linearLimitY = { joint->d6.linearLimitY.lower, joint->d6.linearLimitY.upper,
                                          joint->d6.linearLimitY.stiffness, joint->d6.linearLimitY.damping,
                                          joint->d6.linearLimitY.restitution, joint->d6.linearLimitY.bounceThreshold };
                    desc.linearLimitZ = { joint->d6.linearLimitZ.lower, joint->d6.linearLimitZ.upper,
                                          joint->d6.linearLimitZ.stiffness, joint->d6.linearLimitZ.damping,
                                          joint->d6.linearLimitZ.restitution, joint->d6.linearLimitZ.bounceThreshold };

                    desc.twistLimit = { joint->d6.twistLimit.lower, joint->d6.twistLimit.upper,
                                        joint->d6.twistLimit.stiffness, joint->d6.twistLimit.damping,
                                        joint->d6.twistLimit.restitution, joint->d6.twistLimit.bounceThreshold };

                    desc.swingLimit = { joint->d6.swingLimit.yAngle, joint->d6.swingLimit.zAngle,
                                        joint->d6.swingLimit.stiffness, joint->d6.swingLimit.damping,
                                        joint->d6.swingLimit.restitution, joint->d6.swingLimit.bounceThreshold };

                    desc.driveX = { joint->d6.driveX.stiffness, joint->d6.driveX.damping,
                                    joint->d6.driveX.forceLimit, joint->d6.driveX.isAcceleration };
                    desc.driveY = { joint->d6.driveY.stiffness, joint->d6.driveY.damping,
                                    joint->d6.driveY.forceLimit, joint->d6.driveY.isAcceleration };
                    desc.driveZ = { joint->d6.driveZ.stiffness, joint->d6.driveZ.damping,
                                    joint->d6.driveZ.forceLimit, joint->d6.driveZ.isAcceleration };
                    desc.driveSwing = { joint->d6.driveSwing.stiffness, joint->d6.driveSwing.damping,
                                        joint->d6.driveSwing.forceLimit, joint->d6.driveSwing.isAcceleration };
                    desc.driveTwist = { joint->d6.driveTwist.stiffness, joint->d6.driveTwist.damping,
                                        joint->d6.driveTwist.forceLimit, joint->d6.driveTwist.isAcceleration };
                    desc.driveSlerp = { joint->d6.driveSlerp.stiffness, joint->d6.driveSlerp.damping,
                                        joint->d6.driveSlerp.forceLimit, joint->d6.driveSlerp.isAcceleration };

                    desc.drivePose = toJointFrame(joint->d6.drivePose);
                    desc.driveLinearVelocity = ToVec3(joint->d6.driveLinearVelocity);
                    desc.driveAngularVelocity = ToVec3(joint->d6.driveAngularVelocity);

                    created = m_physicsWorld->CreateD6Joint(*actorA, *actorB, desc);
                    break;
                }
                }

                if (created)
                {
                    IPhysicsJoint* raw = created.get();
                    m_entityToJoint[entityId] = std::move(created);
                    m_lastJoints[entityId] = newState;
                    joint->jointHandle = raw;
                }
                else
                {
                    joint->jointHandle = nullptr;
                    m_lastJoints.erase(entityId);
                }
            }

            std::vector<EntityId> jointToRemove;
            for (const auto& [entityId, handle] : m_entityToJoint)
            {
                if (m_tempEntitiesWithJoint.find(entityId) == m_tempEntitiesWithJoint.end())
                    jointToRemove.push_back(entityId);
            }
            for (auto eid : jointToRemove)
                DestroyJoint(eid);
        }
    }

    // 2. Game → Physics 동기화
    {
        auto transforms = m_world.GetComponents<TransformComponent>();
        for (const auto& [entityId, transform] : transforms)
        {
            if (!transform.enabled) continue;
            
            auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
            auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
            auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
            
            if (!rb && !collider && !meshCollider) continue;

            auto it = m_lastTransforms.find(entityId);
            bool needsSync = false;

            if (it == m_lastTransforms.end())
            {
                needsSync = true;
                m_lastTransforms[entityId] = {
                    transform.position,
                    transform.rotation,
                    transform.scale
                };
            }
            else
            {
                // 변경 감지
                const auto& last = it->second;
                if (transform.position.x != last.position.x || transform.position.y != last.position.y || transform.position.z != last.position.z ||
                    transform.rotation.x != last.rotation.x || transform.rotation.y != last.rotation.y || transform.rotation.z != last.rotation.z ||
                    transform.scale.x != last.scale.x || transform.scale.y != last.scale.y || transform.scale.z != last.scale.z)
                {
                    needsSync = true;
                    it->second = {
                        transform.position,
                        transform.rotation,
                        transform.scale
                    };
                }
            }

            if (needsSync)
            {
                SyncGameToPhysics(entityId, transform.position, transform.rotation);
            }
        }
    }

    // 3. Collider 변경 감지 및 Shape 재구성
    {
        auto colliders = m_world.GetComponents<Phy_ColliderComponent>();
        for (const auto& [entityId, collider] : colliders)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled) continue;
            if (m_world.GetComponent<Phy_MeshColliderComponent>(entityId))
                continue;

            auto it = m_lastColliders.find(entityId);
            bool needsRebuild = false;

            if (it == m_lastColliders.end())
            {
                ColliderState state{};
                state.type = collider.type;
                state.halfExtents = collider.halfExtents;
                state.offset = collider.offset;
                state.radius = collider.radius;
                state.capsuleRadius = collider.capsuleRadius;
                state.capsuleHalfHeight = collider.capsuleHalfHeight;
                state.capsuleAlignYAxis = collider.capsuleAlignYAxis;
                state.staticFriction = collider.staticFriction;
                state.dynamicFriction = collider.dynamicFriction;
                state.restitution = collider.restitution;
                state.layerBits = collider.layerBits;
				state.ignoreLayers = collider.ignoreLayers;
                state.isTrigger = collider.isTrigger;
                state.scale = transform->scale;
                m_lastColliders[entityId] = state;
            }
            else
            {
                const auto& last = it->second;
                bool changed = false;
				bool maskOnlyChanged = false;

				bool layerOrIgnoreChanged = (collider.layerBits != last.layerBits || collider.ignoreLayers != last.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					uint32_t sanitized = SanitizeLayerBits(collider.layerBits, "Collider", entityId);
					int li = FirstLayerIndex(sanitized);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						newCollide &= ~collider.ignoreLayers;
						newQuery &= ~collider.ignoreLayers;
						
						// 런타임 마스크 캐시 업데이트 (컴포넌트는 authoring 데이터로 유지)
						RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
						if (runtime.collideMask != newCollide || runtime.queryMask != newQuery)
						{
							runtime.collideMask = newCollide;
							runtime.queryMask = newQuery;
							maskOnlyChanged = true;
						}
					}
				}

                if (collider.type != last.type ||
                    !FloatEqual(collider.halfExtents.x, last.halfExtents.x) || !FloatEqual(collider.halfExtents.y, last.halfExtents.y) || !FloatEqual(collider.halfExtents.z, last.halfExtents.z) ||
                    !Float3Equal(collider.offset, last.offset) ||
                    !FloatEqual(collider.radius, last.radius) ||
                    !FloatEqual(collider.capsuleRadius, last.capsuleRadius) ||
                    !FloatEqual(collider.capsuleHalfHeight, last.capsuleHalfHeight) ||
                    collider.capsuleAlignYAxis != last.capsuleAlignYAxis ||
                    !FloatEqual(collider.staticFriction, last.staticFriction) ||
                    !FloatEqual(collider.dynamicFriction, last.dynamicFriction) ||
                    !FloatEqual(collider.restitution, last.restitution) ||
                    collider.isTrigger != last.isTrigger)
                {
                    changed = true;
                }

				// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)

                // Scale 변경
                if (!FloatEqual(transform->scale.x, last.scale.x) || 
                    !FloatEqual(transform->scale.y, last.scale.y) || 
                    !FloatEqual(transform->scale.z, last.scale.z))
                {
                    changed = true;
                }

				if (maskOnlyChanged && !changed)
				{
					auto itActor = m_entityToActor.find(entityId);
					if (itActor != m_entityToActor.end())
					{
						ActorHandle& handle = itActor->second;
						if (handle.IsValid() && handle.GetActor())
						{
							// 런타임 마스크 사용
							RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
							uint32_t sanitized = SanitizeLayerBits(collider.layerBits, "Collider", entityId);
							handle.GetActor()->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
						}
					}
				}

				if (changed || maskOnlyChanged)
				{
					if (changed) needsRebuild = true;
                    it->second.type = collider.type;
                    it->second.halfExtents = collider.halfExtents;
                    it->second.radius = collider.radius;
                    it->second.capsuleRadius = collider.capsuleRadius;
                    it->second.capsuleHalfHeight = collider.capsuleHalfHeight;
                    it->second.capsuleAlignYAxis = collider.capsuleAlignYAxis;
                    it->second.staticFriction = collider.staticFriction;
                    it->second.dynamicFriction = collider.dynamicFriction;
                    it->second.restitution = collider.restitution;
                    it->second.layerBits = collider.layerBits;
					it->second.ignoreLayers = collider.ignoreLayers;
                    it->second.isTrigger = collider.isTrigger;
                    it->second.scale = transform->scale;
                }
            }

            if (needsRebuild)
            {
                RebuildShapes(entityId);
            }
        }

        std::vector<EntityId> collidersToRemove;
        for (const auto& [entityId, state] : m_lastColliders)
        {
            auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
            if (!collider)
            {
                collidersToRemove.push_back(entityId);
            }
        }
        for (EntityId entityId : collidersToRemove)
        {
            auto itActor = m_entityToActor.find(entityId);
            if (itActor != m_entityToActor.end())
            {
                ActorHandle& handle = itActor->second;
                if (handle.IsValid())
                {
                    IPhysicsActor* actor = handle.GetActor();
                    if (actor && actor->IsValid())
                    {
                        actor->ClearShapes();

                        IRigidBody* body = handle.GetRigidBody();
                        if (body && body->IsValid())
                            body->RecomputeMass();
                    }
                }
            }

            m_lastColliders.erase(entityId);
        }
    }

    // 3.5 MeshCollider 변경 감지 및 Shape 재구성
    {
        auto meshColliders = m_world.GetComponents<Phy_MeshColliderComponent>();
        for (const auto& [entityId, mc] : meshColliders)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled) continue;

            const std::string resolvedPath = ResolveMeshAssetPath(m_world, entityId, mc);

            auto it = m_lastMeshColliders.find(entityId);
            bool needsRebuild = false;

            if (it == m_lastMeshColliders.end())
            {
                MeshColliderState state{};
                state.type = mc.type;
                state.meshAssetPath = resolvedPath;
                state.staticFriction = mc.staticFriction;
                state.dynamicFriction = mc.dynamicFriction;
                state.restitution = mc.restitution;
                state.layerBits = mc.layerBits;
                state.ignoreLayers = mc.ignoreLayers;
                state.isTrigger = mc.isTrigger;
                state.flipNormals = mc.flipNormals;
                state.doubleSidedQueries = mc.doubleSidedQueries;
                state.validate = mc.validate;
                state.shiftVertices = mc.shiftVertices;
                state.vertexLimit = mc.vertexLimit;
                state.scale = transform->scale;
                m_lastMeshColliders[entityId] = state;
            }
            else
            {
                const auto& last = it->second;
                bool changed = false;
                bool maskOnlyChanged = false;

                bool layerOrIgnoreChanged = (mc.layerBits != last.layerBits || mc.ignoreLayers != last.ignoreLayers);
                if (layerOrIgnoreChanged)
                {
                    uint32_t sanitized = SanitizeLayerBits(mc.layerBits, "MeshCollider", entityId);
                    int li = FirstLayerIndex(sanitized);
                    if (li >= 0 && li < MAX_PHYSICS_LAYERS)
                    {
                        uint32_t newCollide = collideByLayer[li];
                        uint32_t newQuery = queryByLayer[li];

                        newCollide &= ~mc.ignoreLayers;
                        newQuery &= ~mc.ignoreLayers;

                        // 런타임 마스크 캐시 업데이트 (컴포넌트는 authoring 데이터로 유지)
                        RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
                        if (runtime.collideMask != newCollide || runtime.queryMask != newQuery)
                        {
                            runtime.collideMask = newCollide;
                            runtime.queryMask = newQuery;
                            maskOnlyChanged = true;
                        }
                    }
                }

                if (mc.type != last.type ||
                    resolvedPath != last.meshAssetPath ||
                    !FloatEqual(mc.staticFriction, last.staticFriction) ||
                    !FloatEqual(mc.dynamicFriction, last.dynamicFriction) ||
                    !FloatEqual(mc.restitution, last.restitution) ||
                    mc.isTrigger != last.isTrigger ||
                    mc.flipNormals != last.flipNormals ||
                    mc.doubleSidedQueries != last.doubleSidedQueries ||
                    mc.validate != last.validate ||
                    mc.shiftVertices != last.shiftVertices ||
                    mc.vertexLimit != last.vertexLimit)
                {
                    changed = true;
                }

                // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)

                if (!FloatEqual(transform->scale.x, last.scale.x) ||
                    !FloatEqual(transform->scale.y, last.scale.y) ||
                    !FloatEqual(transform->scale.z, last.scale.z))
                {
                    changed = true;
                }

                if (maskOnlyChanged && !changed)
                {
                    auto itActor = m_entityToActor.find(entityId);
                    if (itActor != m_entityToActor.end())
                    {
                        ActorHandle& handle = itActor->second;
                        if (handle.IsValid() && handle.GetActor())
                        {
                            // 런타임 마스크 사용
                            RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
                            uint32_t sanitized = SanitizeLayerBits(mc.layerBits, "MeshCollider", entityId);
                            handle.GetActor()->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
                        }
                    }
                }

                if (changed || maskOnlyChanged)
                {
                    if (changed) needsRebuild = true;
                    it->second.type = mc.type;
                    it->second.meshAssetPath = resolvedPath;
                    it->second.staticFriction = mc.staticFriction;
                    it->second.dynamicFriction = mc.dynamicFriction;
                    it->second.restitution = mc.restitution;
                    it->second.layerBits = mc.layerBits;
                    it->second.ignoreLayers = mc.ignoreLayers;
                    it->second.isTrigger = mc.isTrigger;
                    it->second.flipNormals = mc.flipNormals;
                    it->second.doubleSidedQueries = mc.doubleSidedQueries;
                    it->second.validate = mc.validate;
                    it->second.shiftVertices = mc.shiftVertices;
                    it->second.vertexLimit = mc.vertexLimit;
                    it->second.scale = transform->scale;
                }
            }

            if (needsRebuild)
            {
                RebuildMeshShapes(entityId);
            }
        }

        std::vector<EntityId> meshToRemove;
        for (const auto& [entityId, state] : m_lastMeshColliders)
        {
            auto* mesh = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
            if (!mesh)
            {
                meshToRemove.push_back(entityId);
            }
        }
        for (EntityId entityId : meshToRemove)
        {
            auto itActor = m_entityToActor.find(entityId);
            if (itActor != m_entityToActor.end() && itActor->second.IsValid())
            {
                auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
                if (collider)
                {
                    ActorHandle& handle = itActor->second;
                    // IRigidBody가 있으면 IRigidBody* 사용, 없으면 IPhysicsActor* 사용
                    collider->physicsActorHandle = handle.GetRigidBody()
                        ? static_cast<IPhysicsActor*>(handle.GetRigidBody())
                        : handle.GetActor();
                    RebuildShapes(entityId);
                }
            }

            m_lastMeshColliders.erase(entityId);
        }
    }

    //  4. Phy_RigidBodyComponent 변경 감지
    {
        auto rigidBodies = m_world.GetComponents<Phy_RigidBodyComponent>();
        for (const auto& [entityId, rb] : rigidBodies)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled) continue;
            
            IRigidBody* body = nullptr;
            auto it = m_entityToActor.find(entityId);
            if (it != m_entityToActor.end())
                body = it->second.GetRigidBody();
            if (!body || !body->IsValid())
                continue;

            RigidBodyState cur{};
            cur.density = rb.density;
            cur.massOverride = rb.massOverride;
            cur.isKinematic = rb.isKinematic;
            cur.gravityEnabled = rb.gravityEnabled;
            cur.startAwake = rb.startAwake;
            cur.enableCCD = rb.enableCCD;
            cur.enableSpeculativeCCD = rb.enableSpeculativeCCD;
            cur.lockFlags = rb.lockFlags;
            cur.linearDamping = rb.linearDamping;
            cur.angularDamping = rb.angularDamping;
            cur.maxLinearVelocity = rb.maxLinearVelocity;
            cur.maxAngularVelocity = rb.maxAngularVelocity;
            cur.solverPositionIterations = rb.solverPositionIterations;
            cur.solverVelocityIterations = rb.solverVelocityIterations;
            cur.sleepThreshold = rb.sleepThreshold;
            cur.stabilizationThreshold = rb.stabilizationThreshold;

            auto lastIt = m_lastRigidBodies.find(entityId);
            if (lastIt == m_lastRigidBodies.end())
            {
                m_lastRigidBodies[entityId] = cur;
                continue;
            }

            const RigidBodyState& prev = lastIt->second;

            if (cur.isKinematic != prev.isKinematic) body->SetKinematic(cur.isKinematic);
            if (cur.gravityEnabled != prev.gravityEnabled) body->SetGravityEnabled(cur.gravityEnabled);

            if (cur.enableCCD != prev.enableCCD || cur.enableSpeculativeCCD != prev.enableSpeculativeCCD)
                body->SetCCDEnabled(cur.enableCCD, cur.enableSpeculativeCCD);

            if (cur.lockFlags != prev.lockFlags) body->SetLockFlags(cur.lockFlags);

            if (!FloatEqual(cur.linearDamping, prev.linearDamping) || !FloatEqual(cur.angularDamping, prev.angularDamping))
                body->SetDamping(cur.linearDamping, cur.angularDamping);

            if (!FloatEqual(cur.maxLinearVelocity, prev.maxLinearVelocity) || !FloatEqual(cur.maxAngularVelocity, prev.maxAngularVelocity))
                body->SetMaxVelocities(cur.maxLinearVelocity, cur.maxAngularVelocity);

            if (!FloatEqual(cur.density, prev.density) || !FloatEqual(cur.massOverride, prev.massOverride))
                body->SetMassProperties(cur.density, cur.massOverride);

            if (cur.solverPositionIterations != prev.solverPositionIterations ||
                cur.solverVelocityIterations != prev.solverVelocityIterations)
                body->SetSolverIterations(cur.solverPositionIterations, cur.solverVelocityIterations);

            if (!FloatEqual(cur.sleepThreshold, prev.sleepThreshold))
                body->SetSleepThreshold(cur.sleepThreshold);

            if (!FloatEqual(cur.stabilizationThreshold, prev.stabilizationThreshold))
                body->SetStabilizationThreshold(cur.stabilizationThreshold);

            if (cur.startAwake != prev.startAwake)
            {
                if (cur.startAwake) body->WakeUp();
                else body->PutToSleep();
            }

            m_lastRigidBodies[entityId] = cur;
        }
    }

	// 4. Terrain 변경 감지
    {
        auto terrains = m_world.GetComponents<Phy_TerrainHeightFieldComponent>();
        for (const auto& [entityId, terrain] : terrains)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled) continue;

            uint32_t sanitized = SanitizeLayerBits(terrain.layerBits, "Terrain", entityId);
            int li = FirstLayerIndex(sanitized);
            if (li < 0 || li >= MAX_PHYSICS_LAYERS) continue;

            uint32_t newCollide = collideByLayer[li];
            uint32_t newQuery = queryByLayer[li];

            newCollide &= ~terrain.ignoreLayers;
            newQuery &= ~terrain.ignoreLayers;

            // 런타임 마스크 캐시 업데이트 (컴포넌트는 authoring 데이터로 유지)
            RuntimeMasks& runtime = m_runtimeTerrainMasks[entityId];
            runtime.collideMask = newCollide;
            runtime.queryMask = newQuery;

            if (terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
            {
                const size_t expectedSamples = static_cast<size_t>(terrain.numRows) * static_cast<size_t>(terrain.numCols);
                terrain.heightSamples.resize(expectedSamples, 0.0f);
            }

            const uint64_t geomKey = MakeTerrainGeomKey(terrain);

            auto it = m_lastTerrains.find(entityId);
            if (it == m_lastTerrains.end())
            {
                TerrainState state{ terrain.layerBits, terrain.ignoreLayers, geomKey };
                m_lastTerrains[entityId] = state;

                if (!terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
                {
                    CreateTerrainHeightField(entityId);
                }
                continue;
            }

            TerrainState prev = it->second;
            TerrainState cur{ terrain.layerBits, terrain.ignoreLayers, geomKey };

            if (cur.GeometryChanged(prev))
            {
                if (terrain.physicsActorHandle != nullptr)
                {
                    DestroyPhysicsActor(entityId);

                    if (m_physicsWorld)
                    {
                        m_physicsWorld->Flush();
                    }
                }

                if (!terrain.heightSamples.empty() && terrain.numRows >= 2 && terrain.numCols >= 2)
                {
                    CreateTerrainHeightField(entityId);
                }

                m_lastTerrains[entityId] = cur;
                continue;
            }

            if (cur.MasksChanged(prev))
            {
                auto itActor = m_entityToActor.find(entityId);
                if (itActor != m_entityToActor.end())
                {
                    ActorHandle& handle = itActor->second;
                    if (handle.IsValid() && handle.GetActor())
                    {
                        // PhysX에는 sanitize된 layerBits를 전달해야 함
                        handle.GetActor()->SetLayerMasks(sanitized, newCollide, newQuery);
                    }
                }
            }

            it = m_lastTerrains.find(entityId);
            if (it != m_lastTerrains.end())
            {
                it->second = cur;
            }
        }

        std::vector<EntityId> toErase;
        for (auto& [eid, st] : m_lastTerrains)
        {
            if (!m_world.GetComponent<Phy_TerrainHeightFieldComponent>(eid))
                toErase.push_back(eid);
        }
        for (auto eid : toErase) m_lastTerrains.erase(eid);
    }

    // 5. CCT 변경 감지
    {
        auto ccts = m_world.GetComponents<Phy_CCTComponent>();
        
        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled) continue;

            auto itCCT = m_entityToCCT.find(entityId);
            
            CCTState cur{ ccc, transform->scale };

            auto itState = m_lastCCTs.find(entityId);
            if (itState == m_lastCCTs.end())
            {
                m_lastCCTs[entityId] = cur;
                auto* cccPtr = m_world.GetComponent<Phy_CCTComponent>(entityId);
                if (!cccPtr) continue;

                bool shouldCreate = (itCCT == m_entityToCCT.end() || !itCCT->second.IsValid() || cccPtr->controllerHandle == nullptr);
                if (shouldCreate)
                {
                    CreateCharacterController(entityId);
                }
            }
            else
            {
                const auto& prev = itState->second;
                bool needsRebuild = false;

				bool layerOrIgnoreChanged = (ccc.layerBits != prev.layerBits || ccc.ignoreLayers != prev.ignoreLayers);
				if (layerOrIgnoreChanged)
				{
					uint32_t sanitized = SanitizeLayerBits(ccc.layerBits, "CCT", entityId);
					int li = FirstLayerIndex(sanitized);
					if (li >= 0 && li < MAX_PHYSICS_LAYERS)
					{
						uint32_t newCollide = collideByLayer[li];
						uint32_t newQuery = queryByLayer[li];
						
						newCollide &= ~ccc.ignoreLayers;
						newQuery &= ~ccc.ignoreLayers;
						
						// 런타임 마스크 캐시 업데이트 (컴포넌트는 authoring 데이터로 유지)
						RuntimeMasks& runtime = m_runtimeCCTMasks[entityId];
						runtime.collideMask = newCollide;
						runtime.queryMask = newQuery;
						// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
                    }
				}
                
                if (cur.NeedsRebuild(prev))
                {
                    needsRebuild = true;
                }

                if (itCCT == m_entityToCCT.end() || !itCCT->second.IsValid() || ccc.controllerHandle == nullptr)
                {
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else if (needsRebuild)
                {
                    DestroyCharacterController(entityId);
                    CreateCharacterController(entityId);
                    m_lastCCTs[entityId] = cur;
                }
                else
                {
                    if (cur.NeedsMaskUpdate(prev))
                    {
                        ICharacterController* ctrl = itCCT->second.cct;
                        if (ctrl)
                        {
                            // 런타임 마스크 사용
                            RuntimeMasks& runtime = m_runtimeCCTMasks[entityId];
                            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
                            {
                                runtime = ComputeRuntimeMasks(cur.layerBits, cur.ignoreLayers);
                            }
                            uint32_t sanitized = SanitizeLayerBits(cur.layerBits, "CCT", entityId);
                            ctrl->SetLayerMasks(sanitized, runtime.collideMask, runtime.queryMask);
                        }
                        m_lastCCTs[entityId] = cur;
                    }
                }
            }
        }

        std::vector<EntityId> cctsToRemove;
        for (const auto& [entityId, state] : m_lastCCTs)
        {
            auto* ccc = m_world.GetComponent<Phy_CCTComponent>(entityId);
            if (!ccc)
            {
                cctsToRemove.push_back(entityId);
            }
        }
        for (auto eid : cctsToRemove)
        {
            DestroyCharacterController(eid);
            m_lastCCTs.erase(eid);
        }
    }

    // 6. CCT 이동 및 Transform 갱신
    {        
        auto ccts = m_world.GetComponents<Phy_CCTComponent>();

        for (const auto& [entityId, ccc] : ccts)
        {
            auto* transform = m_world.GetComponent<TransformComponent>(entityId);
            if (!transform) continue;

            auto it = m_entityToCCT.find(entityId);
            if (it == m_entityToCCT.end() || !it->second.IsValid())
            {
                if (m_warnedMissingCCT.find(entityId) == m_warnedMissingCCT.end())
                {
                    ALICE_LOG_WARN("[PhysicsSystem] CCT not found for entity %llu (controllerHandle: %p). Check if CreateCharacterController succeeded.",
                        (unsigned long long)entityId, ccc.controllerHandle);
                    
                    auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
                    auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
                    auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
                    if (rb || collider || meshCollider)
                    {
                        ALICE_LOG_WARN("[PhysicsSystem] Entity %llu has RigidBody or Collider, which conflicts with CCT!", 
                            (unsigned long long)entityId);
                    }
                    
                    m_warnedMissingCCT.insert(entityId);
                }
                continue;
            }

            ICharacterController* ctrl = it->second.cct;
            if (!ctrl) continue;

            // teleport: 발 위치를 Transform.position으로 강제
            if (ccc.teleport)
            {
                ctrl->SetFootPosition(ToVec3(transform->position));
                ccc.verticalVelocity = 0.0f;
                ccc.teleport = false;
            }

            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeCCTMasks[entityId];
            CharacterControllerState st0 = ctrl->GetState(runtime.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
            const bool wasGrounded = st0.onGround;

            if (ccc.jumpRequested && wasGrounded)
                ccc.verticalVelocity = ccc.jumpSpeed;
            ccc.jumpRequested = false;

            if (ccc.applyGravity)
            {
                if (wasGrounded && ccc.verticalVelocity < 0.0f)
                    ccc.verticalVelocity = 0.0f;
                else
                    ccc.verticalVelocity += ccc.gravity * deltaTime;
            }

            Vec3 disp;
            disp.x = ccc.desiredVelocity.x * deltaTime;
            disp.z = ccc.desiredVelocity.z * deltaTime;
			disp.y = ccc.verticalVelocity * deltaTime;

            CCTCollisionFlags cf = ctrl->Move(
                disp,
                deltaTime,
                runtime.collideMask,
				ccc.layerBits,
                ccc.hitTriggers);

            CharacterControllerState st = ctrl->GetState(runtime.collideMask, ccc.layerBits, 0.2f, ccc.hitTriggers);
            ccc.onGround = st.onGround;
            ccc.groundNormal = ToXMFLOAT3(st.groundNormal);
            ccc.groundDistance = st.groundDistance;
            ccc.collisionFlags = static_cast<uint8_t>(cf);

            transform->position = ToXMFLOAT3(ctrl->GetFootPosition());
            m_world.MarkTransformDirty(entityId);
        }
    }
}

void PhysicsSystem::CreatePhysicsActor(EntityId entityId)
{
    if (!m_physicsWorld) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform || !transform->enabled) return;

    auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
    auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
    auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
    auto* terrain = m_world.GetComponent<Phy_TerrainHeightFieldComponent>(entityId);

    // 런타임 마스크는 멤버 함수 ComputeRuntimeMasks 사용

    if (!rb && !collider && !meshCollider) return;
    if (terrain)
    {
        if (rb || collider || meshCollider)
        {
            ALICE_LOG_WARN("[PhysicsSystem] Entity %llu has TerrainHeightField with other colliders; Terrain takes priority.",
                (unsigned long long)entityId);
        }
        return;
    }

    if (meshCollider && collider)
    {
        ALICE_LOG_WARN("[PhysicsSystem] Entity %llu has both MeshCollider and Collider. MeshCollider will be used.",
            (unsigned long long)entityId);
    }

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    if (rb)
    {
        RigidBodyDesc rbDesc{};
        rbDesc.density = rb->density;
        rbDesc.massOverride = rb->massOverride;
        rbDesc.isKinematic = rb->isKinematic;
        rbDesc.gravityEnabled = rb->gravityEnabled;
        rbDesc.startAwake = rb->startAwake;
        rbDesc.enableCCD = rb->enableCCD;
        rbDesc.enableSpeculativeCCD = rb->enableSpeculativeCCD;
        rbDesc.lockFlags = rb->lockFlags;
        rbDesc.linearDamping = rb->linearDamping;
        rbDesc.angularDamping = rb->angularDamping;
        rbDesc.maxLinearVelocity = rb->maxLinearVelocity;
        rbDesc.maxAngularVelocity = rb->maxAngularVelocity;
        rbDesc.solverPositionIterations = rb->solverPositionIterations;
		rbDesc.solverVelocityIterations = rb->solverVelocityIterations;
		rbDesc.sleepThreshold = rb->sleepThreshold;
		rbDesc.stabilizationThreshold = rb->stabilizationThreshold;
		rbDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        if (meshCollider)
        {
            if (!m_physicsWorld->SupportsMeshCooking())
            {
                ALICE_LOG_WARN("[PhysicsSystem] Mesh cooking not supported. MeshCollider skipped (entity: %llu).",
                    (unsigned long long)entityId);
                return;
            }

            std::vector<Vec3> vertices;
            std::vector<uint32_t> indices;
            const std::string meshPath = ResolveMeshAssetPath(m_world, entityId, *meshCollider);
            if (!BuildMeshBuffers(m_skinnedRegistry, meshPath, vertices, indices))
            {
                ALICE_LOG_WARN("[PhysicsSystem] MeshCollider: invalid mesh asset (entity: %llu, path: %s).",
                    (unsigned long long)entityId, meshPath.c_str());
                return;
            }

            // Triangle mesh는 RigidBody와 함께 사용할 수 없음 (PhysX 제약)
            // RigidBody가 있으면 Convex mesh로 강제 전환
            if (meshCollider->type == MeshColliderType::Triangle && rb && !rb->isKinematic)
            {
                ALICE_LOG_WARN("[PhysicsSystem] Triangle mesh cannot be used with dynamic (non-kinematic) RigidBody. Forcing Convex conversion (entity: %llu).",
                    (unsigned long long)entityId);
                // return 하지 말고 convex로 진행
            }

            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));

            ConvexMeshColliderDesc convexDesc{};
            convexDesc.vertices = vertices.data();
            convexDesc.vertexCount = static_cast<uint32_t>(vertices.size());
            convexDesc.scale = scale;
            convexDesc.shiftVertices = meshCollider->shiftVertices;
            convexDesc.vertexLimit = std::min(meshCollider->vertexLimit, 255u); // PhysX 제한: 최대 255
            convexDesc.validate = meshCollider->validate;

            convexDesc.staticFriction = meshCollider->staticFriction;
            convexDesc.dynamicFriction = meshCollider->dynamicFriction;
            convexDesc.restitution = meshCollider->restitution;
            convexDesc.layerBits = SanitizeLayerBits(meshCollider->layerBits, "MeshCollider", entityId);
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(meshCollider->layerBits, meshCollider->ignoreLayers);
            }
            convexDesc.collideMask = runtime.collideMask;
            convexDesc.queryMask = runtime.queryMask;
            convexDesc.isTrigger = meshCollider->isTrigger;
            convexDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            if (convexDesc.vertexCount > 255)
            {
                ALICE_LOG_WARN("[PhysicsSystem] Convex mesh has too many vertices (%u). Consider simplified mesh.",
                    convexDesc.vertexCount);
            }

            auto bodyPtr = m_physicsWorld->CreateDynamicConvexMesh(pos, rot, rbDesc, convexDesc);
            if (bodyPtr)
            {
                ActorHandle handle(std::move(bodyPtr));
                IRigidBody* body = handle.GetRigidBody();

                rb->physicsActorHandle = body;
                meshCollider->physicsActorHandle = body;
                m_entityToActor[entityId] = std::move(handle);

                // Convex mesh can yield extremely small mass; clamp to 1.0f unless user overrides.
                if (body && body->IsValid() && rb->massOverride <= 0.0f)
                {
                    const float curMass = body->GetMass();
                    if (curMass > 0.0f && curMass < 1.0f)
                        body->SetMass(1.0f, true);
                }
            }
            return;
        }
        else if (collider)
        {
            switch (collider->type)
            {
            case ColliderType::Box:
            {
                BoxColliderDesc boxDesc{};
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                Vec3 he = ToVec3(collider->halfExtents);
                he.x *= scale.x;
                he.y *= scale.y;
                he.z *= scale.z;
                boxDesc.halfExtents = he;
                boxDesc.staticFriction = collider->staticFriction;
                boxDesc.dynamicFriction = collider->dynamicFriction;
                boxDesc.restitution = collider->restitution;
                boxDesc.layerBits = SanitizeLayerBits(collider->layerBits, "Collider", entityId);
                // 런타임 마스크 사용
                RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
                if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
                {
                    // 아직 계산되지 않았으면 계산
                    runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
                }
                boxDesc.collideMask = runtime.collideMask;
                boxDesc.queryMask = runtime.queryMask;
				boxDesc.isTrigger = collider->isTrigger;
				boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

                Vec3 localOffset = ToVec3(collider->offset);
                localOffset.x *= scale.x;
                localOffset.y *= scale.y;
                localOffset.z *= scale.z;
                const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                       (std::abs(localOffset.y) > kFloatEpsilon) ||
                                       (std::abs(localOffset.z) > kFloatEpsilon);

                if (hasOffset)
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();

                        if (!body->AddBoxShape(boxDesc, localOffset, Quat::Identity))
                        {
                            handle.Destroy();
                            break;
                        }

                        body->RecomputeMass();
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                else
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicBox(pos, rot, rbDesc, boxDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();
                        
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                break;
            }
            case ColliderType::Sphere:
            {
                SphereColliderDesc sphereDesc{};
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                float sMax = std::max({ scale.x, scale.y, scale.z });
                sphereDesc.radius = collider->radius * sMax;
                sphereDesc.staticFriction = collider->staticFriction;
                sphereDesc.dynamicFriction = collider->dynamicFriction;
                sphereDesc.restitution = collider->restitution;
                sphereDesc.layerBits = SanitizeLayerBits(collider->layerBits, "Collider", entityId);
                // 런타임 마스크 사용
                RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
                if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
                {
                    // 아직 계산되지 않았으면 계산
                    runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
                }
                sphereDesc.collideMask = runtime.collideMask;
				sphereDesc.queryMask = runtime.queryMask;
				sphereDesc.isTrigger = collider->isTrigger;
				sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

                Vec3 localOffset = ToVec3(collider->offset);
                localOffset.x *= scale.x;
                localOffset.y *= scale.y;
                localOffset.z *= scale.z;
                const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                       (std::abs(localOffset.y) > kFloatEpsilon) ||
                                       (std::abs(localOffset.z) > kFloatEpsilon);

                if (hasOffset)
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();

                        if (!body->AddSphereShape(sphereDesc, localOffset, Quat::Identity))
                        {
                            handle.Destroy();
                            break;
                        }

                        body->RecomputeMass();
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                else
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicSphere(pos, rot, rbDesc, sphereDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();
                        
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                break;
            }
            case ColliderType::Capsule:
            {
                CapsuleColliderDesc capsuleDesc{};
                Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                if (collider->capsuleAlignYAxis)
                {
                    float radial = std::max(scale.x, scale.z);
                    capsuleDesc.radius = collider->capsuleRadius * radial;
                    capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
                }
                else
                {
                    float radial = std::max(scale.y, scale.z);
                    capsuleDesc.radius = collider->capsuleRadius * radial;
                    capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
                }
                capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
                capsuleDesc.staticFriction = collider->staticFriction;
                capsuleDesc.dynamicFriction = collider->dynamicFriction;
                capsuleDesc.restitution = collider->restitution;
                capsuleDesc.layerBits = SanitizeLayerBits(collider->layerBits, "Collider", entityId);
                // 런타임 마스크 사용
                RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
                if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
                {
                    // 아직 계산되지 않았으면 계산
                    runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
                }
                capsuleDesc.collideMask = runtime.collideMask;
				capsuleDesc.queryMask = runtime.queryMask;
				capsuleDesc.isTrigger = collider->isTrigger;
				capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

                Vec3 localOffset = ToVec3(collider->offset);
                localOffset.x *= scale.x;
                localOffset.y *= scale.y;
                localOffset.z *= scale.z;
                const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                       (std::abs(localOffset.y) > kFloatEpsilon) ||
                                       (std::abs(localOffset.z) > kFloatEpsilon);

                if (hasOffset)
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();

                        if (!body->AddCapsuleShape(capsuleDesc, localOffset, Quat::Identity))
                        {
                            handle.Destroy();
                            break;
                        }

                        body->RecomputeMass();
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                else
                {
                    auto bodyPtr = m_physicsWorld->CreateDynamicCapsule(pos, rot, rbDesc, capsuleDesc);
                    if (bodyPtr)
                    {
                        ActorHandle handle(std::move(bodyPtr));
                        IRigidBody* body = handle.GetRigidBody();
                        
                        rb->physicsActorHandle = body;
                        collider->physicsActorHandle = body;
                        m_entityToActor[entityId] = std::move(handle);
                    }
                }
                break;
            }
            }
        }
        else
        {
            auto bodyPtr = m_physicsWorld->CreateDynamicEmpty(pos, rot, rbDesc);
            if (bodyPtr)
            {
                ActorHandle handle(std::move(bodyPtr));
                IRigidBody* body = handle.GetRigidBody();
                
                rb->physicsActorHandle = body;
                m_entityToActor[entityId] = std::move(handle);
            }
        }
    }
    else if (meshCollider)
    {
        if (!m_physicsWorld->SupportsMeshCooking())
        {
            ALICE_LOG_WARN("[PhysicsSystem] Mesh cooking not supported. MeshCollider skipped (entity: %llu).",
                (unsigned long long)entityId);
            return;
        }

        std::vector<Vec3> vertices;
        std::vector<uint32_t> indices;
        const std::string meshPath = ResolveMeshAssetPath(m_world, entityId, *meshCollider);
        if (!BuildMeshBuffers(m_skinnedRegistry, meshPath, vertices, indices))
        {
            ALICE_LOG_WARN("[PhysicsSystem] MeshCollider: invalid mesh asset (entity: %llu, path: %s).",
                (unsigned long long)entityId, meshPath.c_str());
            return;
        }

        Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));

        if (meshCollider->type == MeshColliderType::Triangle)
        {
            TriangleMeshColliderDesc triDesc{};
            triDesc.vertices = vertices.data();
            triDesc.vertexCount = static_cast<uint32_t>(vertices.size());
            triDesc.indices32 = indices.data();
            triDesc.indexCount = static_cast<uint32_t>(indices.size());
            triDesc.scale = scale;
            triDesc.flipNormals = meshCollider->flipNormals;
            triDesc.doubleSidedQueries = meshCollider->doubleSidedQueries;
            triDesc.validate = meshCollider->validate;

            triDesc.staticFriction = meshCollider->staticFriction;
            triDesc.dynamicFriction = meshCollider->dynamicFriction;
            triDesc.restitution = meshCollider->restitution;
            triDesc.layerBits = SanitizeLayerBits(meshCollider->layerBits, "MeshCollider", entityId);
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(meshCollider->layerBits, meshCollider->ignoreLayers);
            }
            triDesc.collideMask = runtime.collideMask;
            triDesc.queryMask = runtime.queryMask;
            // TriangleMesh는 트리거 지원 안 함: 강제 해제
            if (meshCollider->isTrigger)
            {
                ALICE_LOG_WARN("[PhysicsSystem] TriangleMesh cannot be used as trigger (entity: %llu). Forcing isTrigger = false. "
                    "Use ConvexMesh if trigger is required.",
                    (unsigned long long)entityId);
            }
            triDesc.isTrigger = false;
            triDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            auto actorPtr = m_physicsWorld->CreateStaticTriangleMesh(pos, rot, triDesc);
            if (actorPtr)
            {
                ActorHandle handle(std::move(actorPtr));
                IPhysicsActor* actor = handle.GetActor();

                meshCollider->physicsActorHandle = actor;
                m_entityToActor[entityId] = std::move(handle);
            }
        }
        else
        {
            ConvexMeshColliderDesc convexDesc{};
            convexDesc.vertices = vertices.data();
            convexDesc.vertexCount = static_cast<uint32_t>(vertices.size());
            convexDesc.scale = scale;
            convexDesc.shiftVertices = meshCollider->shiftVertices;
            convexDesc.vertexLimit = std::min(meshCollider->vertexLimit, 255u); // PhysX 제한: 최대 255
            convexDesc.validate = meshCollider->validate;

            convexDesc.staticFriction = meshCollider->staticFriction;
            convexDesc.dynamicFriction = meshCollider->dynamicFriction;
            convexDesc.restitution = meshCollider->restitution;
            convexDesc.layerBits = meshCollider->layerBits;
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(meshCollider->layerBits, meshCollider->ignoreLayers);
            }
            convexDesc.collideMask = runtime.collideMask;
            convexDesc.queryMask = runtime.queryMask;
            convexDesc.isTrigger = meshCollider->isTrigger;
            convexDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            if (convexDesc.vertexCount > 255)
            {
                ALICE_LOG_WARN("[PhysicsSystem] Convex mesh has too many vertices (%u). Consider simplified mesh.",
                    convexDesc.vertexCount);
            }

            auto actorPtr = m_physicsWorld->CreateStaticConvexMesh(pos, rot, convexDesc);
            if (actorPtr)
            {
                ActorHandle handle(std::move(actorPtr));
                IPhysicsActor* actor = handle.GetActor();

                meshCollider->physicsActorHandle = actor;
                m_entityToActor[entityId] = std::move(handle);
            }
        }
    }
    else if (collider)
    {
        FilterDesc filterDesc{};
        filterDesc.layerBits = SanitizeLayerBits(collider->layerBits, "Collider", entityId);
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
        }
        filterDesc.collideMask = runtime.collideMask;
        filterDesc.queryMask = runtime.queryMask;
        filterDesc.isTrigger = collider->isTrigger;
        filterDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        MaterialDesc materialDesc{};
        materialDesc.staticFriction = collider->staticFriction;
        materialDesc.dynamicFriction = collider->dynamicFriction;
        materialDesc.restitution = collider->restitution;

        switch (collider->type)
        {
        case ColliderType::Box:
        {
            BoxColliderDesc boxDesc{};
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            Vec3 he = ToVec3(collider->halfExtents);
            he.x *= scale.x;
            he.y *= scale.y;
            he.z *= scale.z;
            boxDesc.halfExtents = he;
            boxDesc.staticFriction = collider->staticFriction;
            boxDesc.dynamicFriction = collider->dynamicFriction;
            boxDesc.restitution = collider->restitution;
            boxDesc.layerBits = collider->layerBits;
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
            }
            boxDesc.collideMask = runtime.collideMask;
            boxDesc.queryMask = runtime.queryMask;
            boxDesc.isTrigger = collider->isTrigger;
            boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            Vec3 localOffset = ToVec3(collider->offset);
            localOffset.x *= scale.x;
            localOffset.y *= scale.y;
            localOffset.z *= scale.z;
            const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                   (std::abs(localOffset.y) > kFloatEpsilon) ||
                                   (std::abs(localOffset.z) > kFloatEpsilon);

            if (hasOffset)
            {
                auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, boxDesc.userData);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();

                    if (!actor->AddBoxShape(boxDesc, localOffset, Quat::Identity))
                    {
                        handle.Destroy();
                        break;
                    }

                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            else
            {
                auto actorPtr = m_physicsWorld->CreateStaticBox(pos, rot, boxDesc);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();
                    
                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            break;
        }
        case ColliderType::Sphere:
        {
            SphereColliderDesc sphereDesc{};
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            float sMax = std::max({ scale.x, scale.y, scale.z });
            sphereDesc.radius = collider->radius * sMax;
            sphereDesc.staticFriction = collider->staticFriction;
            sphereDesc.dynamicFriction = collider->dynamicFriction;
            sphereDesc.restitution = collider->restitution;
            sphereDesc.layerBits = SanitizeLayerBits(collider->layerBits, "Collider", entityId);
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
            }
            sphereDesc.collideMask = runtime.collideMask;
            sphereDesc.queryMask = runtime.queryMask;
			sphereDesc.isTrigger = collider->isTrigger;
			sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            Vec3 localOffset = ToVec3(collider->offset);
            localOffset.x *= scale.x;
            localOffset.y *= scale.y;
            localOffset.z *= scale.z;
            const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                   (std::abs(localOffset.y) > kFloatEpsilon) ||
                                   (std::abs(localOffset.z) > kFloatEpsilon);

            if (hasOffset)
            {
                auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, sphereDesc.userData);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();

                    if (!actor->AddSphereShape(sphereDesc, localOffset, Quat::Identity))
                    {
                        handle.Destroy();
                        break;
                    }

                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            else
            {
			    auto actorPtr = m_physicsWorld->CreateStaticSphere(pos, rot, sphereDesc);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();
                    
                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            break;
        }
        case ColliderType::Capsule:
        {
            CapsuleColliderDesc capsuleDesc{};
            Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
            if (collider->capsuleAlignYAxis)
            {
                float radial = std::max(scale.x, scale.z);
                capsuleDesc.radius = collider->capsuleRadius * radial;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
            }
            else
            {
                float radial = std::max(scale.y, scale.z);
                capsuleDesc.radius = collider->capsuleRadius * radial;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
            }
            capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
            capsuleDesc.staticFriction = collider->staticFriction;
            capsuleDesc.dynamicFriction = collider->dynamicFriction;
            capsuleDesc.restitution = collider->restitution;
            capsuleDesc.layerBits = collider->layerBits;
            // 런타임 마스크 사용
            RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
            if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
            {
                runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
            }
            capsuleDesc.collideMask = runtime.collideMask;
            capsuleDesc.queryMask = runtime.queryMask;
			capsuleDesc.isTrigger = collider->isTrigger;
			capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

            Vec3 localOffset = ToVec3(collider->offset);
            localOffset.x *= scale.x;
            localOffset.y *= scale.y;
            localOffset.z *= scale.z;
            const bool hasOffset = (std::abs(localOffset.x) > kFloatEpsilon) ||
                                   (std::abs(localOffset.y) > kFloatEpsilon) ||
                                   (std::abs(localOffset.z) > kFloatEpsilon);

            if (hasOffset)
            {
                auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, capsuleDesc.userData);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();

                    if (!actor->AddCapsuleShape(capsuleDesc, localOffset, Quat::Identity))
                    {
                        handle.Destroy();
                        break;
                    }

                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            else
            {
			    auto actorPtr = m_physicsWorld->CreateStaticCapsule(pos, rot, capsuleDesc);
                if (actorPtr)
                {
                    ActorHandle handle(std::move(actorPtr));
                    IPhysicsActor* actor = handle.GetActor();
                    
                    collider->physicsActorHandle = actor;
                    m_entityToActor[entityId] = std::move(handle);
                }
            }
            break;
        }
        }

    }
}

void PhysicsSystem::DestroyPhysicsActor(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

	if (!it->second.owned)
	{
		m_entityToActor.erase(it);
		return;
	}
    auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
    if (rb) rb->physicsActorHandle = nullptr;

    auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
    if (collider) collider->physicsActorHandle = nullptr;

    auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
    if (meshCollider) meshCollider->physicsActorHandle = nullptr;

    auto* terrain = m_world.GetComponent<Phy_TerrainHeightFieldComponent>(entityId);
    if (terrain) terrain->physicsActorHandle = nullptr;

	ActorHandle handle = std::move(it->second);
	
	if (handle.owned)
	{
		handle.Destroy();
	}
	
    m_entityToActor.erase(it);
    m_lastTransforms.erase(entityId);
    m_lastColliders.erase(entityId);
    m_lastMeshColliders.erase(entityId);
    m_lastRigidBodies.erase(entityId);
    
    // 런타임 마스크 캐시 정리
    m_runtimeColliderMasks.erase(entityId);
    m_runtimeMeshColliderMasks.erase(entityId);
    m_runtimeTerrainMasks.erase(entityId);
}

void PhysicsSystem::DestroyJoint(EntityId entityId)
{
    auto it = m_entityToJoint.find(entityId);
    if (it != m_entityToJoint.end())
    {
        it->second.reset();
        m_entityToJoint.erase(it);
    }

    m_lastJoints.erase(entityId);

    if (auto* joint = m_world.GetComponent<Phy_JointComponent>(entityId))
        joint->jointHandle = nullptr;
}

void PhysicsSystem::CreateTerrainHeightField(EntityId entityId)
{
    if (!m_physicsWorld)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: m_physicsWorld is null!");
        return;
    }

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    if (!transform)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Transform component missing!");
        return;
    }

    auto* terrain = m_world.GetComponent<Phy_TerrainHeightFieldComponent>(entityId);
    if (!terrain)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Phy_TerrainHeightFieldComponent missing!");
        return;
    }

	const size_t expectedSamples = static_cast<size_t>(terrain->numRows) * static_cast<size_t>(terrain->numCols);
    if (terrain->heightSamples.empty() || terrain->numRows < 2 || terrain->numCols < 2)
    {
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain data (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
        return;
    }

	if (terrain->heightSamples.size() != expectedSamples)
	{
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: HeightSamples size mismatch (entity: %llu, rows: %u, cols: %u, samples: %zu, expected: %zu)!",
			(unsigned long long)entityId, terrain->numRows, terrain->numCols, terrain->heightSamples.size(), expectedSamples);
		return;
	}

    if (terrain->heightScale <= 0.0f || terrain->rowScale <= 0.0f || terrain->colScale <= 0.0f)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: Invalid terrain scale (entity: %llu, heightScale: %.2f, rowScale: %.2f, colScale: %.2f)!",
            (unsigned long long)entityId, terrain->heightScale, terrain->rowScale, terrain->colScale);
        return;
    }

	auto itActor = m_entityToActor.find(entityId);
	if (itActor != m_entityToActor.end())
	{
		ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField called but actor already exists (entity=%llu). Skipping.",
			(unsigned long long)entityId);
		return;
	}

    if (terrain->physicsActorHandle != nullptr)
	{
		ALICE_LOG_WARN("[PhysicsSystem] Terrain component has stale physicsActorHandle. Forcing null (entity=%llu)",
			(unsigned long long)entityId);
		terrain->physicsActorHandle = nullptr;
    }

    Vec3 pos = ToVec3(transform->position);
    Quat rot = ToQuat(transform->rotation);

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);

    HeightFieldColliderDesc hfDesc{};
    hfDesc.heightSamples = terrain->heightSamples.data();
    hfDesc.numRows = terrain->numRows;
    hfDesc.numCols = terrain->numCols;

	hfDesc.colScale = terrain->colScale * s.x;
	hfDesc.rowScale = terrain->rowScale * s.z;
    hfDesc.heightScale = terrain->heightScale * s.y;
    hfDesc.staticFriction = terrain->staticFriction;
    hfDesc.dynamicFriction = terrain->dynamicFriction;
    hfDesc.restitution = terrain->restitution;
    hfDesc.layerBits = SanitizeLayerBits(terrain->layerBits, "Terrain", entityId);
    // 런타임 마스크 사용
    RuntimeMasks& runtime = m_runtimeTerrainMasks[entityId];
    if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
    {
        runtime = ComputeRuntimeMasks(terrain->layerBits, terrain->ignoreLayers);
    }
    hfDesc.collideMask = runtime.collideMask;
    hfDesc.queryMask = runtime.queryMask;
    hfDesc.isTrigger = false;
    hfDesc.doubleSidedQueries = terrain->doubleSidedQueries; 
    hfDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

    Vec3 localPos = Vec3::Zero;
    Quat localRot = Quat::Identity;

    if (terrain->centerPivot)
    {
        const float halfW = 0.5f * static_cast<float>(terrain->numCols - 1) * hfDesc.colScale;
        const float halfD = 0.5f * static_cast<float>(terrain->numRows - 1) * hfDesc.rowScale;
        localPos = Vec3(-halfW, 0.0f, -halfD);
    }

    if (!m_physicsWorld)
    {
        ALICE_LOG_WARN("[PhysicsSystem] CreateTerrainHeightField: m_physicsWorld became null during creation!");
        return;
    }
    
    auto actorPtr = m_physicsWorld->CreateStaticEmpty(pos, rot, hfDesc.userData);
    if (actorPtr)
    {
        ActorHandle handle(std::move(actorPtr));
        IPhysicsActor* actor = handle.GetActor();
        
        if (!actor->AddHeightFieldShape(hfDesc, localPos, localRot))
        {
            handle.Destroy();
            return;
        }
        
        terrain->physicsActorHandle = actor;
        m_entityToActor[entityId] = std::move(handle);
    }
    else
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateTerrainHeightField: Failed to create terrain actor!");
    }
}

void PhysicsSystem::RebuildShapes(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    ActorHandle& handle = it->second;
    if (!handle.IsValid()) return;

    IPhysicsActor* actor = handle.GetActor();
    if (!actor || !actor->IsValid()) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
    if (!transform || !collider) return;

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };

    Vec3 scale = AbsScale(transform->scale);
    Vec3 localOffset = ToVec3(collider->offset);
    localOffset.x *= scale.x;
    localOffset.y *= scale.y;
    localOffset.z *= scale.z;
    actor->ClearShapes();

    switch (collider->type)
    {
    case ColliderType::Box:
    {
        BoxColliderDesc boxDesc{};
        Vec3 he = ToVec3(collider->halfExtents);
        he.x *= scale.x;
        he.y *= scale.y;
        he.z *= scale.z;
        boxDesc.halfExtents = he;
        boxDesc.staticFriction = collider->staticFriction;
        boxDesc.dynamicFriction = collider->dynamicFriction;
        boxDesc.restitution = collider->restitution;
        boxDesc.layerBits = collider->layerBits;
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
        }
        boxDesc.collideMask = runtime.collideMask;
        boxDesc.queryMask = runtime.queryMask;
        boxDesc.isTrigger = collider->isTrigger;
        boxDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        actor->AddBoxShape(boxDesc, localOffset, Quat::Identity);
        break;
    }
    case ColliderType::Sphere:
    {
        SphereColliderDesc sphereDesc{};
        float sMax = std::max({ scale.x, scale.y, scale.z });
        sphereDesc.radius = collider->radius * sMax;
        sphereDesc.staticFriction = collider->staticFriction;
        sphereDesc.dynamicFriction = collider->dynamicFriction;
        sphereDesc.restitution = collider->restitution;
        sphereDesc.layerBits = collider->layerBits;
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
        }
        sphereDesc.collideMask = runtime.collideMask;
        sphereDesc.queryMask = runtime.queryMask;
        sphereDesc.isTrigger = collider->isTrigger;
        sphereDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        actor->AddSphereShape(sphereDesc, localOffset, Quat::Identity);
        break;
    }
    case ColliderType::Capsule:
    {
        CapsuleColliderDesc capsuleDesc{};
            if (collider->capsuleAlignYAxis)
            {
                float radial = std::max(scale.x, scale.z);
                capsuleDesc.radius = collider->capsuleRadius * radial;
                capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.y;
            }
            else
            {
                float radial = std::max(scale.y, scale.z);
            capsuleDesc.radius = collider->capsuleRadius * radial;
            capsuleDesc.halfHeight = collider->capsuleHalfHeight * scale.x;
        }
        capsuleDesc.alignYAxis = collider->capsuleAlignYAxis;
        capsuleDesc.staticFriction = collider->staticFriction;
        capsuleDesc.dynamicFriction = collider->dynamicFriction;
        capsuleDesc.restitution = collider->restitution;
        capsuleDesc.layerBits = collider->layerBits;
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(collider->layerBits, collider->ignoreLayers);
        }
        capsuleDesc.collideMask = runtime.collideMask;
        capsuleDesc.queryMask = runtime.queryMask;
        capsuleDesc.isTrigger = collider->isTrigger;
        capsuleDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        actor->AddCapsuleShape(capsuleDesc, localOffset, Quat::Identity);
        break;
    }
    }

    IRigidBody* body = handle.GetRigidBody();
    if (body && body->IsValid())
    {
        body->RecomputeMass();
        auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
        if (rb && rb->massOverride <= 0.0f)
        {
            const float curMass = body->GetMass();
            if (curMass > 0.0f && curMass < 1.0f)
                body->SetMass(1.0f, true);
        }
    }
}

void PhysicsSystem::RebuildMeshShapes(EntityId entityId)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    ActorHandle& handle = it->second;
    if (!handle.IsValid()) return;

    IPhysicsActor* actor = handle.GetActor();
    if (!actor || !actor->IsValid()) return;

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
    if (!transform || !meshCollider) return;

    if (!m_physicsWorld->SupportsMeshCooking())
    {
        ALICE_LOG_WARN("[PhysicsSystem] Mesh cooking not supported. RebuildMeshShapes skipped (entity: %llu).",
            (unsigned long long)entityId);
        return;
    }

    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices;
    const std::string meshPath = ResolveMeshAssetPath(m_world, entityId, *meshCollider);
    if (!BuildMeshBuffers(m_skinnedRegistry, meshPath, vertices, indices))
    {
        ALICE_LOG_WARN("[PhysicsSystem] MeshCollider: invalid mesh asset (entity: %llu, path: %s).",
            (unsigned long long)entityId, meshPath.c_str());
        return;
    }

    // Triangle mesh는 RigidBody와 함께 사용할 수 없음 (PhysX 제약, kinematic 포함)
    // RigidBody가 있으면 Convex mesh로 강제 전환 (아래 forceConvex에서 처리)

    Vec3 scale = Vec3(std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
    actor->ClearShapes();

    // RigidBody가 있으면 Triangle을 Convex로 강제 전환 (kinematic 포함)
    // PhysX에서 TriangleMesh는 PxRigidDynamic(kinematic 포함)에 붙일 수 없음
    bool forceConvex = (meshCollider->type == MeshColliderType::Triangle && handle.GetRigidBody() != nullptr);

    if (meshCollider->type == MeshColliderType::Triangle && !forceConvex)
    {
        TriangleMeshColliderDesc triDesc{};
        triDesc.vertices = vertices.data();
        triDesc.vertexCount = static_cast<uint32_t>(vertices.size());
        triDesc.indices32 = indices.data();
        triDesc.indexCount = static_cast<uint32_t>(indices.size());
        triDesc.scale = scale;
        triDesc.flipNormals = meshCollider->flipNormals;
        triDesc.doubleSidedQueries = meshCollider->doubleSidedQueries;
        triDesc.validate = meshCollider->validate;

        triDesc.staticFriction = meshCollider->staticFriction;
        triDesc.dynamicFriction = meshCollider->dynamicFriction;
        triDesc.restitution = meshCollider->restitution;
        triDesc.layerBits = SanitizeLayerBits(meshCollider->layerBits, "MeshCollider", entityId);
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(meshCollider->layerBits, meshCollider->ignoreLayers);
        }
        triDesc.collideMask = runtime.collideMask;
        triDesc.queryMask = runtime.queryMask;
        // TriangleMesh는 트리거 지원 안 함: 강제 해제
        if (meshCollider->isTrigger)
        {
            ALICE_LOG_WARN("[PhysicsSystem] TriangleMesh cannot be used as trigger (entity: %llu). Forcing isTrigger = false. "
                "Use ConvexMesh if trigger is required.",
                (unsigned long long)entityId);
        }
        triDesc.isTrigger = false;
        triDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        if (!actor->AddTriangleMeshShape(triDesc, Vec3::Zero, Quat::Identity))
        {
            ALICE_LOG_WARN("[PhysicsSystem] AddTriangleMeshShape failed (entity: %llu).",
                (unsigned long long)entityId);
        }
    }
    else // Convex 또는 Triangle+RigidBody 강제 전환
    {
        ConvexMeshColliderDesc convexDesc{};
        convexDesc.vertices = vertices.data();
        convexDesc.vertexCount = static_cast<uint32_t>(vertices.size());
        convexDesc.scale = scale;
        convexDesc.shiftVertices = meshCollider->shiftVertices;
        convexDesc.vertexLimit = std::min(meshCollider->vertexLimit, 255u); // PhysX 제한: 최대 255
        convexDesc.validate = meshCollider->validate;

        convexDesc.staticFriction = meshCollider->staticFriction;
        convexDesc.dynamicFriction = meshCollider->dynamicFriction;
        convexDesc.restitution = meshCollider->restitution;
        convexDesc.layerBits = SanitizeLayerBits(meshCollider->layerBits, "MeshCollider", entityId);
        // 런타임 마스크 사용
        RuntimeMasks& runtime = m_runtimeMeshColliderMasks[entityId];
        if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
        {
            runtime = ComputeRuntimeMasks(meshCollider->layerBits, meshCollider->ignoreLayers);
        }
        convexDesc.collideMask = runtime.collideMask;
        convexDesc.queryMask = runtime.queryMask;
        convexDesc.isTrigger = meshCollider->isTrigger;
        convexDesc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);

        if (convexDesc.vertexCount > 255)
        {
            ALICE_LOG_WARN("[PhysicsSystem] Convex mesh has too many vertices (%u). Consider simplified mesh.",
                convexDesc.vertexCount);
        }

        if (!actor->AddConvexMeshShape(convexDesc, Vec3::Zero, Quat::Identity))
        {
            ALICE_LOG_WARN("[PhysicsSystem] AddConvexMeshShape failed (entity: %llu).",
                (unsigned long long)entityId);
        }
    }

    IRigidBody* body = handle.GetRigidBody();
    if (body && body->IsValid())
    {
        body->RecomputeMass();
    }
}

void PhysicsSystem::SyncGameToPhysics(EntityId entityId, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation)
{
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end()) return;

    ActorHandle& handle = it->second;
    if (!handle.IsValid()) return;

    IPhysicsActor* actor = handle.GetActor();
    IRigidBody* body = handle.GetRigidBody();

    Vec3 pos = ToVec3(position);
    Quat rot = ToQuat(rotation);

    auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);

    // RigidBody 컴포넌트가 있고, 실제 PxRigidDynamic이 붙어 있을 때만 body 분기
    if (rb && body && body->IsValid())
    {
        const bool physIsKinematic = body->IsKinematic();

        if (rb->teleport)
        {
            actor->SetTransform(pos, rot);

            if (rb->resetVelocityOnTeleport)
            {
                if (!physIsKinematic)
                {
                    body->SetLinearVelocity(Vec3::Zero);
                    body->SetAngularVelocity(Vec3::Zero);
                }
            }

            rb->teleport = false;
            if (physIsKinematic)
                body->SetKinematicTarget(pos, rot);
            return;
        }

        // 키네마틱: 게임 → 물리(target)만 반영. 트랜스폼 역쓰기 없음 (m_lastTransforms는 호출부 루프에서 갱신됨).
        // body가 아직 scene에 없으면 SetKinematicTarget 내부에서 setGlobalPose로 폴백함.
        if (physIsKinematic)
        {
            body->SetKinematicTarget(pos, rot);
            return;
        }

        if (actor && actor->IsValid())
        {
            auto* t = m_world.GetComponent<TransformComponent>(entityId);
            if (t)
            {
                t->position = ToXMFLOAT3(actor->GetPosition());
                t->rotation = ToEulerRadians(actor->GetRotation());
                m_lastTransforms[entityId] = { t->position, t->rotation, t->scale };
                m_world.MarkTransformDirty(entityId);
            }
        }
        return;
    }

    // RigidBody 없음, 또는 body 미생성/무효: static/키네마틱 생성 직후 등. 포즈만 반영.
    if (actor && actor->IsValid())
        actor->SetTransform(pos, rot);
}

void PhysicsSystem::SyncPhysicsToGame(const ActiveTransform& transform)
{
    if (!transform.userData) return;

    EntityId entityId = m_world.ExtractEntityIdFromUserData(transform.userData);
    if (entityId == InvalidEntityId) return;

    if (!IsTrackedEntity(entityId))
        return;

    auto* transformComp = m_world.GetComponent<TransformComponent>(entityId);
    if (!transformComp) return;
    if (!transformComp->enabled) return;

    transformComp->position = ToXMFLOAT3(transform.position);
    DirectX::XMFLOAT3 euler = ToEulerRadians(transform.rotation);
    transformComp->rotation = euler;

    m_lastTransforms[entityId] = {
        transformComp->position,
        transformComp->rotation,
        transformComp->scale
    };
    m_world.MarkTransformDirty(entityId);
}

bool PhysicsSystem::IsTrackedEntity(Alice::EntityId id) const noexcept
{
    return (m_entityToActor.find(id) != m_entityToActor.end()) ||
           (m_entityToCCT.find(id) != m_entityToCCT.end());
}

IPhysicsActor* PhysicsSystem::ValidateAndGetActor(void* handle, Alice::EntityId entityId) const noexcept
{
    if (!handle) return nullptr;
    
    // 1. worldEpoch 검증: IsTrackedEntity로 확인
    if (!IsTrackedEntity(entityId))
    {
        // 이전 씬의 핸들 또는 추적되지 않는 엔티티
        return nullptr;
    }
    
    // 2. m_entityToActor에서 실제 소유권 확인
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end() || !it->second.IsValid())
    {
        return nullptr;
    }
    
    // 3. 핸들이 실제로 m_entityToActor의 것과 일치하는지 확인
    IPhysicsActor* actor = it->second.GetActor();
    IRigidBody* body = it->second.GetRigidBody();
    
    if (handle != actor && handle != body)
    {
        // 핸들이 실제 소유권과 일치하지 않음 (stale handle)
        return nullptr;
    }
    
    // 4. IsValid() 최종 검증
    if (body && handle == body)
    {
        if (!body->IsValid()) return nullptr;
        return body; // IRigidBody는 IPhysicsActor를 상속
    }
    
    if (actor && handle == actor)
    {
        if (!actor->IsValid()) return nullptr;
        return actor;
    }
    
    return nullptr;
}

IRigidBody* PhysicsSystem::ValidateAndGetRigidBody(void* handle, Alice::EntityId entityId) const noexcept
{
    if (!handle) return nullptr;
    
    // 1. worldEpoch 검증
    if (!IsTrackedEntity(entityId))
    {
        return nullptr;
    }
    
    // 2. m_entityToActor에서 확인
    auto it = m_entityToActor.find(entityId);
    if (it == m_entityToActor.end() || !it->second.IsValid())
    {
        return nullptr;
    }
    
    // 3. 핸들 일치 확인
    IRigidBody* body = it->second.GetRigidBody();
    if (!body || handle != body)
    {
        return nullptr;
    }
    
    // 4. IsValid() 검증
    if (!body->IsValid()) return nullptr;
    
    return body;
}

IPhysicsJoint* PhysicsSystem::ValidateAndGetJoint(void* handle, Alice::EntityId entityId) const noexcept
{
    if (!handle) return nullptr;
    
    // 1. worldEpoch 검증
    if (!IsTrackedEntity(entityId))
    {
        return nullptr;
    }
    
    // 2. m_entityToJoint에서 확인
    auto it = m_entityToJoint.find(entityId);
    if (it == m_entityToJoint.end() || !it->second)
    {
        return nullptr;
    }
    
    // 3. 핸들 일치 확인
    IPhysicsJoint* joint = it->second.get();
    if (handle != joint)
    {
        return nullptr;
    }
    
    // 4. IsValid() 검증
    if (!joint->IsValid()) return nullptr;
    
    return joint;
}

ICharacterController* PhysicsSystem::ValidateAndGetController(void* handle, Alice::EntityId entityId) const noexcept
{
    if (!handle) return nullptr;
    
    // 1. worldEpoch 검증
    if (!IsTrackedEntity(entityId))
    {
        return nullptr;
    }
    
    // 2. m_entityToCCT에서 확인
    auto it = m_entityToCCT.find(entityId);
    if (it == m_entityToCCT.end() || !it->second.IsValid())
    {
        return nullptr;
    }
    
    // 3. 핸들 일치 확인
    ICharacterController* cct = it->second.cct;
    if (handle != cct)
    {
        return nullptr;
    }
    
    // 4. IsValid() 검증
    if (!cct->IsValid()) return nullptr;
    
    return cct;
}

Vec3 PhysicsSystem::ToVec3(const DirectX::XMFLOAT3& v)
{
    return Vec3(v.x, v.y, v.z);
}

Quat PhysicsSystem::ToQuat(const DirectX::XMFLOAT3& eulerRadians)
{
	return Quat::CreateFromYawPitchRoll(
		eulerRadians.y,
		eulerRadians.x,
		eulerRadians.z
	);
}

DirectX::XMFLOAT3 PhysicsSystem::ToXMFLOAT3(const Vec3& v)
{
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

DirectX::XMFLOAT3 PhysicsSystem::ToEulerRadians(const Quat& q)
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
    
	float sinp = 2.0f * (w * x - y * z);
	float pitch = (std::abs(sinp) >= 1.0f)
		? std::copysign(DirectX::XM_PIDIV2, sinp)
		: std::asin(sinp);
    
	float siny_cosp = 2.0f * (w * y + x * z);
	float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
    float yaw = std::atan2(siny_cosp, cosy_cosp);
    
	float sinr_cosp = 2.0f * (w * z + x * y);
	float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
	float roll = std::atan2(sinr_cosp, cosr_cosp);

	return DirectX::XMFLOAT3(pitch, yaw, roll);
}

void PhysicsSystem::CreateCharacterController(EntityId entityId)
{
    if (!m_physicsWorld)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: m_physicsWorld is null!");
        return;
    }
    if (!m_physicsWorld->SupportsCharacterControllers())
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: CharacterControllers not supported!");
        return;
    }

    auto* transform = m_world.GetComponent<TransformComponent>(entityId);
    auto* ccc = m_world.GetComponent<Phy_CCTComponent>(entityId);
    if (!transform || !ccc)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Transform or CCT component missing! (transform: %p, ccc: %p)",
            (void*)transform, (void*)ccc);
        return;
    }

    auto* rb = m_world.GetComponent<Phy_RigidBodyComponent>(entityId);
    auto* collider = m_world.GetComponent<Phy_ColliderComponent>(entityId);
    auto* meshCollider = m_world.GetComponent<Phy_MeshColliderComponent>(entityId);
    if (rb || collider || meshCollider)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Entity has RigidBody (%p) or Collider (%p), cannot create CCT!",
            (void*)rb, (void*)collider);
        return;
    }

    auto AbsScale = [](const DirectX::XMFLOAT3& s) -> Vec3 {
        return Vec3(std::abs(s.x), std::abs(s.y), std::abs(s.z));
    };
    const Vec3 s = AbsScale(transform->scale);
	const float radial = std::max(s.x, s.z);

    CharacterControllerDesc desc{};
	desc.type = CCTType::Capsule;

    desc.radius = ccc->radius * radial;
    desc.halfHeight = ccc->halfHeight * s.y;

	desc.stepOffset = ccc->stepOffset * s.y;
	desc.contactOffset = ccc->contactOffset * std::min(radial, s.y);

	float maxStep = 0.0f;
	if (desc.type == CCTType::Capsule)
	{
		maxStep = desc.halfHeight * 2.0f + desc.radius * 2.0f;
	}
	else
	{
		maxStep = desc.halfHeight * 2.0f;
	}

	desc.stepOffset = std::clamp(desc.stepOffset, 0.0f, maxStep);
	desc.contactOffset = std::max(desc.contactOffset, 0.001f);

    desc.slopeLimitRadians = ccc->slopeLimitRadians;
    desc.nonWalkableMode = ccc->nonWalkableMode;
    desc.climbingMode = ccc->climbingMode;
    desc.density = ccc->density;
    desc.enableQueries = ccc->enableQueries;

    desc.layerBits = SanitizeLayerBits(ccc->layerBits, "CCT", entityId);
    // 런타임 마스크 사용
    RuntimeMasks& runtime = m_runtimeCCTMasks[entityId];
    if (runtime.collideMask == 0xFFFFFFFFu && runtime.queryMask == 0xFFFFFFFFu)
    {
        runtime = ComputeRuntimeMasks(ccc->layerBits, ccc->ignoreLayers);
    }
    desc.collideMask = runtime.collideMask;
    desc.queryMask = runtime.queryMask;
    desc.userData = MakeUserData(m_world.GetWorldEpoch(), entityId);
    desc.footPosition = ToVec3(transform->position);
    desc.upDirection = Vec3::UnitY;

    auto ctrl = m_physicsWorld->CreateCharacterController(desc);
    if (!ctrl)
    {
        ALICE_LOG_ERRORF("[PhysicsSystem] CreateCharacterController: Failed to create CCT (CreateCharacterController returned null)!");
        return;
    }

    CCTHandle handle(std::move(ctrl));
    ccc->controllerHandle = handle.cct;
    m_entityToCCT[entityId] = std::move(handle);
}
void PhysicsSystem::DestroyCharacterController(EntityId entityId)
{
    auto it = m_entityToCCT.find(entityId);
    if (it == m_entityToCCT.end()) return;
    it->second.Destroy();
    m_entityToCCT.erase(it);

    auto* ccc = m_world.GetComponent<Phy_CCTComponent>(entityId);
    if (ccc) ccc->controllerHandle = nullptr;
    
    // 런타임 마스크 캐시 정리
    m_runtimeCCTMasks.erase(entityId);
}

// ComputeRuntimeMasks 멤버 함수 구현
PhysicsSystem::RuntimeMasks PhysicsSystem::ComputeRuntimeMasks(uint32_t layerBits, uint32_t ignoreLayers) const
{
    RuntimeMasks masks{};
    const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();
    if (!settingsMap.empty())
    {
        const auto& s = settingsMap.begin()->second;
        // layerBits sanitize (단일 비트만 허용)
        uint32_t sanitized = SanitizeLayerBits(layerBits, "ComputeRuntimeMasks", InvalidEntityId);
        int li = FirstLayerIndex(sanitized);
        if (li >= 0 && li < MAX_PHYSICS_LAYERS)
        {
            // collide: row 기반
            uint32_t collideMask = 0;
            for (int j = 0; j < MAX_PHYSICS_LAYERS; ++j)
                if (s.layerCollideMatrix[li][j]) collideMask |= (1u << j);
            
            // query: column 기반
            uint32_t queryMask = 0;
            for (int querier = 0; querier < MAX_PHYSICS_LAYERS; ++querier)
                if (s.layerQueryMatrix[querier][li]) queryMask |= (1u << querier);
            
            masks.collideMask = collideMask & ~ignoreLayers;
            masks.queryMask = queryMask & ~ignoreLayers;
        }
        else
        {
            masks.collideMask = 0xFFFFFFFFu;
            masks.queryMask = 0xFFFFFFFFu;
        }
    }
    else
    {
        // 매트릭스가 없으면 기본값
        masks.collideMask = 0xFFFFFFFFu;
        masks.queryMask = 0xFFFFFFFFu;
    }
    return masks;
}
