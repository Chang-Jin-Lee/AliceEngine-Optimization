// PhysXWorld_Actors.cpp
#include "PhysXWorld_Internal.h"

// ============================================================
//  Local helper functions
// ============================================================

namespace
{
	void ApplyRbDesc(PxRigidDynamic& body, const RigidBodyDesc& rb)
	{
		body.userData = rb.userData;

		body.setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !rb.gravityEnabled);

		body.setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, rb.isKinematic);
		body.setLinearDamping(rb.linearDamping);
		body.setAngularDamping(rb.angularDamping);

		if (rb.maxLinearVelocity > 0.0f)  body.setMaxLinearVelocity(rb.maxLinearVelocity);
		if (rb.maxAngularVelocity > 0.0f) body.setMaxAngularVelocity(rb.maxAngularVelocity);

		body.setSolverIterationCounts(
			static_cast<PxU32>(std::max(1u, rb.solverPositionIterations)),
			static_cast<PxU32>(std::max(1u, rb.solverVelocityIterations)));

		if (rb.sleepThreshold >= 0.0f) body.setSleepThreshold(rb.sleepThreshold);
		if (rb.stabilizationThreshold >= 0.0f) body.setStabilizationThreshold(rb.stabilizationThreshold);

		body.setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, rb.enableCCD);
		body.setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, rb.enableSpeculativeCCD);

		body.setRigidDynamicLockFlags(ToPxLockFlags(rb.lockFlags));

		if (!rb.startAwake)
			body.putToSleep();
	}
}

// ============================================================
//  PhysXWorld - Bodies / Actors / Meshes / CCT
// ============================================================

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicEmpty(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb)
{
	if (!impl || !impl->physics) return {};

	PxRigidDynamic* body = impl->physics->createRigidDynamic(ToPxTransform(pos, rot));
	if (!body) return {};

	ApplyRbDesc(*body, rb);

	body->setMass(1.0f);
	body->setMassSpaceInertiaTensor(PxVec3(1.0f, 1.0f, 1.0f));

	impl->EnqueueAdd(body);
	return std::make_unique<PhysXRigidBody>(body, impl, rb);
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticEmpty(const Vec3& pos, const Quat& rot, void* userData)
{
	if (!impl || !impl->physics) return {};

	PxRigidStatic* actor = impl->physics->createRigidStatic(ToPxTransform(pos, rot));
	if (!actor) return {};
	actor->userData = userData;

	impl->EnqueueAdd(actor);
	return std::make_unique<PhysXActor>(actor, impl);
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicBox(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const BoxColliderDesc& box)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddBoxShape(box);
	body->RecomputeMass();
	return body;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicSphere(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const SphereColliderDesc& sphere)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddSphereShape(sphere);
	body->RecomputeMass();
	return body;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicCapsule(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const CapsuleColliderDesc& capsule)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddCapsuleShape(capsule);
	body->RecomputeMass();
	return body;
}

void PhysXWorld::CreateStaticPlane(float staticFriction, float dynamicFriction, float restitution, const FilterDesc& filter)
{
	// 월드가 소유하는 내부 액터로 보관 (즉시 파괴 방지)
	if (auto actor = CreateStaticPlaneActor(staticFriction, dynamicFriction, restitution, filter))
		m_internalActors.push_back(std::move(actor));
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticPlaneActor(float staticFriction, float dynamicFriction, float restitution, const FilterDesc& filter)
{
	if (!impl || !impl->physics) return {};

	MaterialDesc md;
	md.staticFriction = staticFriction;
	md.dynamicFriction = dynamicFriction;
	md.restitution = restitution;
	PxMaterial* mat = impl->GetOrCreateMaterial(md);

	PxRigidStatic* plane = PxCreatePlane(*impl->physics, PxPlane(0, 1, 0, 0), *mat);
	if (!plane) return {};
	plane->userData = filter.userData;

	// Apply filter to its only shape
	PxShape* sh = nullptr;
	if (plane->getNbShapes() == 1)
	{
		plane->getShapes(&sh, 1);
		if (sh) ApplyFilterToShape(*sh, filter);
	}

	impl->EnqueueAdd(plane);
	return std::make_unique<PhysXActor>(plane, impl);
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticBox(const Vec3& pos, const Quat& rot, const BoxColliderDesc& box)
{
	auto a = CreateStaticEmpty(pos, rot, box.userData);
	if (!a) return {};
	a->AddBoxShape(box);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticSphere(const Vec3& pos, const Quat& rot, const SphereColliderDesc& sphere)
{
	auto a = CreateStaticEmpty(pos, rot, sphere.userData);
	if (!a) return {};
	a->AddSphereShape(sphere);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticCapsule(const Vec3& pos, const Quat& rot, const CapsuleColliderDesc& capsule)
{
	auto a = CreateStaticEmpty(pos, rot, capsule.userData);
	if (!a) return {};
	a->AddCapsuleShape(capsule);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticTriangleMesh(const Vec3& pos, const Quat& rot, const TriangleMeshColliderDesc& mesh)
{
	auto a = CreateStaticEmpty(pos, rot, mesh.userData);
	if (!a) return {};
	if (!a->AddTriangleMeshShape(mesh))
		return {};
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticConvexMesh(const Vec3& pos, const Quat& rot, const ConvexMeshColliderDesc& mesh)
{
	auto a = CreateStaticEmpty(pos, rot, mesh.userData);
	if (!a) return {};
	if (!a->AddConvexMeshShape(mesh))
		return {};
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticHeightField(const Vec3& pos, const Quat& rot, const HeightFieldColliderDesc& heightField)
{
	auto a = CreateStaticEmpty(pos, rot, heightField.userData);
	if (!a) return {};
	if (!a->AddHeightFieldShape(heightField))
		return {};
	return a;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicConvexMesh(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const ConvexMeshColliderDesc& mesh)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	if (!body->AddConvexMeshShape(mesh))
		return {};
	body->RecomputeMass();
	return body;
}

bool PhysXWorld::SupportsCharacterControllers() const
{
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	return impl && impl->controllerMgr != nullptr;
#else
	return false;
#endif
}

std::unique_ptr<ICharacterController> PhysXWorld::CreateCharacterController(const CharacterControllerDesc& desc)
{
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	if (!impl || !impl->scene || !impl->controllerMgr) return {};

	if (desc.type == CCTType::Capsule)
	{
		if (desc.radius <= 0.0f || desc.halfHeight <= 0.0f) return {};
	}
		else
	{
		if (desc.halfExtents.x <= 0.0f || desc.halfExtents.y <= 0.0f || desc.halfExtents.z <= 0.0f) return {};
	}

	Vec3 up = desc.upDirection;
	if (!physwrap::NormalizeSafe(up)) up = Vec3::UnitY;

	PxMaterial* mat = impl->GetOrCreateMaterial(desc);
	if (!mat) return {};

	const float clampedSlope = std::max(0.0f, std::min(desc.slopeLimitRadians, 1.56079633f));
	const float slopeCos = std::cos(clampedSlope);

	PxController* controller = nullptr;
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		if (desc.type == CCTType::Capsule)
		{
			PxCapsuleControllerDesc cd;
			cd.radius = desc.radius;
			cd.height = desc.halfHeight * 2.0f; // cylinder height
			cd.climbingMode = ToPx(desc.climbingMode);

			cd.material = mat;
			cd.upDirection = ToPx(up);
			cd.stepOffset = std::max(0.0f, desc.stepOffset);
			cd.contactOffset = std::max(0.001f, desc.contactOffset);
			cd.slopeLimit = slopeCos;
			cd.nonWalkableMode = ToPx(desc.nonWalkableMode);
			cd.density = std::max(0.0f, desc.density);

			const float footToCenter = desc.halfHeight + desc.radius;
			const Vec3 center = desc.footPosition + up * footToCenter;
			cd.position = ToPxExt(center);

			if (!cd.isValid()) return {};
			controller = impl->controllerMgr->createController(cd);
		}
		else
		{
			PxBoxControllerDesc cd;
			cd.halfHeight = desc.halfExtents.y;
			cd.halfSideExtent = desc.halfExtents.x;
			cd.halfForwardExtent = desc.halfExtents.z;

			cd.material = mat;
			cd.upDirection = ToPx(up);
			cd.stepOffset = std::max(0.0f, desc.stepOffset);
			cd.contactOffset = std::max(0.001f, desc.contactOffset);
			cd.slopeLimit = slopeCos;
			cd.nonWalkableMode = ToPx(desc.nonWalkableMode);
			cd.density = std::max(0.0f, desc.density);

			const float footToCenter = desc.halfExtents.y;
			const Vec3 center = desc.footPosition + up * footToCenter;
			cd.position = ToPxExt(center);

			if (!cd.isValid()) return {};
			controller = impl->controllerMgr->createController(cd);
		}
	}

	if (!controller) return {};

	PxRigidDynamic* actor = controller->getActor();
	if (actor)
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		actor->userData = desc.userData;
		actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
		actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

		// Apply filters to the underlying kinematic actor shapes.
		FilterDesc f = desc;
		f.isTrigger = false;
		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			ApplyFilterToShape(*sh, f);
			sh->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, desc.enableQueries);
			sh->setFlag(PxShapeFlag::eVISUALIZATION, true); 
		}
	}

	return std::make_unique<PhysXCharacterController>(controller, actor, desc, impl);
#else
	(void)desc;
	return {};
#endif
}

bool PhysXWorld::SupportsMeshCooking() const
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return ctx.IsCookingAvailable();
#else
	return false;
#endif
}

void PhysXWorld::ClearMeshCaches()
{
	if (!impl) return;
	impl->ClearMeshCachesInternal();
}

