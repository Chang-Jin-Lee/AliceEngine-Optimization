// PhysXWorld_Joints.cpp
#include "PhysXWorld_Internal.h"

// ============================================================
//  Events
// ============================================================

void PhysXWorld::DrainActiveTransforms(std::vector<ActiveTransform>& outTransforms)
{
	outTransforms.clear();
	if (!impl) return;

	std::scoped_lock lock(impl->activeMtx);
	outTransforms.swap(impl->activeTransforms);
}

void PhysXWorld::DrainEvents(std::vector<PhysicsEvent>& outEvents)
{
	outEvents.clear();
	if (!impl) return;

	std::scoped_lock lock(impl->eventMtx);
	outEvents.swap(impl->events);
}

void PhysXWorld::SetContactModifyCallback(ContactModifyCallback cb, void* userContext)
{
	if (!impl) return;
	std::scoped_lock lock(impl->contactModifyMtx);
	impl->contactModifyCb = cb;
	impl->contactModifyUser = userContext;
}

// ============================================================
//  Joints (minimal)
// ============================================================

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateFixedJoint(const IPhysicsActor& a, const IPhysicsActor& b, const FixedJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxFixedJoint* j = PxFixedJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	j->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, desc.collideConnected);

	const float breakF = (desc.breakForce > 0.0f) ? desc.breakForce : PX_MAX_F32;
	const float breakT = (desc.breakTorque > 0.0f) ? desc.breakTorque : PX_MAX_F32;
	j->setBreakForce(breakF, breakT);

	j->userData = desc.userData;

	return std::make_unique<PhysXJoint>(j, impl);
}


// ============================================================
//  Joints (extended)
// ============================================================

static PxD6Motion::Enum ToPxD6Motion(D6Motion m)
{
	switch (m)
	{
	case D6Motion::Locked:  return PxD6Motion::eLOCKED;
	case D6Motion::Limited: return PxD6Motion::eLIMITED;
	case D6Motion::Free:    return PxD6Motion::eFREE;
	}
	return PxD6Motion::eLOCKED;
}

static void ApplyLimitParams(PxJointLimitParameters& p, float stiffness, float damping, float restitution, float bounceThreshold)
{
	p.stiffness = stiffness;
	p.damping = damping;
	if (restitution >= 0.0f) p.restitution = restitution;
	if (bounceThreshold >= 0.0f) p.bounceThreshold = bounceThreshold;
}

static PxJointLinearLimitPair MakeLinearLimitPair(const PxTolerancesScale& scale, float lower, float upper,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	if (lower > upper) {
		const float t = lower; lower = upper; upper = t;
	}
	PxJointLinearLimitPair lim(scale, lower, upper);
	ApplyLimitParams(lim, stiffness, damping, restitution, bounceThreshold);
	return lim;
}

static PxJointAngularLimitPair MakeAngularLimitPair(float lower, float upper,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	if (lower > upper) {
		const float t = lower; lower = upper; upper = t;
	}
	PxJointAngularLimitPair lim(lower, upper, PxSpring(stiffness, damping));
	if (restitution >= 0.0f) lim.restitution = restitution;
	if (bounceThreshold >= 0.0f) lim.bounceThreshold = bounceThreshold;
	return lim;
}

static PxJointLimitCone MakeLimitCone(float yAngle, float zAngle,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	PxJointLimitCone lim(yAngle, zAngle, PxSpring(stiffness, damping));
	if (restitution >= 0.0f) lim.restitution = restitution;
	if (bounceThreshold >= 0.0f) lim.bounceThreshold = bounceThreshold;
	return lim;
}

static PxD6JointDrive MakeD6Drive(const D6JointDriveDesc& d)
{
	const float f = (d.forceLimit > 0.0f) ? d.forceLimit : PX_MAX_F32;
	return PxD6JointDrive(d.stiffness, d.damping, f, d.isAcceleration);
}

static void ApplyBreakAndFlags(PxJoint& j, bool collideConnected, float breakForce, float breakTorque, void* userData)
{
	j.setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, collideConnected);

	const float bf = (breakForce > 0.0f) ? breakForce : PX_MAX_F32;
	const float bt = (breakTorque > 0.0f) ? breakTorque : PX_MAX_F32;
	j.setBreakForce(bf, bt);

	j.userData = userData;
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateRevoluteJoint(const IPhysicsActor& a, const IPhysicsActor& b, const RevoluteJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxRevoluteJoint* j = PxRevoluteJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	// Limit
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		PxJointAngularLimitPair lim = MakeAngularLimitPair(
			desc.lowerLimit, desc.upperLimit,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimit(lim);
	}

	// Drive (motor)
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, desc.enableDrive);
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_FREESPIN, desc.driveFreeSpin);

	if (desc.enableDrive)
	{
		j->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, desc.driveLimitsAreForces);
		j->setDriveVelocity(desc.driveVelocity, true);
		j->setDriveForceLimit((desc.driveForceLimit > 0.0f) ? desc.driveForceLimit : PX_MAX_F32);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreatePrismaticJoint(const IPhysicsActor& a, const IPhysicsActor& b, const PrismaticJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxPrismaticJoint* j = PxPrismaticJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	j->setPrismaticJointFlag(PxPrismaticJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		const PxTolerancesScale scale = impl->physics->getTolerancesScale();
		PxJointLinearLimitPair lim = MakeLinearLimitPair(
			scale,
			desc.lowerLimit, desc.upperLimit,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimit(lim);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateDistanceJoint(const IPhysicsActor& a, const IPhysicsActor& b, const DistanceJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxDistanceJoint* j = PxDistanceJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	float minD = desc.minDistance;
	float maxD = desc.maxDistance;
	if (minD > maxD) {
		const float t = minD; minD = maxD; maxD = t;
	}

	j->setMinDistance(minD);
	j->setMaxDistance(maxD);
	j->setTolerance(desc.tolerance);

	j->setDistanceJointFlag(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, desc.enableMinDistance);
	j->setDistanceJointFlag(PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, desc.enableMaxDistance);

	j->setDistanceJointFlag(PxDistanceJointFlag::eSPRING_ENABLED, desc.enableSpring);
	if (desc.enableSpring)
	{
		j->setStiffness(desc.stiffness);
		j->setDamping(desc.damping);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateSphericalJoint(const IPhysicsActor& a, const IPhysicsActor& b, const SphericalJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxSphericalJoint* j = PxSphericalJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	j->setSphericalJointFlag(PxSphericalJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		PxJointLimitCone lim = MakeLimitCone(
			desc.yLimitAngle, desc.zLimitAngle,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimitCone(lim);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateD6Joint(const IPhysicsActor& a, const IPhysicsActor& b, const D6JointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxD6Joint* j = PxD6JointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	// Motions
	j->setMotion(PxD6Axis::eX, ToPxD6Motion(desc.motionX));
	j->setMotion(PxD6Axis::eY, ToPxD6Motion(desc.motionY));
	j->setMotion(PxD6Axis::eZ, ToPxD6Motion(desc.motionZ));
	j->setMotion(PxD6Axis::eTWIST, ToPxD6Motion(desc.motionTwist));
	j->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(desc.motionSwing1));
	j->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(desc.motionSwing2));

	// Linear limits (per axis)
	const PxTolerancesScale scale = impl->physics->getTolerancesScale();
	if (desc.motionX == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eX, MakeLinearLimitPair(scale,
			desc.linearLimitX.lower, desc.linearLimitX.upper,
			desc.linearLimitX.stiffness, desc.linearLimitX.damping,
			desc.linearLimitX.restitution, desc.linearLimitX.bounceThreshold));
	if (desc.motionY == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eY, MakeLinearLimitPair(scale,
			desc.linearLimitY.lower, desc.linearLimitY.upper,
			desc.linearLimitY.stiffness, desc.linearLimitY.damping,
			desc.linearLimitY.restitution, desc.linearLimitY.bounceThreshold));
	if (desc.motionZ == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eZ, MakeLinearLimitPair(scale,
			desc.linearLimitZ.lower, desc.linearLimitZ.upper,
			desc.linearLimitZ.stiffness, desc.linearLimitZ.damping,
			desc.linearLimitZ.restitution, desc.linearLimitZ.bounceThreshold));

	// Angular limits
	if (desc.motionTwist == D6Motion::Limited)
	{
		PxJointAngularLimitPair lim = MakeAngularLimitPair(
			desc.twistLimit.lower, desc.twistLimit.upper,
			desc.twistLimit.stiffness, desc.twistLimit.damping,
			desc.twistLimit.restitution, desc.twistLimit.bounceThreshold);
		j->setTwistLimit(lim);
	}

	if (desc.motionSwing1 == D6Motion::Limited || desc.motionSwing2 == D6Motion::Limited)
	{
		PxJointLimitCone lim = MakeLimitCone(
			desc.swingLimit.yAngle, desc.swingLimit.zAngle,
			desc.swingLimit.stiffness, desc.swingLimit.damping,
			desc.swingLimit.restitution, desc.swingLimit.bounceThreshold);
		j->setSwingLimit(lim);
	}

	// Drives
	j->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, desc.driveLimitsAreForces);
	j->setDrive(PxD6Drive::eX, MakeD6Drive(desc.driveX));
	j->setDrive(PxD6Drive::eY, MakeD6Drive(desc.driveY));
	j->setDrive(PxD6Drive::eZ, MakeD6Drive(desc.driveZ));
	j->setDrive(PxD6Drive::eSWING, MakeD6Drive(desc.driveSwing));
	j->setDrive(PxD6Drive::eTWIST, MakeD6Drive(desc.driveTwist));
	j->setDrive(PxD6Drive::eSLERP, MakeD6Drive(desc.driveSlerp));

	// Drive targets
	j->setDrivePosition(ToPxTransform(desc.drivePose.position, desc.drivePose.rotation), true);
	j->setDriveVelocity(ToPx(desc.driveLinearVelocity), ToPx(desc.driveAngularVelocity), true);

	return std::make_unique<PhysXJoint>(j, impl);
}
