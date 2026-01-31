// PhysXWorld_Core.cpp
#include "PhysXWorld_Internal.h"

// ============================================================
//  PhysXWorld
// ============================================================

PhysXWorld::PhysXWorld(PhysXContext& inCtx, const Desc& desc)
	: ctx(inCtx)
{
	impl = std::shared_ptr<Impl>(new Impl(inCtx, desc));
	impl->eventCb.owner = impl;
}

PhysXWorld::~PhysXWorld() = default;

void PhysXWorld::Flush()
{
	if (!impl) return;
	impl->FlushPending(true);
}

void PhysXWorld::Step(float fixedDt)
{
	if (!impl || !impl->scene) return;

	// Apply pending adds/releases before stepping.
	impl->FlushPending(true);

	// Clear last step's active pose list.
	{
		std::scoped_lock lock(impl->activeMtx);
		impl->activeTransforms.clear();
	}

	// Keep the write lock held across simulate() -> fetchResults() so that no other
	// thread can run queries/add/remove/release while the scene is simulating.
	// This prevents race conditions during the simulation step.
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		impl->scene->simulate(fixedDt);
		impl->scene->fetchResults(true);
	}

	// Flush any releases queued during callbacks.
	impl->FlushPending(true);
}

void PhysXWorld::SetGravity(const Vec3& g)
{
	if (!impl || !impl->scene) return;
	SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
	impl->scene->setGravity(ToPx(g));
}

Vec3 PhysXWorld::GetGravity() const
{
	if (!impl || !impl->scene) return Vec3(0, -9.81f, 0);
	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	return FromPx(impl->scene->getGravity());
}

