// PhysXWorld_Queries.cpp
#include "PhysXWorld_Internal.h"
#include "Core/Logger.h"

// ============================================================
//  Queries
// ============================================================

// 방향 벡터 정규화(안전장치)
static bool NormalizeDir(const Vec3& in, PxVec3& out)
{
	out = ToPx(in);
	const PxReal lenSq = out.magnitudeSquared();
	if (lenSq < 1e-12f) return false;
	out *= PxRecipSqrt(lenSq);
	return true;
}

static inline void FillRaycastHit(const PxRaycastHit& h, RaycastHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.faceIndex = HasHitFlag(h.flags, PxHitFlag::eFACE_INDEX) ? h.faceIndex : 0xFFFFFFFFu;
	if (HasHitFlag(h.flags, PxHitFlag::eUV))
	{
		out.baryUV = Vec2(h.u, h.v);
		const float w = 1.0f - h.u - h.v;
		out.barycentric = Vec3(w, h.u, h.v);
	}
	else
	{
		out.baryUV = Vec2::Zero;
		out.barycentric = Vec3::Zero;
	}
	out.actorHandle = h.actor ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.actor)) : 0ull;
	out.shapeHandle = h.shape ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.shape)) : 0ull;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, bool hitTriggers) const
{
	return RaycastEx(origin, dir, maxDist, outHit, layerMask, 0xFFFFFFFFu, hitTriggers);
}

bool PhysXWorld::RaycastEx(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHit = RaycastHit{};
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		uint32_t count = ++physxwrap_detail::g_blockedQueryCount;
		// 첫 1회 또는 100회마다 경고 (로그 폭탄 방지)
		if (count == 1 || count % 100 == 0)
		{
			ALICE_LOG_WARN("[PhysXWorld] RaycastEx called inside ContactModify callback! This will cause deadlock. (blocked count: %u)", count);
		}
		return false;
	}
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxRaycastBuffer buf;
	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool hit = impl->scene->raycast(ToPx(origin), unitDir, maxDist, buf, hitFlags, qfd, &cb);
	if (!hit || !buf.hasBlock) return false;

	FillRaycastHit(buf.block, outHit);
	return true;
}

uint32_t PhysXWorld::RaycastAll(const Vec3& origin, const Vec3& dir, float maxDist, std::vector<RaycastHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHits.clear();
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		uint32_t count = ++physxwrap_detail::g_blockedQueryCount;
		// 첫 1회 또는 100회마다 경고 (로그 폭탄 방지)
		if (count == 1 || count % 100 == 0)
		{
			ALICE_LOG_WARN("[PhysXWorld] RaycastAll called inside ContactModify callback! This will cause deadlock. (blocked count: %u)", count);
		}
		return 0;
	}
	if (!impl || !impl->scene || maxHits == 0) return 0;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return 0;

	std::vector<PxRaycastHit> hits(maxHits);
	PxRaycastBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool ok = impl->scene->raycast(ToPx(origin), unitDir, maxDist, buf, hitFlags, qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		RaycastHit rh;
		FillRaycastHit(buf.getTouch(i), rh);
		outHits.push_back(rh);
	}
	return static_cast<uint32_t>(outHits.size());
}

static inline void FillOverlapHit(const PxOverlapHit& h, OverlapHit& out)
{
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

uint32_t PhysXWorld::OverlapBox(const Vec3& center, const Quat& rot, const Vec3& halfExtents, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHits.clear();
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] OverlapBox called inside ContactModify callback! This will cause deadlock.");
		#endif
		return 0;
	}
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(center, rot);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapSphere(const Vec3& center, float radius, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHits.clear();
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] OverlapSphere called inside ContactModify callback! This will cause deadlock.");
		#endif
		return 0;
	}
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(center));

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapCapsule(const Vec3& center, const Quat& rot, float radius, float halfHeight, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits, bool alignYAxis) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHits.clear();
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] OverlapCapsule called inside ContactModify callback! This will cause deadlock.");
		#endif
		return 0;
	}
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align; // world rot * align (shape local)
	}

	const PxTransform pose = ToPxTransform(center, q);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

static inline void FillSweepHit(const PxSweepHit& h, SweepHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::SweepBox(const Vec3& origin, const Quat& rot, const Vec3& halfExtents, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHit = SweepHit{};
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] SweepBox called inside ContactModify callback! This will cause deadlock.");
		#endif
		return false;
	}
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(origin, rot);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepSphere(const Vec3& origin, float radius, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHit = SweepHit{};
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] SweepSphere called inside ContactModify callback! This will cause deadlock.");
		#endif
		return false;
	}
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(origin));

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepCapsule(const Vec3& origin, const Quat& rot, float radius, float halfHeight, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, bool alignYAxis) const
{
	// 출력 초기화 (ContactModify 가드보다 먼저) - 유령 히트 방지
	outHit = SweepHit{};
	
	// ContactModify 콜백 내에서 쿼리 호출 시 데드락 방지
	if (physxwrap_detail::IsInContactModifyCallback())
	{
		#ifdef _DEBUG
		ALICE_LOG_ERRORF("[PhysXWorld] SweepCapsule called inside ContactModify callback! This will cause deadlock.");
		#endif
		return false;
	}
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}
	const PxTransform pose = ToPxTransform(origin, q);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

// ============================================================
//  Extended Queries (Q) - ignore + multi-hit support
// ============================================================

bool PhysXWorld::RaycastQ(
	const Vec3& origin, const Vec3& dir, float maxDist,
	RaycastHit& outHit, const SceneQueryFilter& f) const
{
	// 출력 초기화 - 유령 히트 방지
	outHit = RaycastHit{};
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Block,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	PxRaycastBuffer buf;
	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->raycast(ToPx(origin), unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillRaycastHit(buf.block, outHit);
	return true;
}

uint32_t PhysXWorld::RaycastAllQ(
	const Vec3& origin, const Vec3& dir, float maxDist,
	std::vector<RaycastHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return 0;

	std::vector<PxRaycastHit> hits(maxHits);
	PxRaycastBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->raycast(ToPx(origin), unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		RaycastHit rh;
		FillRaycastHit(buf.getTouch(i), rh);
		outHits.push_back(rh);
	}

	std::sort(outHits.begin(), outHits.end(),
		[](const RaycastHit& a, const RaycastHit& b) { return a.distance < b.distance; });

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapBoxQ(
	const Vec3& center, const Quat& rot, const Vec3& halfExtents,
	std::vector<OverlapHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(center, rot);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapSphereQ(
	const Vec3& center, float radius,
	std::vector<OverlapHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(center));

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapCapsuleQ(
	const Vec3& center, const Quat& rot, float radius, float halfHeight,
	std::vector<OverlapHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits, bool alignYAxis) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}
	const PxTransform pose = ToPxTransform(center, q);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

bool PhysXWorld::SweepBoxQ(
	const Vec3& origin, const Quat& rot, const Vec3& halfExtents,
	const Vec3& dir, float maxDist, SweepHit& outHit, const SceneQueryFilter& f) const
{
	// 출력 초기화 - 유령 히트 방지
	outHit = SweepHit{};
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Block,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(origin, rot);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepSphereQ(
	const Vec3& origin, float radius,
	const Vec3& dir, float maxDist, SweepHit& outHit, const SceneQueryFilter& f) const
{
	// 출력 초기화 - 유령 히트 방지
	outHit = SweepHit{};
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Block,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(origin));

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepCapsuleQ(
	const Vec3& origin, const Quat& rot, float radius, float halfHeight,
	const Vec3& dir, float maxDist, SweepHit& outHit, const SceneQueryFilter& f, bool alignYAxis) const
{
	// 출력 초기화 - 유령 히트 방지
	outHit = SweepHit{};
	
	if (!impl || !impl->scene) return false;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Block,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}
	const PxTransform pose = ToPxTransform(origin, q);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

uint32_t PhysXWorld::SweepBoxAllQ(
	const Vec3& origin, const Quat& rot, const Vec3& halfExtents,
	const Vec3& dir, float maxDist,
	std::vector<SweepHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return 0;

	std::vector<PxSweepHit> hits(maxHits);
	PxSweepBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(origin, rot);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		SweepHit sh;
		FillSweepHit(buf.getTouch(i), sh);
		outHits.push_back(sh);
	}

	std::sort(outHits.begin(), outHits.end(),
		[](const SweepHit& a, const SweepHit& b) { return a.distance < b.distance; });

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::SweepSphereAllQ(
	const Vec3& origin, float radius,
	const Vec3& dir, float maxDist,
	std::vector<SweepHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return 0;

	std::vector<PxSweepHit> hits(maxHits);
	PxSweepBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(origin));

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		SweepHit sh;
		FillSweepHit(buf.getTouch(i), sh);
		outHits.push_back(sh);
	}

	std::sort(outHits.begin(), outHits.end(),
		[](const SweepHit& a, const SweepHit& b) { return a.distance < b.distance; });

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::SweepCapsuleAllQ(
	const Vec3& origin, const Quat& rot, float radius, float halfHeight,
	const Vec3& dir, float maxDist,
	std::vector<SweepHit>& outHits, const SceneQueryFilter& f, uint32_t maxHits, bool alignYAxis) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	PxVec3 unitDir;
	if (!NormalizeDir(dir, unitDir)) return 0;

	std::vector<PxSweepHit> hits(maxHits);
	PxSweepBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(
		f.layerMask, f.queryMask, f.hitTriggers, QueryHitMode::Touch,
		reinterpret_cast<PxRigidActor*>(f.ignoreNativeActor),
		reinterpret_cast<PxShape*>(f.ignoreNativeShape),
		f.ignoreUserData);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}
	const PxTransform pose = ToPxTransform(origin, q);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, unitDir, maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		SweepHit sh;
		FillSweepHit(buf.getTouch(i), sh);
		outHits.push_back(sh);
	}

	std::sort(outHits.begin(), outHits.end(),
		[](const SweepHit& a, const SweepHit& b) { return a.distance < b.distance; });

	return static_cast<uint32_t>(outHits.size());
}

// ============================================================
//  Penetration (MTD) 헬퍼
// ============================================================

bool PhysXWorld::ComputePenetrationBoxVsShape(
	const Vec3& center, const Quat& rot, const Vec3& halfExtents,
	void* otherNativeActor, void* otherNativeShape,
	Vec3& outDir, float& outDepth) const
{
	if (!otherNativeActor || !otherNativeShape) return false;
	if (!impl || !impl->scene) return false;

	PxRigidActor* actor = reinterpret_cast<PxRigidActor*>(otherNativeActor);
	PxShape* shape = reinterpret_cast<PxShape*>(otherNativeShape);
	if (!actor || !shape) return false;

	const PxBoxGeometry geom0(ToPx(halfExtents));
	const PxTransform pose0 = ToPxTransform(center, rot);

	PxTransform pose1;
	PxGeometryHolder gh;
	{
		SceneReadLock rl(impl->scene, impl->enableSceneLocks);
		pose1 = actor->getGlobalPose() * shape->getLocalPose();
		gh = shape->getGeometry();
	}

	PxVec3 dir;
	PxF32 depth = 0.0f;

	const bool ok = PxGeometryQuery::computePenetration(
		dir, depth,
		geom0, pose0,
		gh.any(), pose1);

	if (!ok) return false;

	outDir = FromPx(dir);
	outDepth = depth;
	return true;
}

bool PhysXWorld::ComputePenetrationSphereVsShape(
	const Vec3& center, float radius,
	void* otherNativeActor, void* otherNativeShape,
	Vec3& outDir, float& outDepth) const
{
	if (!otherNativeActor || !otherNativeShape) return false;
	if (!impl || !impl->scene) return false;

	PxRigidActor* actor = reinterpret_cast<PxRigidActor*>(otherNativeActor);
	PxShape* shape = reinterpret_cast<PxShape*>(otherNativeShape);
	if (!actor || !shape) return false;

	const PxSphereGeometry geom0(radius);
	const PxTransform pose0(ToPx(center));

	PxTransform pose1;
	PxGeometryHolder gh;
	{
		SceneReadLock rl(impl->scene, impl->enableSceneLocks);
		pose1 = actor->getGlobalPose() * shape->getLocalPose();
		gh = shape->getGeometry();
	}

	PxVec3 dir;
	PxF32 depth = 0.0f;

	const bool ok = PxGeometryQuery::computePenetration(
		dir, depth,
		geom0, pose0,
		gh.any(), pose1);

	if (!ok) return false;

	outDir = FromPx(dir);
	outDepth = depth;
	return true;
}

bool PhysXWorld::ComputePenetrationCapsuleVsShape(
	const Vec3& center, const Quat& rot,
	float radius, float halfHeight, bool alignYAxis,
	void* otherNativeActor, void* otherNativeShape,
	Vec3& outDir, float& outDepth) const
{
	if (!otherNativeActor || !otherNativeShape) return false;
	if (!impl || !impl->scene) return false;

	PxRigidActor* actor = reinterpret_cast<PxRigidActor*>(otherNativeActor);
	PxShape* shape = reinterpret_cast<PxShape*>(otherNativeShape);
	if (!actor || !shape) return false;

	const PxCapsuleGeometry geom0(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}
	const PxTransform pose0 = ToPxTransform(center, q);

	PxTransform pose1;
	PxGeometryHolder gh;
	{
		SceneReadLock rl(impl->scene, impl->enableSceneLocks);
		pose1 = actor->getGlobalPose() * shape->getLocalPose();
		gh = shape->getGeometry();
	}

	PxVec3 dir;
	PxF32 depth = 0.0f;

	const bool ok = PxGeometryQuery::computePenetration(
		dir, depth,
		geom0, pose0,
		gh.any(), pose1);

	if (!ok) return false;

	outDir = FromPx(dir);
	outDepth = depth;
	return true;
}
