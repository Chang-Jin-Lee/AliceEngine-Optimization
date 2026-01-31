#pragma once

// PhysXWorld_Internal.h (split from PhysXWorld.cpp)
#include "PhysXWorld.h"
#include "PhysicsMath.h"
#include "Runtime/Foundation/Logger.h"

#include <PhysX/PxPhysicsAPI.h>

// ============================================================
// ContactModify 가드 (공유 thread_local)
// ============================================================
namespace physxwrap_detail {
	// ContactModify 콜백 내부에서 쿼리 호출을 방지하기 위한 depth 카운터
	// inline을 사용하여 모든 번역단위에서 같은 변수를 공유
	// depth > 0이면 ContactModify 콜백 내부
	inline thread_local int g_contactModifyDepth = 0;

	// 차단된 쿼리 횟수 카운터 (경고 로깅용)
	inline std::atomic<uint32_t> g_blockedQueryCount{ 0 };

	// RAII 헬퍼: 콜백 진입 시 자동으로 depth 증가/감소
	struct ContactModifyScope {
		ContactModifyScope() { 
			++g_contactModifyDepth;
			if (g_contactModifyDepth > 1) {
				// 재진입 감지: 경고 로그
				ALICE_LOG_WARN("[PhysXWorld] ContactModify callback reentrant detected (depth=%d)! This may cause deadlock.", g_contactModifyDepth);
			}
		}
		~ContactModifyScope() { 
			--g_contactModifyDepth;
			if (g_contactModifyDepth < 0) {
				g_contactModifyDepth = 0; // 안전장치
			}
		}
	};
	
	// ContactModify 콜백 내부인지 확인 (호환성 유지)
	inline bool IsInContactModifyCallback() {
		return g_contactModifyDepth > 0;
	}
}

// ------------------------------------------------------------
// PhysX Character Controller (CCT) header detection
// ------------------------------------------------------------
#ifndef PHYSXWRAP_ENABLE_CCT
#define PHYSXWRAP_ENABLE_CCT 1
#endif

#ifndef PHYSXWRAP_HAS_CCT_HEADERS
#if PHYSXWRAP_ENABLE_CCT && defined(__has_include)
#  if __has_include(<characterkinematic/PxController.h>) && __has_include(<characterkinematic/PxControllerManager.h>)
#    define PHYSXWRAP_HAS_CCT_HEADERS 1
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 1
#  elif __has_include(<physx/characterkinematic/PxController.h>) && __has_include(<physx/characterkinematic/PxControllerManager.h>)
#    define PHYSXWRAP_HAS_CCT_HEADERS 1
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 2
#  else
#    define PHYSXWRAP_HAS_CCT_HEADERS 0
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#  endif
#elif PHYSXWRAP_ENABLE_CCT
#  define PHYSXWRAP_HAS_CCT_HEADERS 0
#  define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#else
#  define PHYSXWRAP_HAS_CCT_HEADERS 0
#  define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#endif
#endif

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
#  if PHYSXWRAP_CCT_INCLUDE_STYLE == 1
#    include <characterkinematic/PxController.h>
#    include <characterkinematic/PxControllerManager.h>
#    include <characterkinematic/PxCapsuleController.h>
#    include <characterkinematic/PxBoxController.h>
#  elif PHYSXWRAP_CCT_INCLUDE_STYLE == 2
#    include <physx/characterkinematic/PxController.h>
#    include <physx/characterkinematic/PxControllerManager.h>
#    include <physx/characterkinematic/PxCapsuleController.h>
#    include <physx/characterkinematic/PxBoxController.h>
#  endif
#endif

// Mesh cooking (PhysX 5.2+): immediate cooking entry points.
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
#  if PHYSXWRAP_COOKING_INCLUDE_STYLE == 1
#    include <cooking/PxCooking.h>
#    include <cooking/PxTriangleMeshDesc.h>
#    include <cooking/PxConvexMeshDesc.h>
#  elif PHYSXWRAP_COOKING_INCLUDE_STYLE == 2
#    include <physx/cooking/PxCooking.h>
#    include <physx/cooking/PxTriangleMeshDesc.h>
#    include <physx/cooking/PxConvexMeshDesc.h>
#  endif
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace physx;

// ============================================================
//  Helpers
// ============================================================

static inline PxVec3 ToPx(const Vec3& v) { return PxVec3(v.x, v.y, v.z); }
static inline Vec3   FromPx(const PxVec3& v) { return Vec3(v.x, v.y, v.z); }

static inline PxExtendedVec3 ToPxExt(const Vec3& v)
{
	return PxExtendedVec3(static_cast<PxExtended>(v.x), static_cast<PxExtended>(v.y), static_cast<PxExtended>(v.z));
}

static inline Vec3 FromPxExt(const PxExtendedVec3& v)
{
	return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

static inline PxQuat ToPx(const Quat& q)
{
	Quat nq = q;
	nq.Normalize();
	return PxQuat(nq.x, nq.y, nq.z, nq.w);
}

static inline Quat FromPx(const PxQuat& q) { return Quat(q.x, q.y, q.z, q.w); }

static inline PxTransform ToPxTransform(const Vec3& p, const Quat& q)
{
	return PxTransform(ToPx(p), ToPx(q));
}

// Capsule axis alignment: PhysX capsule axis is +X. Rotate +X -> +Y.
static inline PxQuat CapsuleAlignQuatPx()
{
	return PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f)); // 90deg about Z
}

// Bit-flag helpers (avoid ambiguous conversions from PxFlags)
static inline bool HasPairFlag(PxPairFlags flags, PxPairFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasShapeFlag(PxShapeFlags flags, PxShapeFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasHitFlag(PxHitFlags flags, PxHitFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasRigidBodyFlag(PxRigidBodyFlags flags, PxRigidBodyFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasActorFlag(PxActorFlags flags, PxActorFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasTriggerPairFlag(PxTriggerPairFlags flags, PxTriggerPairFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}


// Pointer pair key (commutative)
static inline uint64_t PtrPairKey(const void* a, const void* b)
{
	uintptr_t pa = reinterpret_cast<uintptr_t>(a);
	uintptr_t pb = reinterpret_cast<uintptr_t>(b);
	if (pa > pb) std::swap(pa, pb);
	// mix (splitmix-ish)
	uint64_t x = (uint64_t)pa;
	uint64_t y = (uint64_t)pb;
	x ^= y + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2);
	return x;
}

// ============================================================
//  Mesh hashing (cache keys)
// ============================================================

static inline uint32_t FloatBits(float v)
{
	uint32_t u = 0;
	std::memcpy(&u, &v, sizeof(uint32_t));
	return u;
}

static inline uint64_t HashFNV1a64(uint64_t h, const void* data, size_t len)
{
	constexpr uint64_t prime = 1099511628211ull;
	const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
	for (size_t i = 0; i < len; ++i)
	{
		h ^= uint64_t(p[i]);
		h *= prime;
	}
	return h;
}

static inline uint64_t HashU32(uint64_t h, uint32_t v) { return HashFNV1a64(h, &v, sizeof(v)); }

static inline uint64_t HashVec3(uint64_t h, const Vec3& v)
{
	h = HashU32(h, FloatBits(v.x));
	h = HashU32(h, FloatBits(v.y));
	h = HashU32(h, FloatBits(v.z));
	return h;
}

static inline uint64_t HashVertices(uint64_t h, const Vec3* v, uint32_t count)
{
	for (uint32_t i = 0; i < count; ++i) h = HashVec3(h, v[i]);
	return h;
}

// ============================================================
//  Filter shader data (per-scene)
// ============================================================

struct FilterShaderData
{
	PxU32 enableContactEvents = 1;
	PxU32 enableContactPoints = 0;
	PxU32 enableContactModify = 0;
	PxU32 enableCCD = 0;  // Scene-level CCD 활성화 여부
};

static PxFilterFlags LayerFilterShader(
	PxFilterObjectAttributes attributes0, PxFilterData filterData0,
	PxFilterObjectAttributes attributes1, PxFilterData filterData1,
	PxPairFlags& pairFlags,
	const void* constantBlock, PxU32 constantBlockSize)
{
	// filterData.word0 : layerBits
	// filterData.word1 : collideMask

	const bool allow01 = (filterData0.word1 & filterData1.word0) != 0;
	const bool allow10 = (filterData1.word1 & filterData0.word0) != 0;
	if (!allow01 || !allow10)
		return PxFilterFlag::eSUPPRESS;

	const FilterShaderData* fsd = nullptr;
	if (constantBlock && constantBlockSize >= sizeof(FilterShaderData))
		fsd = reinterpret_cast<const FilterShaderData*>(constantBlock);

	// Triggers vs contacts
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	pairFlags = PxPairFlag::eCONTACT_DEFAULT;

	if (fsd && fsd->enableContactEvents)
	{
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_LOST;

		if (fsd->enableContactPoints) {
			pairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;
		}
	}

	if (fsd && fsd->enableContactModify)
	{
		pairFlags |= PxPairFlag::eMODIFY_CONTACTS;
	}

	// PhysX 5.5: Scene이 CCD를 활성화했으면 contact pair에도 eDETECT_CCD_CONTACT 플래그 필요
	// 개별 body의 eENABLE_CCD 플래그와는 별개로, collision filtering에서도 명시해야 함
	if (fsd && fsd->enableCCD)
	{
		pairFlags |= PxPairFlag::eDETECT_CCD_CONTACT;
	}

	return PxFilterFlag::eDEFAULT;
}

// ============================================================
//  Scene locks (optional)
// ============================================================

struct SceneReadLock
{
	PxScene* scene = nullptr;
	bool enabled = false;
	SceneReadLock(PxScene* s, bool e) : scene(s), enabled(e) { if (enabled && scene) scene->lockRead(); }
	~SceneReadLock() { if (enabled && scene) scene->unlockRead(); }
};

struct SceneWriteLock
{
	PxScene* scene = nullptr;
	bool enabled = false;
	SceneWriteLock(PxScene* s, bool e) : scene(s), enabled(e) { if (enabled && scene) scene->lockWrite(); }
	~SceneWriteLock() { if (enabled && scene) scene->unlockWrite(); }
};

// ============================================================
//  Query callback (layerMask + queryMask + trigger filtering)
// ============================================================

enum class QueryHitMode : uint8_t { Block, Touch };

class MaskQueryCallback final : public PxQueryFilterCallback
{
public:
	MaskQueryCallback(
		uint32_t layerMaskBits,
		uint32_t queryMaskBits,
		bool hitTriggers,
		QueryHitMode mode,
		const PxRigidActor* ignoreActor = nullptr,
		const PxShape* ignoreShape = nullptr,
		void* ignoreUserData = nullptr)
		: layerMask(layerMaskBits)
		, queryMask(queryMaskBits)
		, includeTriggers(hitTriggers)
		, hitMode(mode)
		, ignoreActor(ignoreActor)
		, ignoreShape(ignoreShape)
		, ignoreUserData(ignoreUserData)
	{
	}

	PxQueryHitType::Enum preFilter(
		const PxFilterData& /*filterData*/, const PxShape* shape,
		const PxRigidActor* actor, PxHitFlags& /*queryFlags*/) override
	{
		if (!shape || !actor) return PxQueryHitType::eNONE;

		//  ignore 먼저
		if (ignoreShape && shape == ignoreShape) return PxQueryHitType::eNONE;
		if (ignoreActor && actor == ignoreActor) return PxQueryHitType::eNONE;
		if (ignoreUserData && actor->userData == ignoreUserData) return PxQueryHitType::eNONE;

		const PxShapeFlags sf = shape->getFlags();

		// Must be query shape
		if (!HasShapeFlag(sf, PxShapeFlag::eSCENE_QUERY_SHAPE))
			return PxQueryHitType::eNONE;

		// Trigger handling
		if (!includeTriggers && HasShapeFlag(sf, PxShapeFlag::eTRIGGER_SHAPE))
			return PxQueryHitType::eNONE;

		const PxFilterData fd = shape->getQueryFilterData();
		const uint32_t shapeLayerBits = fd.word0;
		const uint32_t shapeQueryMask = fd.word2;

		if ((shapeLayerBits & layerMask) == 0)
			return PxQueryHitType::eNONE;

		if ((shapeQueryMask & queryMask) == 0)
			return PxQueryHitType::eNONE;

		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

	PxQueryHitType::Enum postFilter(
		const PxFilterData& /*filterData*/, const PxQueryHit& /*hit*/,
		const PxShape* /*shape*/, const PxRigidActor* /*actor*/) override
	{
		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

private:
	uint32_t layerMask = 0xFFFFFFFFu;
	uint32_t queryMask = 0xFFFFFFFFu;
	bool includeTriggers = false;
	QueryHitMode hitMode = QueryHitMode::Block;


	const PxRigidActor* ignoreActor = nullptr;
	const PxShape* ignoreShape = nullptr;
	void* ignoreUserData = nullptr;
};

// ============================================================
//  PhysXWorld::Impl
// ============================================================

struct PhysXWorld::Impl : public std::enable_shared_from_this<PhysXWorld::Impl>
{
	explicit Impl(PhysXContext& inCtx, const PhysXWorld::Desc& desc)
		: ctx(&inCtx)
	{
		physics = ctx->GetPhysics();
		if (!physics) throw std::runtime_error("PhysXContext has no PxPhysics");

		enableSceneLocks = desc.enableSceneLocks;
		enableActiveTransforms = desc.enableActiveTransforms;

		// Pre-reserve activeTransforms to avoid reallocation during simulation
		// (onAdvance callback runs during simulation, so allocations should be minimized)
		if (enableActiveTransforms)
		{
			activeTransforms.reserve(256);  // 예상치: 일반적인 씬에서 활성 액터 수
		}

		// Default material (used for planes and as a fallback)
		defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.0f);
		if (!defaultMaterial) throw std::runtime_error("createMaterial failed");

		// Scene
		PxSceneDesc sdesc(physics->getTolerancesScale());
		sdesc.gravity = ToPx(desc.gravity);
		sdesc.cpuDispatcher = ctx->GetDispatcher();
		sdesc.filterShader = LayerFilterShader;

		shaderData.enableContactEvents = desc.enableContactEvents ? 1u : 0u;
		shaderData.enableContactPoints = desc.enableContactPoints ? 1u : 0u;
		shaderData.enableContactModify = desc.enableContactModify ? 1u : 0u;
		shaderData.enableCCD = desc.enableCCD ? 1u : 0u;  // Scene-level CCD를 필터 셰이더에 전달
		sdesc.filterShaderData = &shaderData;
		sdesc.filterShaderDataSize = sizeof(FilterShaderData);

		sdesc.simulationEventCallback = &eventCb;
		sdesc.contactModifyCallback = desc.enableContactModify ? &eventCb : nullptr;

		// CCD must be enabled on the scene for swept CCD on rigid bodies.
		if (desc.enableCCD)
			sdesc.flags |= PxSceneFlag::eENABLE_CCD;

		// Use PCM by default (better contact generation in most cases)
		sdesc.flags |= PxSceneFlag::eENABLE_PCM;

		// Require RW locks for thread safety (will assert if accessed without locks)
		// This helps catch bugs in multi-threaded scenarios and editor/tooling code.
		if (desc.enableSceneLocks)
			sdesc.flags |= PxSceneFlag::eREQUIRE_RW_LOCK;

		scene = physics->createScene(sdesc);
		if (!scene) throw std::runtime_error("createScene failed");

		{
			SceneWriteLock wl(scene, desc.enableSceneLocks);

			scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0f);
			scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
			scene->setVisualizationParameter(physx::PxVisualizationParameter::eACTOR_AXES, 1.0f);

			if (physx::PxPvdSceneClient* client = scene->getScenePvdClient())
			{
				client->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
				client->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
				client->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
			}
		}

		eventCb.owner.reset();

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		// Character Controller Manager (CCT)
		controllerMgr = PxCreateControllerManager(*scene);
#endif
	}

	~Impl()
	{
		// Ensure callback doesn't access freed Impl
		eventCb.owner.reset();

		FlushPending(true);

		if (defaultMaterial) defaultMaterial->release();
		defaultMaterial = nullptr;

		// Release controllers/manager before scene teardown.
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		if (controllerMgr)
		{
			// Best-effort: controller manager should have no live controllers at this point.
			controllerMgr->release();
			controllerMgr = nullptr;
		}
#endif

		if (scene) scene->release();
		scene = nullptr;

		// Release cached materials/meshes
		ClearMeshCachesInternal();
	}

	// ------------------------------------------------------------
	// Simulation callback
	// ------------------------------------------------------------
	struct EventCallback final : public PxSimulationEventCallback, physx::PxContactModifyCallback
	{
		std::weak_ptr<Impl> owner;

		void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !constraints || count == 0) return;

			std::scoped_lock lock(s->eventMtx);

			for (PxU32 i = 0; i < count; ++i)
			{
				PxConstraintInfo& ci = constraints[i];

				PxJoint* joint = reinterpret_cast<PxJoint*>(ci.externalReference);

				PhysicsEvent e;
				e.type = PhysicsEventType::JointBreak;

				if (joint)
				{
					e.nativeJoint = joint;
					e.jointUserData = joint->userData;

					PxRigidActor* a = nullptr;
					PxRigidActor* b = nullptr;
					joint->getActors(a, b);

					e.nativeActorA = a;
					e.nativeActorB = b;
					e.userDataA = a ? a->userData : nullptr;
					e.userDataB = b ? b->userData : nullptr;
				}
				else
				{
					// externalReference가 없는 케이스 대비
					e.nativeJoint = ci.constraint;
				}

				s->events.push_back(e);
			}
		}
		void onWake(PxActor**, PxU32) override {}
		void onSleep(PxActor**, PxU32) override {}
		// onAdvance는 simulate~fetchResults 사이(시뮬레이션 도는 중)에 호출됨
		// PhysX 문서: PxSimulationEventCallback::onAdvance는 eENABLE_POSE_INTEGRATION_PREVIEW가
		// 켜진 바디들의 포즈를 미리 제공하기 위해 시뮬레이션 중간에 호출됨
		// 주의: 이 콜백 내에서는 할당/로그/복잡한 연산을 피해야 함 (성능/안정성)
		void onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !s->enableActiveTransforms) return;
			if (!bodyBuffer || !poseBuffer || count == 0) return;

			std::scoped_lock lock(s->activeMtx);
			// reserve로 할당 최소화 (시뮬레이션 중 할당을 피하기 위함)
			s->activeTransforms.reserve(s->activeTransforms.size() + count);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxRigidBody* rb = bodyBuffer[i];
				if (!rb) continue;

				ActiveTransform at{};
				at.nativeActor = const_cast<PxRigidBody*>(rb);
				at.userData = rb->userData;
				at.position = FromPx(poseBuffer[i].p);
				at.rotation = FromPx(poseBuffer[i].q);

				s->activeTransforms.emplace_back(at);
			}
		}

		void onTrigger(PxTriggerPair* pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs) return;

			std::scoped_lock lock(s->eventMtx, s->contactStateMtx);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxTriggerPair& tp = pairs[i];

				const bool removedShape = HasTriggerPairFlag(tp.flags, PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER) ||
					HasTriggerPairFlag(tp.flags, PxTriggerPairFlag::eREMOVED_SHAPE_OTHER);

				const PxShape* shA = tp.triggerShape;
				const PxShape* shB = tp.otherShape;
				const PxActor* acA = tp.triggerActor;
				const PxActor* acB = tp.otherActor;

				// if shape removed, 정리 후 스킵
				if (removedShape)
				{
					// 가능한 범위에서 상태 정리 (누수 방지)
					// shape가 유효하면 shapeKey를 계산해서 제거
					if (shA && shB && acA && acB)
					{
						const uint64_t shapeKey = PtrPairKey(shA, shB);
						const uint64_t actorKey = PtrPairKey(acA, acB);
						
						if (s->activeTriggerShapePairs.erase(shapeKey) > 0)
						{
							// actorCount 감소
							auto it = s->activeTriggerActorCounts.find(actorKey);
							if (it != s->activeTriggerActorCounts.end())
							{
								if (it->second > 0) --it->second;
								if (it->second == 0)
									s->activeTriggerActorCounts.erase(it);
							}
						}
					}
					continue;
				}

				if (!shA || !shB || !acA || !acB)
					continue;

				const uint64_t shapeKey = PtrPairKey(shA, shB);
				const uint64_t actorKey = PtrPairKey(acA, acB);

				const bool begin = HasPairFlag(tp.status, PxPairFlag::eNOTIFY_TOUCH_FOUND);
				const bool end = HasPairFlag(tp.status, PxPairFlag::eNOTIFY_TOUCH_LOST);

				if (begin)
				{
					if (s->activeTriggerShapePairs.insert(shapeKey).second)
					{
						uint32_t& cnt = s->activeTriggerActorCounts[actorKey];
						++cnt;
						if (cnt == 1)
						{
							PhysicsEvent e;
							e.type = PhysicsEventType::TriggerEnter;
							e.nativeActorA = const_cast<PxActor*>(acA);
							e.nativeActorB = const_cast<PxActor*>(acB);
							e.nativeShapeA = const_cast<PxShape*>(shA);
							e.nativeShapeB = const_cast<PxShape*>(shB);
							e.userDataA = const_cast<void*>(acA->userData);
							e.userDataB = const_cast<void*>(acB->userData);
							s->events.push_back(e);
						}
					}
				}

				if (end)
				{
					if (s->activeTriggerShapePairs.erase(shapeKey) > 0)
					{
						auto it = s->activeTriggerActorCounts.find(actorKey);
						if (it != s->activeTriggerActorCounts.end())
						{
							if (it->second > 0) --it->second;
							if (it->second == 0)
							{
								s->activeTriggerActorCounts.erase(it);

								PhysicsEvent e;
								e.type = PhysicsEventType::TriggerExit;
								e.nativeActorA = const_cast<PxActor*>(acA);
								e.nativeActorB = const_cast<PxActor*>(acB);
								e.nativeShapeA = const_cast<PxShape*>(shA);
								e.nativeShapeB = const_cast<PxShape*>(shB);
								e.userDataA = const_cast<void*>(acA->userData);
								e.userDataB = const_cast<void*>(acB->userData);
								s->events.push_back(e);
							}
						}
					}
				}
			}
		}

		void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs || count == 0) return;

			const PxActor* ac0 = header.actors[0];
			const PxActor* ac1 = header.actors[1];
			if (!ac0 || !ac1) return;

			std::scoped_lock lock(s->eventMtx, s->contactStateMtx);

			const uint64_t actorKey = PtrPairKey(ac0, ac1);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxContactPair& cp = pairs[i];

				const bool removedShape = (static_cast<PxU32>(cp.flags) &
					(static_cast<PxU32>(PxContactPairFlag::eREMOVED_SHAPE_0) |
						static_cast<PxU32>(PxContactPairFlag::eREMOVED_SHAPE_1))) != 0u;

				const PxShape* sh0 = cp.shapes[0];
				const PxShape* sh1 = cp.shapes[1];

				// if shape removed, 정리 후 스킵
				if (removedShape)
				{
					// 가능한 범위에서 상태 정리 (누수 방지)
					// shape가 유효하면 shapeKey를 계산해서 제거
					if (sh0 && sh1)
					{
						const uint64_t shapeKey = PtrPairKey(sh0, sh1);
						if (s->activeContactShapePairs.erase(shapeKey) > 0)
						{
							// actorCount 감소
							auto it = s->activeContactActorCounts.find(actorKey);
							if (it != s->activeContactActorCounts.end())
							{
								if (it->second > 0) --it->second;
								if (it->second == 0)
									s->activeContactActorCounts.erase(it);
							}
						}
					}
					continue;
				}

				if (!sh0 || !sh1) continue;

				const uint64_t shapeKey = PtrPairKey(sh0, sh1);

				const bool begin = HasPairFlag(cp.events, PxPairFlag::eNOTIFY_TOUCH_FOUND);
				const bool end = HasPairFlag(cp.events, PxPairFlag::eNOTIFY_TOUCH_LOST);

				if (begin)
				{
					if (s->activeContactShapePairs.insert(shapeKey).second)
					{
						uint32_t& cnt = s->activeContactActorCounts[actorKey];
						++cnt;

						if (cnt == 1)
						{
							PhysicsEvent e;
							e.type = PhysicsEventType::ContactBegin;
							e.nativeActorA = const_cast<PxActor*>(ac0);
							e.nativeActorB = const_cast<PxActor*>(ac1);
							e.nativeShapeA = const_cast<PxShape*>(sh0);
							e.nativeShapeB = const_cast<PxShape*>(sh1);
							e.userDataA = const_cast<void*>(ac0->userData);
							e.userDataB = const_cast<void*>(ac1->userData);

							if (s->shaderData.enableContactPoints && HasPairFlag(cp.events, PxPairFlag::eNOTIFY_CONTACT_POINTS))
							{
								PxContactPairPoint pts[1];
								PxU32 n = cp.extractContacts(pts, 1);
								if (n > 0)
								{
									e.position = FromPx(pts[0].position);
									e.normal = FromPx(pts[0].normal);
								}
							}

							s->events.push_back(e);
						}
					}
				}

				if (end)
				{
					if (s->activeContactShapePairs.erase(shapeKey) > 0)
					{
						auto it = s->activeContactActorCounts.find(actorKey);
						if (it != s->activeContactActorCounts.end())
						{
							if (it->second > 0) --it->second;
							if (it->second == 0)
							{
								s->activeContactActorCounts.erase(it);

								PhysicsEvent e;
								e.type = PhysicsEventType::ContactEnd;
								e.nativeActorA = const_cast<PxActor*>(ac0);
								e.nativeActorB = const_cast<PxActor*>(ac1);
								e.nativeShapeA = const_cast<PxShape*>(sh0);
								e.nativeShapeB = const_cast<PxShape*>(sh1);
								e.userDataA = const_cast<void*>(ac0->userData);
								e.userDataB = const_cast<void*>(ac1->userData);
								s->events.push_back(e);
							}
						}
					}
				}
			}
		}

		// !!!주의!!!주의!!!주의!!! DEADLOCK WARNING: ContactModify 콜백은 PhysX 시뮬레이션 스레드에서 호출될 수 있습니다.
		// Step()이 scene write lock을 잡은 채로 simulate~fetch 사이에 실행되므로,
		// 이 콜백 내에서 Raycast(), Overlap() 등 scene lock이 필요한 함수를 호출하면 데드락이 발생할 수 있습니다.
		// 
		// 안전한 사용:
		//   - ContactModifyPair의 데이터만 읽고 수정
		//   - 로컬 변수/메모리만 접근
		//   - scene lock이 필요 없는 작업만 수행
		//
		// !!!위험!!!위험!!!위험!!!위험한 사용 (데드락 위험):
		//   - world->Raycast(), world->Overlap() 등 쿼리 함수 호출
		//   - scene에 접근하는 모든 함수 호출
		//   - 다른 스레드와의 동기화가 필요한 작업
		void onContactModify(PxContactModifyPair* const pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs || count == 0) return;

			// Fast path: no user callback registered.
			ContactModifyCallback cb = nullptr;
			void* user = nullptr;
			{
				std::scoped_lock lock(s->contactModifyMtx);
				cb = s->contactModifyCb;
				user = s->contactModifyUser;
			}
			if (!cb) return;

			// ContactModify 콜백 진입 플래그 설정 (데드락 방지)
			// 재진입 방지: 이미 콜백 내부에 있으면 즉시 리턴
			if (physxwrap_detail::IsInContactModifyCallback()) return;
			// RAII를 사용하여 자동으로 플래그 설정/해제
			physxwrap_detail::ContactModifyScope scope;

			for (PxU32 i = 0; i < count; ++i)
			{
				PxContactModifyPair& mp = pairs[i];
				// In PhysX 5.x these are exposed as const pointers.
				const PxRigidActor* a = mp.actor[0];
				const PxRigidActor* b = mp.actor[1];
				const PxShape* shA = mp.shape[0];
				const PxShape* shB = mp.shape[1];
				if (!a || !b || !shA || !shB) continue;

				ContactModifyPair pair;
				pair.userDataA = a->userData;
				pair.userDataB = b->userData;
				pair.nativeActorA = const_cast<PxRigidActor*>(a);
				pair.nativeActorB = const_cast<PxRigidActor*>(b);
				pair.nativeShapeA = const_cast<PxShape*>(shA);
				pair.nativeShapeB = const_cast<PxShape*>(shB);

				PxContactSet& cs = mp.contacts;
				const PxU32 n = cs.size();
				pair.contacts.resize(n);
				for (PxU32 c = 0; c < n; ++c)
				{
					ContactModifyPoint& dst = pair.contacts[c];
					dst.position = FromPx(cs.getPoint(c));
					dst.normal = FromPx(cs.getNormal(c));
					dst.separation = cs.getSeparation(c);
					dst.targetVelocity = FromPx(cs.getTargetVelocity(c));
					dst.maxImpulse = cs.getMaxImpulse(c);
				}

				// !!! 사용자 콜백 호출: 이 콜백 내에서 scene lock이 필요한 함수를 호출하지 마세요!
				cb(pair, user);

				if (pair.ignorePair)
				{
					for (PxU32 c = 0; c < n; ++c)
						cs.ignore(c);
					continue;
				}

				const PxU32 m = static_cast<PxU32>(std::min<size_t>(n, pair.contacts.size()));
				for (PxU32 c = 0; c < m; ++c)
				{
					const ContactModifyPoint& src = pair.contacts[c];
					if (src.ignore)
					{
						cs.ignore(c);
						continue;
					}
					cs.setPoint(c, ToPx(src.position));
					cs.setNormal(c, ToPx(src.normal));
					cs.setSeparation(c, src.separation);
					cs.setTargetVelocity(c, ToPx(src.targetVelocity));
					if (src.maxImpulse >= 0.0f)
						cs.setMaxImpulse(c, src.maxImpulse);
				}
			}
			// scope 소멸 시 자동으로 플래그 해제됨
		}
	};

	// ------------------------------------------------------------
	// Public-ish state
	// ------------------------------------------------------------

	PhysXContext* ctx = nullptr;
	PxPhysics* physics = nullptr;
	PxScene* scene = nullptr;
	PxMaterial* defaultMaterial = nullptr;

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	PxControllerManager* controllerMgr = nullptr;
#endif

	bool enableSceneLocks = true;


	bool enableActiveTransforms = true;

	std::mutex activeMtx;
	std::vector<ActiveTransform> activeTransforms;

	FilterShaderData shaderData{};
	EventCallback eventCb{};

	// Events
	std::mutex eventMtx;
	std::vector<PhysicsEvent> events;

	// Optional contact modify callback (registered from game thread)
	std::mutex contactModifyMtx;
	ContactModifyCallback contactModifyCb = nullptr;
	void* contactModifyUser = nullptr;

	// Contact/trigger dedup state
	std::mutex contactStateMtx;
	std::unordered_set<uint64_t> activeContactShapePairs;
	std::unordered_map<uint64_t, uint32_t> activeContactActorCounts;
	std::unordered_set<uint64_t> activeTriggerShapePairs;
	std::unordered_map<uint64_t, uint32_t> activeTriggerActorCounts;

	// Pending adds/releases (for safety around simulate/fetch)
	std::mutex pendingMtx;
	enum class ActorOpType : uint8_t { Add, Remove };
	struct ActorOp { PxActor* actor = nullptr; ActorOpType type = ActorOpType::Add; };
	std::vector<ActorOp> pendingActorOps;
	std::unordered_set<PxActor*> pendingAddSet; // 동일 액터 Add 중복 방지 (키네마틱 등 첫 프레임 2회 Enqueue 방지)
	std::vector<PxBase*> pendingRelease; // actors, joints, meshes, etc.
	std::unordered_set<PxBase*> pendingReleaseSet;

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	std::vector<PxController*> pendingControllerRelease;
#endif

	// Caches
	std::mutex materialCacheMtx;
	std::unordered_map<uint64_t, PxMaterial*> materialCache;

	// HeightField cache (does not require cooking)
	std::mutex heightFieldCacheMtx;
	std::unordered_map<uint64_t, PxHeightField*> heightFieldCache;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	std::mutex meshCacheMtx;
	std::unordered_map<uint64_t, PxTriangleMesh*> triMeshCache;
	std::unordered_map<uint64_t, PxConvexMesh*> convexMeshCache;
#endif

	// ------------------------------------------------------------
	// Pending maintenance
	// ------------------------------------------------------------

	void EnqueueAdd(PxActor* a)
	{
		if (!a) return;
		std::scoped_lock lock(pendingMtx);
		if (!pendingAddSet.insert(a).second)
			return; // 이미 Add 대기 중이면 중복 푸시 방지
		pendingActorOps.push_back({ a, ActorOpType::Add });
	}

	void EnqueueRemove(PxActor* a)
	{
		if (!a) return;
		std::scoped_lock lock(pendingMtx);
		pendingAddSet.erase(a); // 같은 액터를 나중에 다시 Add 할 수 있도록
		pendingActorOps.push_back({ a, ActorOpType::Remove });
	}

	void EnqueueRelease(PxBase* b)
	{
		if (!b) return;
		std::scoped_lock lock(pendingMtx);
		if (!pendingReleaseSet.insert(b).second)
			return;
		pendingRelease.push_back(b);
	}

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	void EnqueueControllerRelease(PxController* c)
	{
		if (!c) return;
		std::scoped_lock lock(pendingMtx);
		pendingControllerRelease.push_back(c);
	}
#endif

	void FlushPending(bool allowImmediateRelease)
	{
		std::vector<ActorOp> actorOps;
		std::unordered_set<PxActor*> addSet;
		std::vector<PxBase*> rels;
		std::unordered_set<PxBase*> relSet;

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		std::vector<PxController*> ctrls;
#endif
		{
			std::scoped_lock lock(pendingMtx);
			actorOps.swap(pendingActorOps);
			addSet.swap(pendingAddSet);
			rels.swap(pendingRelease);
			relSet.swap(pendingReleaseSet);

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
			ctrls.swap(pendingControllerRelease);
#endif
		}

		if (!allowImmediateRelease)
		{
			// Can't touch scene now (e.g. during simulate). Put everything back.
			std::scoped_lock lock(pendingMtx);
			pendingActorOps.insert(pendingActorOps.end(), actorOps.begin(), actorOps.end());
			pendingAddSet.insert(addSet.begin(), addSet.end());
			pendingRelease.insert(pendingRelease.end(), rels.begin(), rels.end());
			pendingReleaseSet.insert(relSet.begin(), relSet.end());

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
			pendingControllerRelease.insert(pendingControllerRelease.end(), ctrls.begin(), ctrls.end());
#endif
			return;
		}

		if (scene)
		{
			SceneWriteLock wl(scene, enableSceneLocks);
			for (const ActorOp& op : actorOps)
			{
				PxActor* a = op.actor;
				if (!a) continue;
				if (relSet.find(static_cast<PxBase*>(a)) != relSet.end())
					continue;
				switch (op.type)
				{
				case ActorOpType::Add:
					if (!a->getScene())
					{
						scene->addActor(*a);
						if (enableActiveTransforms)
						{
							if (PxRigidDynamic* rd = a->is<PxRigidDynamic>())
								rd->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_POSE_INTEGRATION_PREVIEW, true);
						}
					}
					break;
				case ActorOpType::Remove:
					if (a->getScene() == scene)
						scene->removeActor(*a);
					break;
				}
			}
		}

		for (PxBase* b : rels)
		{
			if (!b) continue;
			if (scene)
			{
				if (PxActor* a = b->is<PxActor>())
				{
					if (a->getScene() == scene)
					{
						SceneWriteLock wl(scene, enableSceneLocks);
						scene->removeActor(*a);
					}
				}
			}
			b->release();
		}

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		if (!ctrls.empty())
		{
			if (scene)
			{
				SceneWriteLock wl(scene, enableSceneLocks);
				for (PxController* c : ctrls)
					if (c) c->release();
			}
			else
			{
				for (PxController* c : ctrls)
					if (c) c->release();
			}
		}
#endif
	}

	// ------------------------------------------------------------
	// Materials
	// ------------------------------------------------------------

	PxMaterial* GetOrCreateMaterial(const MaterialDesc& m)
	{
		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, FloatBits(m.staticFriction));
		h = HashU32(h, FloatBits(m.dynamicFriction));
		h = HashU32(h, FloatBits(m.restitution));

		{
			std::scoped_lock lock(materialCacheMtx);
			auto it = materialCache.find(h);
			if (it != materialCache.end()) return it->second;
		}

		PxMaterial* mat = physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
		if (!mat) return defaultMaterial;

		{
			std::scoped_lock lock(materialCacheMtx);
			materialCache.emplace(h, mat);
		}

		return mat;
	}

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	PxTriangleMesh* GetOrCreateTriangleMesh(const TriangleMeshColliderDesc& mesh)
	{
		const PxCookingParams* cook = ctx ? ctx->GetCookingParams() : nullptr;
		if (!cook) return nullptr;

		if (!mesh.vertices || mesh.vertexCount == 0) return nullptr;
		if ((!mesh.indices16 && !mesh.indices32) || mesh.indexCount < 3 || (mesh.indexCount % 3) != 0) return nullptr;

		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, mesh.vertexCount);
		h = HashVertices(h, mesh.vertices, mesh.vertexCount);

		h = HashU32(h, mesh.indexCount);
		if (mesh.indices32)
			h = HashFNV1a64(h, mesh.indices32, sizeof(uint32_t) * mesh.indexCount);
		else
			h = HashFNV1a64(h, mesh.indices16, sizeof(uint16_t) * mesh.indexCount);

		h = HashU32(h, mesh.flipNormals ? 1u : 0u);
		h = HashU32(h, mesh.validate ? 1u : 0u);

		{
			std::scoped_lock lock(meshCacheMtx);
			auto it = triMeshCache.find(h);
			if (it != triMeshCache.end()) return it->second;
		}

	PxTriangleMeshDesc desc{};
	desc.points.count = mesh.vertexCount;
	desc.points.stride = sizeof(Vec3); // Vec3의 실제 크기 사용 (PxVec3와 레이아웃이 다를 수 있음)
	desc.points.data = mesh.vertices;

		if (mesh.indices32)
		{
			desc.triangles.count = mesh.indexCount / 3;
			desc.triangles.stride = 3 * sizeof(uint32_t);
			desc.triangles.data = mesh.indices32;
			// no flag means 32-bit
		}
		else
		{
			desc.flags |= PxMeshFlag::e16_BIT_INDICES;
			desc.triangles.count = mesh.indexCount / 3;
			desc.triangles.stride = 3 * sizeof(uint16_t);
			desc.triangles.data = mesh.indices16;
		}

		if (mesh.flipNormals)
			desc.flags |= PxMeshFlag::eFLIPNORMALS;

		if (mesh.validate)
		{
			const bool ok = PxValidateTriangleMesh(*cook, desc);
			if (!ok) return nullptr;
		}

		PxTriangleMesh* tm = PxCreateTriangleMesh(*cook, desc, physics->getPhysicsInsertionCallback());
		if (!tm) return nullptr;

		{
			std::scoped_lock lock(meshCacheMtx);
			triMeshCache.emplace(h, tm);
		}

		return tm;
	}

	PxConvexMesh* GetOrCreateConvexMesh(const ConvexMeshColliderDesc& mesh)
	{
		const PxCookingParams* cook = ctx ? ctx->GetCookingParams() : nullptr;
		if (!cook) return nullptr;

		if (!mesh.vertices || mesh.vertexCount == 0) return nullptr;

		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, mesh.vertexCount);
		h = HashVertices(h, mesh.vertices, mesh.vertexCount);
		h = HashU32(h, mesh.shiftVertices ? 1u : 0u);
		h = HashU32(h, mesh.vertexLimit);
		h = HashU32(h, mesh.validate ? 1u : 0u);

		{
			std::scoped_lock lock(meshCacheMtx);
			auto it = convexMeshCache.find(h);
			if (it != convexMeshCache.end()) return it->second;
		}

	PxConvexMeshDesc desc{};
	desc.points.count = mesh.vertexCount;
	desc.points.stride = sizeof(Vec3); // Vec3의 실제 크기 사용 (PxVec3와 레이아웃이 다를 수 있음)
	desc.points.data = mesh.vertices;
		desc.flags |= PxConvexFlag::eCOMPUTE_CONVEX;
		if (mesh.shiftVertices)
			desc.flags |= PxConvexFlag::eSHIFT_VERTICES;

		desc.vertexLimit = static_cast<PxU16>(std::min<uint32_t>(mesh.vertexLimit, 255u));

		if (mesh.validate)
		{
			const bool ok = PxValidateConvexMesh(*cook, desc);
			if (!ok) return nullptr;
		}

		PxConvexMesh* cm = PxCreateConvexMesh(*cook, desc, physics->getPhysicsInsertionCallback());
		if (!cm) return nullptr;

		{
			std::scoped_lock lock(meshCacheMtx);
			convexMeshCache.emplace(h, cm);
		}

		return cm;
	}
#endif

	// HeightField creation (requires cooking path in PhysX 5.x)
	PxHeightField* GetOrCreateHeightField(const HeightFieldColliderDesc& hf)
	{
		if (!physics) return nullptr;
		if (!hf.heightSamples) return nullptr;
		if (hf.numRows < 2 || hf.numCols < 2) return nullptr;

		if (hf.heightScale <= 0.0f) return nullptr;
		if (hf.rowScale <= 0.0f) return nullptr;
		if (hf.colScale <= 0.0f) return nullptr;

		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, hf.numRows);
		h = HashU32(h, hf.numCols);
		h = HashU32(h, FloatBits(hf.heightScale));
		h = HashFNV1a64(h, hf.heightSamples, sizeof(float) * hf.numRows * hf.numCols);

		{
			std::scoped_lock lock(heightFieldCacheMtx);
			auto it = heightFieldCache.find(h);
			if (it != heightFieldCache.end()) return it->second;
		}

		std::vector<PxHeightFieldSample> samples(hf.numRows * hf.numCols);
		
		for (uint32_t i = 0; i < hf.numRows * hf.numCols; ++i)
		{
			PxHeightFieldSample s{};
			const float hWorld = hf.heightSamples[i];

			const float q = hWorld / hf.heightScale;
			long qi = std::lround(q);
			qi = std::clamp<long>(qi, -32768, 32767);

			s.height = static_cast<PxI16>(qi);
			s.materialIndex0 = 0;
			s.materialIndex1 = 0;
			s.clearTessFlag();

			samples[i] = s;
		}

		// Create PxHeightFieldDesc
		PxHeightFieldDesc desc{};
		desc.format = PxHeightFieldFormat::eS16_TM;
		desc.nbRows = static_cast<PxU32>(hf.numRows);
		desc.nbColumns = static_cast<PxU32>(hf.numCols);
		desc.samples.data = samples.data();
		desc.samples.stride = sizeof(PxHeightFieldSample);

		PxHeightField* heightField = PxCreateHeightField(desc, physics->getPhysicsInsertionCallback());
		if (!heightField) return nullptr;

		{
			std::scoped_lock lock(heightFieldCacheMtx);
			heightFieldCache.emplace(h, heightField);
		}

		return heightField;
	}

	void ClearMeshCachesInternal()
	{
		{
			std::scoped_lock lock(materialCacheMtx);
			for (auto& kv : materialCache)
				if (kv.second) kv.second->release();
			materialCache.clear();
		}

		{
			std::scoped_lock lock(heightFieldCacheMtx);
			for (auto& kv : heightFieldCache)
				if (kv.second) kv.second->release();
			heightFieldCache.clear();
		}

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		{
			std::scoped_lock lock(meshCacheMtx);
			for (auto& kv : triMeshCache)
				if (kv.second) kv.second->release();
			triMeshCache.clear();

			for (auto& kv : convexMeshCache)
				if (kv.second) kv.second->release();
			convexMeshCache.clear();
		}
#endif
	}
};

// ============================================================
//  Local helpers for actor/shape setup
// ============================================================

static inline PxFilterData MakeSimFilter(const FilterDesc& f)
{
	PxFilterData d{};
	d.word0 = f.layerBits;
	d.word1 = f.collideMask;
	return d;
}

static inline PxFilterData MakeQueryFilter(const FilterDesc& f)
{
	PxFilterData d{};
	d.word0 = f.layerBits;
	d.word2 = f.queryMask;
	return d;
}

static inline void ApplyFilterToShape(PxShape& shape, const FilterDesc& f)
{
	shape.setSimulationFilterData(MakeSimFilter(f));
	shape.setQueryFilterData(MakeQueryFilter(f));

	// In PhysX 5.x, PxFlags may not expose mutating helpers like set(flag,bool).
	// Prefer the explicit PxShape::setFlag API.
	shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);

	if (f.isTrigger)
	{
		shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}
	else
	{
		shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
		shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
	}
}

static inline PxRigidDynamicLockFlags ToPxLockFlags(RigidBodyLockFlags f)
{
	PxRigidDynamicLockFlags out;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearX)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearY)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearZ)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularX)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularY)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularZ)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
	return out;
}

static inline PxForceMode::Enum ToPxForceMode(ForceMode m)
{
	switch (m)
	{
	case ForceMode::Force:          return PxForceMode::eFORCE;
	case ForceMode::Impulse:        return PxForceMode::eIMPULSE;
	case ForceMode::VelocityChange: return PxForceMode::eVELOCITY_CHANGE;
	case ForceMode::Acceleration:   return PxForceMode::eACCELERATION;
	default:                        return PxForceMode::eFORCE;
	}
}

static void ApplyMass(PxRigidDynamic& body, const RigidBodyDesc& rb)
{
	if (rb.isKinematic)
		return;

	if (rb.massOverride > 0.0f)
	{
		// This updates inertia based on attached shapes.
		PxRigidBodyExt::setMassAndUpdateInertia(body, rb.massOverride);
	}
	else
	{
		const float density = (rb.density > 0.0f) ? rb.density : 1.0f;
		PxRigidBodyExt::updateMassAndInertia(body, density);
	}
}

// ============================================================
//  Wrapper types
// ============================================================

class PhysXActor : public IPhysicsActor
{
public:
	PhysXActor(PxRigidActor* a, std::weak_ptr<PhysXWorld::Impl> w)
		: actor(a), world(std::move(w))
	{
	}

	~PhysXActor() override
	{
		auto s = world.lock();
		if (s && actor)
			s->EnqueueRelease(actor);
		actor = nullptr;
	}

	bool IsValid() const override { return actor != nullptr; }

	bool IsInWorld() const override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return actor->getScene() != nullptr;
	}

	void SetInWorld(bool inWorld) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		// getScene() 호출 자체가 simulate/fetchResults 타이밍에 불법이 될 수 있음.
		// 그냥 enqueue만 하고, FlushPending에서 중복/불필요 작업을 걸러내게 둔다.
		if (inWorld)
			s->EnqueueAdd(actor);
		else
			s->EnqueueRemove(actor);
	}

	void SetTransform(const Vec3& p, const Quat& q) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		actor->setGlobalPose(ToPxTransform(p, q));
	}

	Vec3 GetPosition() const override
	{
		if (!actor) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(actor->getGlobalPose().p);
	}

	Quat GetRotation() const override
	{
		if (!actor) return Quat::Identity;
		auto s = world.lock();
		if (!s || !s->scene) return Quat::Identity;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(actor->getGlobalPose().q);
	}

	void SetUserData(void* ptr) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		actor->userData = ptr;
	}

	void* GetUserData() const override
	{
		if (!actor) return nullptr;
		auto s = world.lock();
		if (!s || !s->scene) return nullptr;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return actor->userData;
	}

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		FilterDesc f;
		f.layerBits = layerBits;
		f.collideMask = collideMask;
		f.queryMask = queryMask;

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			// preserve trigger flag
			PxShapeFlags sf = sh->getFlags();
			f.isTrigger = HasShapeFlag(sf, PxShapeFlag::eTRIGGER_SHAPE);
			ApplyFilterToShape(*sh, f);
		}
	}

	void SetTrigger(bool isTrigger) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;

			FilterDesc f;
			const PxFilterData qd = sh->getQueryFilterData();
			const PxFilterData sd = sh->getSimulationFilterData();
			f.layerBits = qd.word0;
			f.queryMask = qd.word2;
			f.collideMask = sd.word1;
			f.isTrigger = isTrigger;
			ApplyFilterToShape(*sh, f);
		}
	}

	void SetMaterial(float staticFriction, float dynamicFriction, float restitution) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		MaterialDesc md;
		md.staticFriction = staticFriction;
		md.dynamicFriction = dynamicFriction;
		md.restitution = restitution;

		PxMaterial* mat = s->GetOrCreateMaterial(md);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			sh->setMaterials(&mat, 1);
		}
	}

	void SetCollisionEnabled(bool enabled) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			PxShapeFlags f = sh->getFlags();
			if (HasShapeFlag(f, PxShapeFlag::eTRIGGER_SHAPE))
				continue;
			sh->setFlag(PxShapeFlag::eSIMULATION_SHAPE, enabled);
		}
	}

	bool IsCollisionEnabled() const override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			PxShapeFlags f = sh->getFlags();
			if (HasShapeFlag(f, PxShapeFlag::eTRIGGER_SHAPE))
				continue;
			return HasShapeFlag(f, PxShapeFlag::eSIMULATION_SHAPE);
		}
		return false;
	}

	void SetQueryEnabled(bool enabled) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			sh->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, enabled);
		}
	}

	bool IsQueryEnabled() const override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		if (n == 0) return false;
		PxShape* sh = nullptr;
		actor->getShapes(&sh, 1);
		if (!sh) return false;
		return HasShapeFlag(sh->getFlags(), PxShapeFlag::eSCENE_QUERY_SHAPE);
	}

	void Destroy() override
	{
		PxRigidActor* a = actor;
		if (!a) return;
		actor = nullptr;

		auto s = world.lock();
		if (!s) return;

		if (s->scene)
			s->EnqueueRemove(a);
		s->EnqueueRelease(a);
	}

	// ---- Shapes

	bool AddBoxShape(const BoxColliderDesc& box, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxBoxGeometry geom(ToPx(box.halfExtents));
		return AddShapeCommon(geom, box, localPos, localRot);
	}

	bool AddSphereShape(const SphereColliderDesc& sphere, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxSphereGeometry geom(sphere.radius);
		return AddShapeCommon(geom, sphere, localPos, localRot);
	}

	bool AddCapsuleShape(const CapsuleColliderDesc& capsule, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxCapsuleGeometry geom(capsule.radius, capsule.halfHeight);
		Quat q = localRot;
		if (capsule.alignYAxis)
		{
			Quat align = FromPx(CapsuleAlignQuatPx());
			q = q * align; // 쿼리 쪽(q = q * align)과 순서 통일
		}
		return AddShapeCommon(geom, capsule, localPos, q);
	}

	bool AddTriangleMeshShape(const TriangleMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		if (!actor) return false;
		
		// PhysX 5.5: TriangleMesh는 트리거 shape로 지원하지 않음
		if (mesh.isTrigger)
		{
			// 경고는 PhysicsSystem에서 이미 출력했으므로 여기서는 false만 반환
			return false;
		}
		
		auto s = world.lock();
		if (!s || !s->scene) return false;

		PxTriangleMesh* tm = s->GetOrCreateTriangleMesh(mesh);
		if (!tm) return false;

		PxMeshGeometryFlags gflags{}; // 초기화 필수: 미초기화 시 랜덤 플래그로 인한 크래시 위험
		if (mesh.doubleSidedQueries) gflags |= PxMeshGeometryFlag::eDOUBLE_SIDED;

		const PxMeshScale scale(ToPx(mesh.scale));
		const PxTriangleMeshGeometry geom(tm, scale, gflags);
		return AddShapeCommon(geom, mesh, localPos, localRot);
#else
		// 쿠킹이 비활성화된 경우: TriangleMesh는 런타임 쿠킹이 필요하므로 생성 불가
		// 매개변수 미사용 경고 방지를 위한 void 캐스팅
		(void)mesh; (void)localPos; (void)localRot;
		return false;
#endif
	}

	bool AddConvexMeshShape(const ConvexMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		PxConvexMesh* cm = s->GetOrCreateConvexMesh(mesh);
		if (!cm) return false;

		const PxMeshScale scale(ToPx(mesh.scale));
		const PxConvexMeshGeometry geom(cm, scale);
		return AddShapeCommon(geom, mesh, localPos, localRot);
#else
		// 쿠킹이 비활성화된 경우: ConvexMesh는 런타임 쿠킹이 필요하므로 생성 불가
		// 매개변수 미사용 경고 방지를 위한 void 캐스팅
		(void)mesh; (void)localPos; (void)localRot;
		return false;
#endif
	}

	bool AddHeightFieldShape(const HeightFieldColliderDesc& hf, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		if (hf.isTrigger) return false;

		if (PxRigidDynamic* dyn = actor->is<PxRigidDynamic>())
		{
			SceneReadLock rl(s->scene, s->enableSceneLocks);
			if (!HasRigidBodyFlag(dyn->getRigidBodyFlags(), PxRigidBodyFlag::eKINEMATIC))
				return false;
		}

		PxHeightField* heightField = s->GetOrCreateHeightField(hf);
		if (!heightField) return false;

		PxMeshGeometryFlags gflags{}; // 초기화 필수: 미초기화 시 랜덤 플래그로 인한 크래시 위험
		if (hf.doubleSidedQueries) gflags |= PxMeshGeometryFlag::eDOUBLE_SIDED;
		const PxHeightFieldGeometry geom(heightField, gflags, hf.heightScale, hf.rowScale, hf.colScale);
		if (!geom.isValid()) return false;

		return AddShapeCommon(geom, hf, localPos, localRot);
	}

	bool ClearShapes() override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);

		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			actor->detachShape(*sh);
		}
		return true;
	}

	uint32_t GetShapeCount() const override
	{
		if (!actor) return 0u;
		auto s = world.lock();
		if (!s || !s->scene) return 0u;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return actor->getNbShapes();
	}

	void* GetNativeActor() const override { return actor; }

protected:
	template<class PxGeomT, class DescT>
	bool AddShapeCommon(const PxGeomT& geom, const DescT& desc, const Vec3& localPos, const Quat& localRot)
	{
		auto s = world.lock();
		if (!s || !s->scene || !s->physics) return false;

		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		PxMaterial* mat = s->GetOrCreateMaterial(desc);
		PxShape* sh = s->physics->createShape(geom, *mat, true);
		if (!sh) return false;

		ApplyFilterToShape(*sh, desc);
		sh->userData = desc.userData;
		sh->setLocalPose(ToPxTransform(localPos, localRot));

		actor->attachShape(*sh);
		sh->release();

		return true;
	}

	PxRigidActor* actor = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
};


class PhysXRigidBody final : public IRigidBody
{
public:
	PhysXRigidBody(PxRigidDynamic* b, std::weak_ptr<PhysXWorld::Impl> w, const RigidBodyDesc& rb)
		: base(b, w), body(b), world(std::move(w)), cachedRb(rb)
	{
	}

	// ------------------------------------------------------------
	// IPhysicsActor (forwarded to shared actor wrapper)
	// ------------------------------------------------------------
	bool IsValid() const override { return base.IsValid(); }
	bool IsInWorld() const override { return base.IsInWorld(); }
	void SetInWorld(bool inWorld) override { base.SetInWorld(inWorld); }

	void SetTransform(const Vec3& p, const Quat& q) override { base.SetTransform(p, q); }
	Vec3 GetPosition() const override { return base.GetPosition(); }
	Quat GetRotation() const override { return base.GetRotation(); }

	void SetUserData(void* ptr) override { base.SetUserData(ptr); }
	void* GetUserData() const override { return base.GetUserData(); }

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		base.SetLayerMasks(layerBits, collideMask, queryMask);
	}

	void SetTrigger(bool isTrigger) override { base.SetTrigger(isTrigger); }

	void SetCollisionEnabled(bool enabled) override { base.SetCollisionEnabled(enabled); }
	bool IsCollisionEnabled() const override { return base.IsCollisionEnabled(); }
	void SetQueryEnabled(bool enabled) override { base.SetQueryEnabled(enabled); }
	bool IsQueryEnabled() const override { return base.IsQueryEnabled(); }

	void Destroy() override
	{
		base.Destroy();
		body = nullptr;
	}

	void SetMaterial(float staticFriction, float dynamicFriction, float restitution) override
	{
		base.SetMaterial(staticFriction, dynamicFriction, restitution);
	}

	bool AddBoxShape(const BoxColliderDesc& box, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddBoxShape(box, localPos, localRot);
	}

	bool AddSphereShape(const SphereColliderDesc& sphere, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddSphereShape(sphere, localPos, localRot);
	}

	bool AddCapsuleShape(const CapsuleColliderDesc& capsule, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddCapsuleShape(capsule, localPos, localRot);
	}

	bool AddTriangleMeshShape(const TriangleMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddTriangleMeshShape(mesh, localPos, localRot);
	}

	bool AddConvexMeshShape(const ConvexMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddConvexMeshShape(mesh, localPos, localRot);
	}

	bool AddHeightFieldShape(const HeightFieldColliderDesc& hf, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddHeightFieldShape(hf, localPos, localRot);
	}

	bool ClearShapes() override { return base.ClearShapes(); }
	uint32_t GetShapeCount() const override { return base.GetShapeCount(); }

	void* GetNativeActor() const override { return base.GetNativeActor(); }

	// ------------------------------------------------------------
	// IRigidBody
	// ------------------------------------------------------------
	void SetKinematicTarget(const Vec3& p, const Quat& q) override
	{
		if (!body) return;

		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		// 아직 scene에 add 안 된 프레임이면 kinematicTarget 금지 (PhysX assert 방지)
		if (body->getScene() == nullptr)
		{
			body->setGlobalPose(ToPxTransform(p, q));
			return;
		}
		// 키네마틱 아니면 글로벌 포즈로 처리
		if (!HasRigidBodyFlag(body->getRigidBodyFlags(), PxRigidBodyFlag::eKINEMATIC))
		{
			body->setGlobalPose(ToPxTransform(p, q));
			return;
		}
		body->setKinematicTarget(ToPxTransform(p, q));
	}

	bool IsKinematic() const override
	{
		if (!body) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return HasRigidBodyFlag(body->getRigidBodyFlags(), PxRigidBodyFlag::eKINEMATIC);
	}

	void SetKinematic(bool isKinematic) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, isKinematic);
		cachedRb.isKinematic = isKinematic;
		if (!isKinematic)
			RecomputeMass();
	}

	void SetGravityEnabled(bool enabled) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !enabled);
		cachedRb.gravityEnabled = enabled;
	}

	bool IsGravityEnabled() const override
	{
		if (!body) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return !HasActorFlag(body->getActorFlags(), PxActorFlag::eDISABLE_GRAVITY);
	}

	void SetLinearVelocity(const Vec3& v) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setLinearVelocity(ToPx(v));
	}

	Vec3 GetLinearVelocity() const override
	{
		if (!body) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(body->getLinearVelocity());
	}

	void SetAngularVelocity(const Vec3& v) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setAngularVelocity(ToPx(v));
	}

	Vec3 GetAngularVelocity() const override
	{
		if (!body) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(body->getAngularVelocity());
	}

	void AddForce(const Vec3& f) override { AddForceEx(f, ForceMode::Force, true); }
	void AddImpulse(const Vec3& impulse) override { AddForceEx(impulse, ForceMode::Impulse, true); }
	void AddTorque(const Vec3& t) override { AddTorqueEx(t, ForceMode::Force, true); }

	void AddForceEx(const Vec3& f, ForceMode mode, bool autowake) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->addForce(ToPx(f), ToPxForceMode(mode), autowake);
	}

	void AddTorqueEx(const Vec3& t, ForceMode mode, bool autowake) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->addTorque(ToPx(t), ToPxForceMode(mode), autowake);
	}

	void SetDamping(float linear, float angular) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setLinearDamping(linear);
		body->setAngularDamping(angular);
		cachedRb.linearDamping = linear;
		cachedRb.angularDamping = angular;
	}

	void SetMaxVelocities(float maxLinear, float maxAngular) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const float ml = (maxLinear > 0.0f) ? maxLinear : PX_MAX_F32;
		const float ma = (maxAngular > 0.0f) ? maxAngular : PX_MAX_F32;

		body->setMaxLinearVelocity(ml);
		body->setMaxAngularVelocity(ma);

		cachedRb.maxLinearVelocity = maxLinear;
		cachedRb.maxAngularVelocity = maxAngular;
	}

	void SetLockFlags(RigidBodyLockFlags flags) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidDynamicLockFlags(ToPxLockFlags(flags));
		cachedRb.lockFlags = flags;
	}

	void SetCCDEnabled(bool enabled, bool speculative) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, enabled);
		body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, speculative);
		cachedRb.enableCCD = enabled;
		cachedRb.enableSpeculativeCCD = speculative;
	}

	float GetMass() const override
	{
		if (!body) return 0.0f;
		auto s = world.lock();
		if (!s || !s->scene) return 0.0f;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return body->getMass();
	}

	void SetMass(float mass, bool updateInertia) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		if (updateInertia)
			PxRigidBodyExt::setMassAndUpdateInertia(*body, mass);
		else
			body->setMass(mass);

		cachedRb.massOverride = mass;
	}

	void RecomputeMass() override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		ApplyMass(*body, cachedRb);
	}

	void WakeUp() override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->wakeUp();
	}

	void PutToSleep() override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->putToSleep();
	}

	bool IsAwake() const override
	{
		if (!body) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return !body->isSleeping();
	}

	bool IsSleeping() const override
	{
		if (!body) return true;
		auto s = world.lock();
		if (!s || !s->scene) return true;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return body->isSleeping();
	}

	//  density + massOverride 같이 갱신
	void SetMassProperties(float density, float massOverride) override
	{
		cachedRb.density = density;
		cachedRb.massOverride = massOverride;
		RecomputeMass(); // cachedRb 기반으로 updateMassAndInertia or setMassAndUpdateInertia
	}

	// 런타임 튜닝용
	void SetSolverIterations(uint32_t posIts, uint32_t velIts) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setSolverIterationCounts(
			static_cast<PxU32>(std::max(1u, posIts)),
			static_cast<PxU32>(std::max(1u, velIts)));
		cachedRb.solverPositionIterations = posIts;
		cachedRb.solverVelocityIterations = velIts;
	}

	void SetSleepThreshold(float sleepThreshold) override
	{
		if (!body) return;
		if (sleepThreshold < 0.0f) return; // -1이면 "변경 안 함"으로 처리(기본값 복원은 재생성으로)
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setSleepThreshold(sleepThreshold);
		cachedRb.sleepThreshold = sleepThreshold;
	}

	void SetStabilizationThreshold(float stabilizationThreshold) override
	{
		if (!body) return;
		if (stabilizationThreshold < 0.0f) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setStabilizationThreshold(stabilizationThreshold);
		cachedRb.stabilizationThreshold = stabilizationThreshold;
	}

private:
	PhysXActor base;
	PxRigidDynamic* body = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
	RigidBodyDesc cachedRb{};
};

// ============================================================
//  Character Controller (CCT) wrapper
// ============================================================

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS

static inline PxControllerNonWalkableMode::Enum ToPx(CCTNonWalkableMode m)
{
	switch (m)
	{
	case CCTNonWalkableMode::PreventClimbing: return PxControllerNonWalkableMode::ePREVENT_CLIMBING;
	case CCTNonWalkableMode::PreventClimbingAndForceSliding: return PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
	default: return PxControllerNonWalkableMode::ePREVENT_CLIMBING;
	}
}

static inline PxCapsuleClimbingMode::Enum ToPx(CCTCapsuleClimbingMode m)
{
	switch (m)
	{
	case CCTCapsuleClimbingMode::Easy: return PxCapsuleClimbingMode::eEASY;
	case CCTCapsuleClimbingMode::Constrained: return PxCapsuleClimbingMode::eCONSTRAINED;
	default: return PxCapsuleClimbingMode::eCONSTRAINED;
	}
}

static inline CCTCollisionFlags FromPx(physx::PxControllerCollisionFlags f)
{
	CCTCollisionFlags out = CCTCollisionFlags::None;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_SIDES)) out |= CCTCollisionFlags::Sides;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_UP))    out |= CCTCollisionFlags::Up;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN))  out |= CCTCollisionFlags::Down;
	return out;
}

class MaskQueryCallbackIgnoreActor final : public PxQueryFilterCallback
{
public:
	MaskQueryCallbackIgnoreActor(uint32_t layerMaskIn, uint32_t queryMaskIn, bool includeTriggersIn, QueryHitMode hitModeIn, const PxRigidActor* ignoreIn)
		: layerMask(layerMaskIn), queryMask(queryMaskIn), includeTriggers(includeTriggersIn), hitMode(hitModeIn), ignore(ignoreIn)
	{
	}

	PxQueryHitType::Enum preFilter(
		const PxFilterData& /*filterData*/, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& /*queryFlags*/) override
	{
		if (!shape) return PxQueryHitType::eNONE;
		if (ignore && actor == ignore) return PxQueryHitType::eNONE;

		const PxShapeFlags sf = shape->getFlags();
		if (!HasShapeFlag(sf, PxShapeFlag::eSCENE_QUERY_SHAPE))
			return PxQueryHitType::eNONE;
		if (!includeTriggers && HasShapeFlag(sf, PxShapeFlag::eTRIGGER_SHAPE))
			return PxQueryHitType::eNONE;

		const PxFilterData fd = shape->getQueryFilterData();
		const uint32_t shapeLayerBits = fd.word0;
		const uint32_t shapeQueryMask = fd.word2;

		if ((shapeLayerBits & layerMask) == 0)
			return PxQueryHitType::eNONE;
		if ((shapeQueryMask & queryMask) == 0)
			return PxQueryHitType::eNONE;

		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

	PxQueryHitType::Enum postFilter(
		const PxFilterData& /*filterData*/, const PxQueryHit& /*hit*/,
		const PxShape* /*shape*/, const PxRigidActor* /*actor*/) override
	{
		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

private:
	uint32_t layerMask = 0xFFFFFFFFu;
	uint32_t queryMask = 0xFFFFFFFFu;
	bool includeTriggers = false;
	QueryHitMode hitMode = QueryHitMode::Block;
	const PxRigidActor* ignore = nullptr;
};

class PhysXCharacterController final : public ICharacterController
{
public:
	PhysXCharacterController(PxController* inController, PxRigidDynamic* inActor, const CharacterControllerDesc& desc, std::weak_ptr<PhysXWorld::Impl> w)
		: controller(inController), actor(inActor), world(std::move(w)), type(desc.type)
	{
		radius = (desc.type == CCTType::Capsule) ? desc.radius : std::max(desc.halfExtents.x, std::max(desc.halfExtents.y, desc.halfExtents.z));
		halfHeight = (desc.type == CCTType::Capsule) ? desc.halfHeight : desc.halfExtents.y;
		up = desc.upDirection;
		if (!physwrap::NormalizeSafe(up)) up = Vec3::UnitY;
		stepOffset = desc.stepOffset;
		contactOffset = desc.contactOffset;
		slopeLimitRadians = desc.slopeLimitRadians;
		filter.layerBits = desc.layerBits;
		filter.collideMask = desc.collideMask;
		filter.queryMask = desc.queryMask;
		filter.isTrigger = false;

	}

	~PhysXCharacterController() override
	{
		auto s = world.lock();
		if (s && controller)
			s->EnqueueControllerRelease(controller);
		controller = nullptr;
		actor = nullptr;
	}

	bool IsValid() const override { return controller != nullptr; }

	void Destroy() override
	{
		if (!controller) return;
		auto s = world.lock();
		if (s)
			s->EnqueueControllerRelease(controller);
		controller = nullptr;
		actor = nullptr;
	}

	void SetUserData(void* ptr) override
	{
		if (actor) actor->userData = ptr;
		else userDataFallback = ptr;
	}

	void* GetUserData() const override
	{
		return actor ? actor->userData : userDataFallback;
	}

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		filter.layerBits = layerBits;
		filter.collideMask = collideMask;
		filter.queryMask = queryMask;
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
			if (sh) ApplyFilterToShape(*sh, filter);
	}

	void SetPosition(const Vec3& centerPos) override
	{
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		controller->setPosition(ToPxExt(centerPos));
	}

	Vec3 GetPosition() const override
	{
		if (!controller) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPxExt(controller->getPosition());
	}

	void SetFootPosition(const Vec3& footPos) override
	{
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		SetPosition(footPos + up * footToCenter);
	}

	Vec3 GetFootPosition() const override
	{
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		return GetPosition() - up * footToCenter;
	}

	CCTCollisionFlags Move(
		const Vec3& displacement,
		float dt,
		uint32_t layerMask,
		uint32_t queryMask,
		bool hitTriggers,
		float minDistance) override
	{
		if (!controller) return CCTCollisionFlags::None;
		auto s = world.lock();
		if (!s || !s->scene) return CCTCollisionFlags::None;

		MaskQueryCallbackIgnoreActor cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block, actor);
		PxFilterData fd{}; // unused by our callback but required by PhysX API
		PxControllerFilters filters;
		filters.mFilterData = &fd;
		filters.mFilterCallback = &cb;
		filters.mCCTFilterCallback = nullptr;

		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		PxControllerCollisionFlags cf = controller->move(ToPx(displacement), minDistance, dt, filters);
		lastCollisionFlags = FromPx(cf);
		return lastCollisionFlags;
	}

	CharacterControllerState GetState(
		uint32_t groundLayerMask,
		uint32_t groundQueryMask,
		float groundProbeDistance,
		bool hitTriggers) const override
	{
		CharacterControllerState out{};
		out.collisionFlags = lastCollisionFlags;
		out.onGround = (lastCollisionFlags & CCTCollisionFlags::Down) != CCTCollisionFlags::None;

		if (!controller) return out;
		auto s = world.lock();
		if (!s || !s->scene) return out;

		SceneReadLock rl(s->scene, s->enableSceneLocks);
		const Vec3 center = FromPxExt(controller->getPosition());
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		const Vec3 foot = center - up * footToCenter;
		const Vec3 origin = foot + up * std::max(0.0f, groundProbeDistance);
		const Vec3 dir = -up;
		const float maxDist = std::max(0.0f, groundProbeDistance) + std::max(0.01f, contactOffset) + 0.01f;

		PxRaycastBuffer buf;
		PxQueryFilterData qfd;
		qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
		MaskQueryCallbackIgnoreActor cb(groundLayerMask, groundQueryMask, hitTriggers, QueryHitMode::Block, actor);

		const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL;
		const bool hit = s->scene->raycast(ToPx(origin), ToPx(dir), maxDist, buf, hitFlags, qfd, &cb);
		if (hit && buf.hasBlock)
		{
			out.groundNormal = FromPx(buf.block.normal);
			// Distance from foot to hit point along -up.
			out.groundDistance = std::max(0.0f, buf.block.distance - std::max(0.0f, groundProbeDistance));
			out.onGround = out.onGround || (out.groundDistance <= std::max(0.05f, contactOffset + 0.02f));
		}
		return out;
	}

	void SetStepOffset(float v) override
	{
		stepOffset = v;
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		controller->setStepOffset(v);
	}
	float GetStepOffset() const override { return stepOffset; }

	void SetSlopeLimit(float inSlopeLimitRadians) override
	{
		slopeLimitRadians = inSlopeLimitRadians;
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		const float c = std::cos(std::max(0.0f, std::min(inSlopeLimitRadians, 1.56079633f))); // clamp to < 89.4deg
		controller->setSlopeLimit(c);
	}
	float GetSlopeLimit() const override { return slopeLimitRadians; }

	void Resize(float inHalfHeight) override
	{
		if (!controller) return;
		if (type != CCTType::Capsule) return;
		PxCapsuleController* cap = static_cast<PxCapsuleController*>(controller);
		halfHeight = std::max(0.01f, inHalfHeight);
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		cap->setHeight(halfHeight * 2.0f);
	}

	void* GetNativeController() const override { return controller; }
	void* GetNativeActor() const override { return actor; }

private:
	PxController* controller = nullptr;
	PxRigidDynamic* actor = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
	CCTType type = CCTType::Capsule;

	FilterDesc filter{};
	Vec3 up = Vec3::UnitY;
	float radius = 0.5f;
	float halfHeight = 0.5f;
	float stepOffset = 0.3f;
	float contactOffset = 0.1f;
	float slopeLimitRadians = 0.785398163f;
	mutable void* userDataFallback = nullptr;

	mutable CCTCollisionFlags lastCollisionFlags = CCTCollisionFlags::None;
};

#endif // PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS

class PhysXJoint final : public IPhysicsJoint
{
public:
	PhysXJoint(PxJoint* j, std::weak_ptr<PhysXWorld::Impl> w)
		: joint(j), world(std::move(w))
	{
	}

	~PhysXJoint() override
	{
		auto s = world.lock();
		if (s && joint)
			s->EnqueueRelease(joint);
		joint = nullptr;
	}

	bool IsValid() const override { return joint != nullptr; }
	void* GetNativeJoint() const override { return joint; }

	void SetBreakForce(float force, float torque) override
	{
		if (!joint) return;
		joint->setBreakForce(force, torque);
	}

	void SetCollideConnected(bool enabled) override
	{
		if (!joint) return;
		joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, enabled);
	}

	void SetUserData(void* ptr) override
	{
		if (!joint) return;
		joint->userData = ptr;
	}

	void* GetUserData() const override
	{
		return joint ? joint->userData : nullptr;
	}

private:
	PxJoint* joint = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
};
