// IPhysicsWorld.h
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <directxtk/SimpleMath.h>

// Engine-facing math aliases
using Vec3 = DirectX::SimpleMath::Vector3;
using Vec2 = DirectX::SimpleMath::Vector2;
using Quat = DirectX::SimpleMath::Quaternion;

// ============================================================
//  Descriptors
// ============================================================

enum class ForceMode : uint8_t
{
	Force,          // PxForceMode::eFORCE
	Impulse,        // PxForceMode::eIMPULSE
	VelocityChange, // PxForceMode::eVELOCITY_CHANGE
	Acceleration,   // PxForceMode::eACCELERATION
};

enum class RigidBodyLockFlags : uint16_t
{
	None = 0,

	LockLinearX = 1 << 0,
	LockLinearY = 1 << 1,
	LockLinearZ = 1 << 2,

	LockAngularX = 1 << 3,
	LockAngularY = 1 << 4,
	LockAngularZ = 1 << 5,
};

inline constexpr RigidBodyLockFlags operator|(RigidBodyLockFlags a, RigidBodyLockFlags b)
{
	return static_cast<RigidBodyLockFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
inline constexpr RigidBodyLockFlags operator&(RigidBodyLockFlags a, RigidBodyLockFlags b)
{
	return static_cast<RigidBodyLockFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}
inline constexpr RigidBodyLockFlags& operator|=(RigidBodyLockFlags& a, RigidBodyLockFlags b)
{
	a = (a | b);
	return a;
}

struct RigidBodyDesc
{
	// Default: compute mass/inertia from density
	float density = 1.0f;

	// If > 0, overrides mass (and updates inertia)
	float massOverride = 0.0f;

	// Starts as kinematic if true
	bool isKinematic = false;

	// Disable gravity if false
	bool gravityEnabled = true;

	// Starts awake if true (dynamic only)
	bool startAwake = true;

	// Continuous collision detection (CCD)
	// NOTE: For swept CCD (enableCCD), the scene must enable CCD as well.
	bool enableCCD = false;
	bool enableSpeculativeCCD = false;

	// Lock axes (dynamic only)
	RigidBodyLockFlags lockFlags = RigidBodyLockFlags::None;

	// Damping (dynamic only)
	float linearDamping = 0.0f;
	float angularDamping = 0.05f;

	// Velocity clamps (dynamic only)
	// <= 0 means "use backend default".
	float maxLinearVelocity = 0.0f;
	float maxAngularVelocity = 0.0f;

	// Solver iterations (dynamic only). PhysX defaults are typically 4/1.
	uint32_t solverPositionIterations = 4;
	uint32_t solverVelocityIterations = 1;

	// Sleep/stabilization thresholds (dynamic only)
	// < 0 means "use backend default".
	float sleepThreshold = -1.0f;
	float stabilizationThreshold = -1.0f;

	// Optional back-pointer to your engine object
	void* userData = nullptr;
};

// Common layer/filtering rules:
// - layerBits: which layer(s) this shape belongs to (bitmask)
// - collideMask: which layers this shape collides with (bitmask)
// - queryMask: which query categories can see this shape (bitmask)
//
// Typical convention:
//   layerBits   = (1u << layerIndex)
//   collideMask = 0xFFFFFFFFu (or a mask of allowed layers)
//   queryMask   = 0xFFFFFFFFu (or a mask of query categories)
struct FilterDesc
{
	uint32_t layerBits = 1u << 0;
	uint32_t collideMask = 0xFFFFFFFFu;
	uint32_t queryMask = 0xFFFFFFFFu;

	bool isTrigger = false;

	// Optional back-pointer for convenience.
	// - For static creation helpers (CreateStaticBox/Sphere/...), this is used as the created actor's userData.
	// - For compound bodies, you may also treat this as per-shape user data if your backend assigns it to PxShape::userData.
	void* userData = nullptr;
};

struct MaterialDesc
{
	float staticFriction = 0.5f;
	float dynamicFriction = 0.5f;
	float restitution = 0.0f;
};

// ------------------------------
// Collider parameters
// ------------------------------
struct BoxColliderDesc : public FilterDesc, public MaterialDesc
{
	Vec3 halfExtents = { 0.5f, 0.5f, 0.5f };
};

struct SphereColliderDesc : public FilterDesc, public MaterialDesc
{
	float radius = 0.5f;
};

struct CapsuleColliderDesc : public FilterDesc, public MaterialDesc
{
	// NOTE: PhysX capsule geometry is defined by radius and halfHeight (excluding hemispheres)
	float radius = 0.5f;
	float halfHeight = 0.5f;

	// Engine convention is usually "capsule along +Y".
	// PhysX capsule is along +X, so wrapper can rotate shape to align with +Y.
	bool alignYAxis = true;
};

// ------------------------------
// HeightField (Terrain)
// ------------------------------
// PhysX HeightField for terrain collision.
// HeightField is a grid of height samples, efficient for large terrains.
struct HeightFieldColliderDesc : public FilterDesc, public MaterialDesc
{
	// Height samples (row-major: samples[i * numCols + j])
	// Each sample is a height value in world units (meters).
	// PhysX internally quantizes these to int16 using heightScale.
	const float* heightSamples = nullptr;
	uint32_t numRows = 0;  // Grid rows (typically Y-axis in PhysX), must be >= 2
	uint32_t numCols = 0;  // Grid columns (typically X-axis in PhysX), must be >= 2

	// Height quantization scale: "world height per int16 step"
	// PhysX formula: worldHeight = PxI16(height) * heightScale
	// Example: heightScale = 0.01f means 1 int16 unit = 1cm in world space
	// Must be > 0.0f
	float heightScale = 0.01f;

	// Horizontal scale (size of each grid cell in world units)
	// This determines the physical size of the terrain.
	// If rowScale/colScale are equal, terrain cells are square.
	// Both must be > 0.0f
	float rowScale = 1.0f;  // Size along rows (Y-axis spacing) in world units
	float colScale = 1.0f;  // Size along columns (X-axis spacing) in world units

	// Thickness for collision detection below the surface
	// NOTE: PhysX 5.x PxHeightFieldDesc does not support thickness member.
	// This field is kept for API compatibility but is not used by PhysX.
	// (Deprecated in PhysX 3.x, removed in PhysX 5.x)
	float thickness = 0.0f;  // Not used in PhysX 5.x

	// 지형 쿼리를 양면으로 처리할지
	bool doubleSidedQueries = false;
};

// ------------------------------
// Mesh cooking inputs
// ------------------------------
// Notes (PhysX 5.x):
// - Dynamic triangle meshes are generally not supported for simulation. Use convex meshes for dynamic bodies.
// - Triangle mesh "doubleSidedQueries" affects ray/sweep only. It does NOT make simulation contacts double-sided.
//   For truly double-sided simulation collision, duplicate faces with flipped winding when building the mesh.
struct TriangleMeshColliderDesc : public FilterDesc, public MaterialDesc
{
	// Vertex positions (required)
	const Vec3* vertices = nullptr;
	uint32_t vertexCount = 0;

	// Triangle indices (required). Provide either 32-bit or 16-bit indices.
	const uint32_t* indices32 = nullptr;
	const uint16_t* indices16 = nullptr;
	uint32_t indexCount = 0; // number of indices (must be multiple of 3)

	// Instance scale applied via PxMeshScale (does not affect cooking cache key)
	Vec3 scale = { 1.0f, 1.0f, 1.0f };

	// If your triangle winding does not match PhysX expectations, set this.
	// PhysX assumes the normal for [a,b,c] is (b-a) x (c-a). See PxMeshFlag::eFLIPNORMALS docs.
	bool flipNormals = false;

	// Query-only convenience (ray/sweep). Simulation contacts are unaffected.
	bool doubleSidedQueries = false;

	// Optional debug validation during cooking (extra cost)
	bool validate = false;
};

struct ConvexMeshColliderDesc : public FilterDesc, public MaterialDesc
{
	// Vertex positions (required). Provide a point cloud; PhysX will compute the hull by default.
	const Vec3* vertices = nullptr;
	uint32_t vertexCount = 0;

	// Instance scale applied via PxMeshScale (does not affect cooking cache key)
	Vec3 scale = { 1.0f, 1.0f, 1.0f };

	// Recommended when your vertices are not centered around the origin (improves numerical stability).
	bool shiftVertices = true;

	// Optional vertex limit hint for the hull computation (PhysX convex meshes are limited to 255 vertices/polygons).
	uint32_t vertexLimit = 255;

	// Optional debug validation during cooking (extra cost)
	bool validate = false;
};

// ============================================================
//  Character Controller (CCT)
// ============================================================
// PhysX-style kinematic character controller wrapper.
//
// Why this exists (practical reasons):
// - Dynamic capsule "rigidbody character" breaks down on steps/ledges/slopes.
// - CCT uses sweeps + stepOffset/slopeLimit/non-walkable modes to avoid jitter.

enum class CCTType : uint8_t
{
	Capsule,
	Box,
};

// Collision flags returned by Move().
enum class CCTCollisionFlags : uint8_t
{
	None  = 0,
	Sides = 1 << 0,
	Up    = 1 << 1,
	Down  = 1 << 2,
};

inline constexpr CCTCollisionFlags operator|(CCTCollisionFlags a, CCTCollisionFlags b)
{
	return static_cast<CCTCollisionFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr CCTCollisionFlags operator&(CCTCollisionFlags a, CCTCollisionFlags b)
{
	return static_cast<CCTCollisionFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline constexpr CCTCollisionFlags& operator|=(CCTCollisionFlags& a, CCTCollisionFlags b)
{
	a = (a | b);
	return a;
}

// Non-walkable mode determines what happens when the controller hits a surface
// that exceeds the slopeLimit.
enum class CCTNonWalkableMode : uint8_t
{
	PreventClimbing,
	PreventClimbingAndForceSliding,
};

enum class CCTCapsuleClimbingMode : uint8_t
{
	Easy,
	Constrained,
};

struct CharacterControllerDesc : public FilterDesc, public MaterialDesc
{
	CCTType type = CCTType::Capsule;

	// Capsule params (type == Capsule)
	float radius = 0.5f;
	float halfHeight = 0.5f; // cylinder half-height (like CapsuleColliderDesc)
	
	// Box params (type == Box) (optional; backend may not support)
	Vec3 halfExtents = { 0.5f, 0.9f, 0.5f };

	// Controller's up direction (usually +Y).
	Vec3 upDirection = Vec3::UnitY;

	// Position convention: footPosition is typically easier for gameplay.
	Vec3 footPosition = Vec3::Zero;

	// Movement tuning
	float stepOffset = 0.3f;      // meters
	float contactOffset = 0.1f;   // "skin" for sweeps (meters)

	// Slope limit is specified as an angle in radians.
	// Example: 45 deg -> 0.785398...
	float slopeLimitRadians = 0.785398163f;
	CCTNonWalkableMode nonWalkableMode = CCTNonWalkableMode::PreventClimbing;
	CCTCapsuleClimbingMode climbingMode = CCTCapsuleClimbingMode::Constrained;

	// Interaction with dynamic objects (push strength-ish).
	// Higher density => controller is heavier when resolving overlaps.
	float density = 10.0f;

	// Optional: if false, the controller will not be added to the scene query system.
	bool enableQueries = true;
};

struct CharacterControllerState
{
	CCTCollisionFlags collisionFlags = CCTCollisionFlags::None;
	bool onGround = false;
	Vec3 groundNormal = Vec3::UnitY;
	float groundDistance = 0.0f;
};

class ICharacterController
{
public:
	virtual ~ICharacterController() = default;

	virtual bool IsValid() const = 0;
	virtual void Destroy() = 0;

	virtual void SetUserData(void* ptr) = 0;
	virtual void* GetUserData() const = 0;

	virtual void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) = 0;

	// Center position of the controller (not foot).
	virtual void SetPosition(const Vec3& centerPos) = 0;
	virtual Vec3 GetPosition() const = 0;

	// Convenience for typical gameplay.
	virtual void SetFootPosition(const Vec3& footPos) = 0;
	virtual Vec3 GetFootPosition() const = 0;

	// Perform a swept move.
	// - displacement: desired displacement this frame (meters)
	// - dt: elapsed time
	// - layerMask/queryMask/hitTriggers: which shapes are considered as obstacles
	// Returns collision flags (sides/up/down).
	virtual CCTCollisionFlags Move(
		const Vec3& displacement,
		float dt,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		float minDistance = 0.0f) = 0;

	virtual CharacterControllerState GetState(
		uint32_t groundLayerMask = 0xFFFFFFFFu,
		uint32_t groundQueryMask = 0xFFFFFFFFu,
		float groundProbeDistance = 0.2f,
		bool hitTriggers = false) const = 0;

	// Runtime tuning
	virtual void SetStepOffset(float v) = 0;
	virtual float GetStepOffset() const = 0;
	virtual void SetSlopeLimit(float slopeLimitRadians) = 0;
	virtual float GetSlopeLimit() const = 0;
	virtual void Resize(float halfHeight) = 0; // capsule only; no-op for other types

	virtual void* GetNativeController() const = 0;
	virtual void* GetNativeActor() const = 0;
};

// ============================================================
//  Query results
// ============================================================
struct RaycastHit
{
	Vec3 position = Vec3::Zero;
	Vec3 normal = Vec3::UnitY;
	float distance = 0.0f;

	// Extra hit information (filled when supported by backend/geometry).
	// NOTE: PhysX reports "u/v" as barycentric coordinates on the hit triangle,
	// not texture UVs. We expose both the raw (u,v) and the full barycentric triple.
	uint32_t faceIndex = 0xFFFFFFFFu; // triangle/face index when available
	Vec2 baryUV = Vec2::Zero;         // (u,v) barycentric coordinates
	Vec3 barycentric = Vec3::Zero;    // (w,u,v) where w = 1-u-v

	// Pointer-stable (during runtime) handles, useful for hashing/caching without storing raw pointers.
	// These are derived from native pointers.
	uint64_t actorHandle = 0;
	uint64_t shapeHandle = 0;

	void* userData = nullptr;     // actor->userData
	void* nativeActor = nullptr;  // underlying physics actor pointer
	void* nativeShape = nullptr;  // underlying physics shape pointer (optional)
};

struct OverlapHit
{
	void* userData = nullptr;     // actor->userData
	void* nativeActor = nullptr;
	void* nativeShape = nullptr;
};

struct SweepHit
{
	Vec3 position = Vec3::Zero;
	Vec3 normal = Vec3::UnitY;
	float distance = 0.0f;

	void* userData = nullptr;
	void* nativeActor = nullptr;
	void* nativeShape = nullptr;
};

// SceneQueryFilter (ignore + SweepAll을 위한 필터)
struct SceneQueryFilter
{
	uint32_t layerMask = 0xFFFFFFFFu;
	uint32_t queryMask = 0xFFFFFFFFu;
	bool hitTriggers = false;

	// ignore (nullptr이면 무시 안 함)
	void* ignoreNativeActor = nullptr; // PxRigidActor*
	void* ignoreNativeShape = nullptr; // PxShape*
	void* ignoreUserData = nullptr;    // actor->userData 비교용
};

// ============================================================
//  Events
// ============================================================
enum class PhysicsEventType : uint8_t
{
	ContactBegin,
	ContactEnd,
	TriggerEnter,
	TriggerExit,
	JointBreak,
};

struct PhysicsEvent
{
	PhysicsEventType type = PhysicsEventType::ContactBegin;

	void* userDataA = nullptr;
	void* userDataB = nullptr;

	void* nativeActorA = nullptr;
	void* nativeActorB = nullptr;

	// Optional (when available)
	void* nativeShapeA = nullptr;
	void* nativeShapeB = nullptr;

	// Optional contact data (only valid for Contact events if enabled)
	Vec3 position = Vec3::Zero;
	Vec3 normal = Vec3::UnitY;

	// JointBreak에 사용
	void* nativeJoint = nullptr;     // PxJoint* (또는 externalReference)
	void* jointUserData = nullptr;   // PxJoint::userData (엔진에서 심어둔 값)
};

// ============================================================
//  Active Transforms (optional but strongly recommended)
// ============================================================
// After Step(), you can call DrainActiveTransforms() to obtain the list of
// rigid bodies that moved during the last simulation step, along with their
// final poses. This avoids polling every actor each frame.
struct ActiveTransform
{
	void* userData = nullptr;
	void* nativeActor = nullptr;

	Vec3 position = Vec3::Zero;
	Quat rotation = Quat::Identity;
};


// ============================================================
//  Contact Modify (optional)
// ============================================================
// When enabled on the world (Desc.enableContactModify), the backend can expose a callback
// that lets you edit or ignore contacts before the solver runs.
//
// Typical uses:
// - One-way platforms (ignore contacts based on normal)
// - Custom surface behavior (e.g., conveyor via target velocity)
// - Gameplay-driven "no-collide" rules beyond static filtering
//
// Important:
// - This runs on the physics simulation thread.
// - Keep work minimal; do NOT call back into the world/engine systems that might lock.

struct ContactModifyPoint
{
	Vec3 position = Vec3::Zero;
	Vec3 normal = Vec3::UnitY;
	float separation = 0.0f;
	Vec3 targetVelocity = Vec3::Zero;
	float maxImpulse = -1.0f; // <0 means "leave unchanged"
	bool ignore = false;
};

struct ContactModifyPair
{
	void* userDataA = nullptr;
	void* userDataB = nullptr;
	void* nativeActorA = nullptr;
	void* nativeActorB = nullptr;
	void* nativeShapeA = nullptr;
	void* nativeShapeB = nullptr;

	// Contacts can be modified in-place.
	std::vector<ContactModifyPoint> contacts;

	// If true, all contacts are ignored.
	bool ignorePair = false;
};

using ContactModifyCallback = void(*)(ContactModifyPair& pair, void* userContext);

// ============================================================
//  Actor Interfaces
// ============================================================
class IPhysicsActor
{
public:
	virtual ~IPhysicsActor() = default;

	virtual bool IsValid() const = 0;

	// Whether this actor currently resides in the simulation scene.
	// Useful for pooling/streaming (remove from world without destroying).
	virtual bool IsInWorld() const = 0;
	virtual void SetInWorld(bool inWorld) = 0;

	virtual void SetTransform(const Vec3& p, const Quat& q) = 0;
	virtual Vec3 GetPosition() const = 0;
	virtual Quat GetRotation() const = 0;

	virtual void SetUserData(void* ptr) = 0;
	virtual void* GetUserData() const = 0;

	virtual void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) = 0;
	virtual void SetTrigger(bool isTrigger) = 0;
	virtual void SetMaterial(float staticFriction, float dynamicFriction, float restitution) = 0;

	// Enable/disable collision (simulation) and query participation.
	// - CollisionEnabled toggles PxShapeFlag::eSIMULATION_SHAPE on non-trigger shapes.
	// - QueryEnabled toggles PxShapeFlag::eSCENE_QUERY_SHAPE.
	virtual void SetCollisionEnabled(bool enabled) = 0;
	virtual bool IsCollisionEnabled() const = 0;
	virtual void SetQueryEnabled(bool enabled) = 0;
	virtual bool IsQueryEnabled() const = 0;

	// Explicitly destroy the underlying native object (deferred-safe).
	// After calling Destroy(), IsValid() becomes false.
	virtual void Destroy() = 0;

	// ------------------------------
	// Compound shapes
	// ------------------------------
	// Add shape with a local pose relative to the actor.
	// Returns false if creation failed.
	virtual bool AddBoxShape(const BoxColliderDesc& box,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	virtual bool AddSphereShape(const SphereColliderDesc& sphere,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	virtual bool AddCapsuleShape(const CapsuleColliderDesc& capsule,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	// Requires mesh cooking support
	virtual bool AddTriangleMeshShape(const TriangleMeshColliderDesc& mesh,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	// Requires mesh cooking support
	virtual bool AddConvexMeshShape(const ConvexMeshColliderDesc& mesh,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	// Height field shape (terrain)
	virtual bool AddHeightFieldShape(const HeightFieldColliderDesc& heightField,
		const Vec3& localPos = Vec3::Zero,
		const Quat& localRot = Quat::Identity) = 0;

	virtual bool ClearShapes() = 0;
	virtual uint32_t GetShapeCount() const = 0;

	virtual void* GetNativeActor() const = 0;
};

class IRigidBody : public IPhysicsActor
{
public:
	virtual ~IRigidBody() = default;

	// For kinematic bodies, prefer SetKinematicTarget over SetTransform to get proper
	// contact generation and smoother interaction with dynamic bodies.
	virtual void SetKinematicTarget(const Vec3& p, const Quat& q) = 0;

	virtual bool IsKinematic() const = 0;
	virtual void SetKinematic(bool isKinematic) = 0;

	virtual void SetGravityEnabled(bool enabled) = 0;
	virtual bool IsGravityEnabled() const = 0;

	virtual void SetLinearVelocity(const Vec3& v) = 0;
	virtual Vec3 GetLinearVelocity() const = 0;

	virtual void SetAngularVelocity(const Vec3& v) = 0;
	virtual Vec3 GetAngularVelocity() const = 0;

	// Force/torque
	virtual void AddForce(const Vec3& f) = 0;
	virtual void AddImpulse(const Vec3& impulse) = 0;
	virtual void AddTorque(const Vec3& t) = 0;

	virtual void AddForceEx(const Vec3& f, ForceMode mode, bool autowake = true) = 0;
	virtual void AddTorqueEx(const Vec3& t, ForceMode mode, bool autowake = true) = 0;

	// Tuning / misc
	virtual void SetDamping(float linear, float angular) = 0;
	virtual void SetMaxVelocities(float maxLinear, float maxAngular) = 0;
	virtual void SetLockFlags(RigidBodyLockFlags flags) = 0;
	virtual void SetCCDEnabled(bool enabled, bool speculative = false) = 0;

	virtual float GetMass() const = 0;
	virtual void SetMass(float mass, bool updateInertia = true) = 0;

	// Recompute mass/inertia from attached shapes using the body's stored density/massOverride.
	// Useful when you add/remove shapes (compound bodies).
	virtual void RecomputeMass() = 0;

	//  density + massOverride 같이 갱신
	virtual void SetMassProperties(float density, float massOverride) = 0;

	//  런타임 튜닝용
	virtual void SetSolverIterations(uint32_t positionIts, uint32_t velocityIts) = 0;
	virtual void SetSleepThreshold(float sleepThreshold) = 0;
	virtual void SetStabilizationThreshold(float stabilizationThreshold) = 0;

	virtual void WakeUp() = 0;
	virtual void PutToSleep() = 0;
	virtual bool IsAwake() const = 0;
	virtual bool IsSleeping() const = 0;
};

// ============================================================
//  Joint Interfaces (minimal)
// ============================================================
struct JointFrame
{
	Vec3 position = Vec3::Zero;
	Quat rotation = Quat::Identity;
};

struct FixedJointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	// If > 0, joint can break.
	// Use a very large value to effectively disable breaking.
	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;
};


// Revolute (hinge) joint
struct RevoluteJointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	// If > 0, joint can break.
	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;

	// Limits (in radians). Enable with enableLimit.
	bool enableLimit = false;
	float lowerLimit = -3.14159265f;
	float upperLimit =  3.14159265f;
	float limitStiffness = 0.0f;
	float limitDamping = 0.0f;
	float limitRestitution = -1.0f;      // < 0: keep PhysX default
	float limitBounceThreshold = -1.0f;  // < 0: keep PhysX default

	// Drive (motor)
	bool enableDrive = false;
	float driveVelocity = 0.0f;     // rad/s
	float driveForceLimit = 0.0f;   // <= 0 => infinite
	bool driveFreeSpin = false;
	bool driveLimitsAreForces = true; // interpret driveForceLimit as force/torque (not impulse)
};

// Prismatic (slider) joint
// NOTE: In PhysX 5.5, PxPrismaticJoint has limits but no built-in motor drive (use D6 if you need a driven slider).
struct PrismaticJointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;

	// Limits (in meters along the joint X axis). Enable with enableLimit.
	bool enableLimit = false;
	float lowerLimit = -1.0f;
	float upperLimit =  1.0f;
	float limitStiffness = 0.0f;
	float limitDamping = 0.0f;
	float limitRestitution = -1.0f;      // < 0: keep PhysX default
	float limitBounceThreshold = -1.0f;  // < 0: keep PhysX default
};

// Distance (rope/spring) joint
struct DistanceJointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;

	// [minDistance, maxDistance]
	float minDistance = 0.0f;
	float maxDistance = 0.0f;

	// Tolerance beyond the allowed range at which the joint becomes active.
	float tolerance = 0.0f;

	bool enableMinDistance = false;
	bool enableMaxDistance = true;

	// Optional spring when distance exceeds [min-tolerance, max+tolerance]
	bool enableSpring = false;
	float stiffness = 0.0f;
	float damping = 0.0f;
};

// Spherical (ball-and-socket) joint
struct SphericalJointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;

	// Cone limit (in radians). Enable with enableLimit.
	bool enableLimit = false;
	float yLimitAngle = 1.5707963f; // 90 deg
	float zLimitAngle = 1.5707963f; // 90 deg
	float limitStiffness = 0.0f;
	float limitDamping = 0.0f;
	float limitRestitution = -1.0f;      // < 0: keep PhysX default
	float limitBounceThreshold = -1.0f;  // < 0: keep PhysX default
};

// D6 joint (6DOF) — the "swiss army" constraint
enum class D6Motion : uint8_t
{
	Locked,
	Limited,
	Free,
};

struct D6JointDriveDesc
{
	float stiffness = 0.0f;
	float damping = 0.0f;
	float forceLimit = 0.0f;       // <= 0 => infinite
	bool isAcceleration = false;   // true => acceleration drive
};

struct D6LinearLimitDesc
{
	// Valid range is [-PX_MAX_F32, PX_MAX_F32] in PhysX.
	float lower = -1.0f;
	float upper =  1.0f;
	float stiffness = 0.0f;
	float damping = 0.0f;
	float restitution = -1.0f;
	float bounceThreshold = -1.0f;
};

struct D6TwistLimitDesc
{
	float lower = -3.14159265f;
	float upper =  3.14159265f;
	float stiffness = 0.0f;
	float damping = 0.0f;
	float restitution = -1.0f;
	float bounceThreshold = -1.0f;
};

struct D6SwingLimitDesc
{
	float yAngle = 1.5707963f;
	float zAngle = 1.5707963f;
	float stiffness = 0.0f;
	float damping = 0.0f;
	float restitution = -1.0f;
	float bounceThreshold = -1.0f;
};

struct D6JointDesc
{
	JointFrame frameA{};
	JointFrame frameB{};

	float breakForce = 0.0f;
	float breakTorque = 0.0f;

	bool collideConnected = false;
	void* userData = nullptr;

	// Interprets drive force limits as force/torque (not impulse).
	bool driveLimitsAreForces = true;

	// Motions
	D6Motion motionX = D6Motion::Locked;
	D6Motion motionY = D6Motion::Locked;
	D6Motion motionZ = D6Motion::Locked;
	D6Motion motionTwist = D6Motion::Locked;
	D6Motion motionSwing1 = D6Motion::Locked;
	D6Motion motionSwing2 = D6Motion::Locked;

	// Limits (used when the corresponding motion is Limited)
	D6LinearLimitDesc linearLimitX{};
	D6LinearLimitDesc linearLimitY{};
	D6LinearLimitDesc linearLimitZ{};
	D6TwistLimitDesc twistLimit{};
	D6SwingLimitDesc swingLimit{};

	// Drives
	D6JointDriveDesc driveX{};
	D6JointDriveDesc driveY{};
	D6JointDriveDesc driveZ{};
	D6JointDriveDesc driveSwing{};
	D6JointDriveDesc driveTwist{};
	D6JointDriveDesc driveSlerp{};

	// Drive targets (in actor0 constraint frame / joint space)
	JointFrame drivePose{};
	Vec3 driveLinearVelocity = Vec3::Zero;
	Vec3 driveAngularVelocity = Vec3::Zero;
};

class IPhysicsJoint
{
public:
	virtual ~IPhysicsJoint() = default;

	virtual bool IsValid() const = 0;
	virtual void SetBreakForce(float force, float torque) = 0;
	virtual void SetCollideConnected(bool enable) = 0;
	virtual void* GetNativeJoint() const = 0;
	virtual void SetUserData(void* ptr) = 0;
	virtual void* GetUserData() const = 0;

};

// ============================================================
//  World Interface
// ============================================================
class IPhysicsWorld
{
public:
	virtual ~IPhysicsWorld() = default;

	// Flush pending adds/removes/releases without advancing simulation.
	// Useful for editor tools or when you manipulate actors outside of Step().
	virtual void Flush() = 0;

	virtual void Step(float fixedDt) = 0;

	virtual void SetGravity(const Vec3& g) = 0;
	virtual Vec3 GetGravity() const = 0;

	// ------------------------------
	// Creation (Dynamic)
	// ------------------------------
	virtual std::unique_ptr<IRigidBody> CreateDynamicBox(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const BoxColliderDesc& box) = 0;

	virtual std::unique_ptr<IRigidBody> CreateDynamicSphere(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const SphereColliderDesc& sphere) = 0;

	virtual std::unique_ptr<IRigidBody> CreateDynamicCapsule(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const CapsuleColliderDesc& capsule) = 0;

	// Create a dynamic actor with no shapes (for compound setup).
	virtual std::unique_ptr<IRigidBody> CreateDynamicEmpty(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb) = 0;

	// ------------------------------
	// Creation (Static)
	// ------------------------------
	// Legacy-friendly: world owns the created plane.
	virtual void CreateStaticPlane(
		float staticFriction = 0.5f,
		float dynamicFriction = 0.5f,
		float restitution = 0.0f,
		const FilterDesc& filter = {}) = 0;

	// RAII handle: you own the created plane.
	virtual std::unique_ptr<IPhysicsActor> CreateStaticPlaneActor(
		float staticFriction = 0.5f,
		float dynamicFriction = 0.5f,
		float restitution = 0.0f,
		const FilterDesc& filter = {}) = 0;

	virtual std::unique_ptr<IPhysicsActor> CreateStaticBox(
		const Vec3& pos,
		const Quat& rot,
		const BoxColliderDesc& box) = 0;

	virtual std::unique_ptr<IPhysicsActor> CreateStaticSphere(
		const Vec3& pos,
		const Quat& rot,
		const SphereColliderDesc& sphere) = 0;

	virtual std::unique_ptr<IPhysicsActor> CreateStaticCapsule(
		const Vec3& pos,
		const Quat& rot,
		const CapsuleColliderDesc& capsule) = 0;

	// Create a static actor with no shapes (for compound setup).
	virtual std::unique_ptr<IPhysicsActor> CreateStaticEmpty(
		const Vec3& pos,
		const Quat& rot,
		void* userData = nullptr) = 0;

	// ------------------------------
	// Cooked mesh shapes (requires PhysX Cooking)
	// ------------------------------
	// Static triangle mesh (good for level geometry)
	virtual std::unique_ptr<IPhysicsActor> CreateStaticTriangleMesh(
		const Vec3& pos,
		const Quat& rot,
		const TriangleMeshColliderDesc& mesh) = 0;

	// Static convex mesh (useful for complex static colliders)
	virtual std::unique_ptr<IPhysicsActor> CreateStaticConvexMesh(
		const Vec3& pos,
		const Quat& rot,
		const ConvexMeshColliderDesc& mesh) = 0;

	// Dynamic convex mesh (recommended for dynamic mesh colliders)
	virtual std::unique_ptr<IRigidBody> CreateDynamicConvexMesh(
		const Vec3& pos,
		const Quat& rot,
		const RigidBodyDesc& rb,
		const ConvexMeshColliderDesc& mesh) = 0;

	// Static height field (terrain collision, efficient for large terrains)
	virtual std::unique_ptr<IPhysicsActor> CreateStaticHeightField(
		const Vec3& pos,
		const Quat& rot,
		const HeightFieldColliderDesc& heightField) = 0;

	// ------------------------------
	// Character Controller (CCT)
	// ------------------------------
	// Default backend implementation may return nullptr.
	virtual bool SupportsCharacterControllers() const { return false; }
	virtual std::unique_ptr<ICharacterController> CreateCharacterController(
		const CharacterControllerDesc& desc)
	{
		(void)desc;
		return nullptr;
	}

	// Optional: query whether mesh cooking is available in this backend
	virtual bool SupportsMeshCooking() const { return false; }

	// Optional: drop the backend's mesh caches (safe; meshes are reference-counted).
	virtual void ClearMeshCaches() {}

	// ------------------------------
	// Joints
	// ------------------------------
	virtual std::unique_ptr<IPhysicsJoint> CreateFixedJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const FixedJointDesc& desc) = 0;

	virtual std::unique_ptr<IPhysicsJoint> CreateRevoluteJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const RevoluteJointDesc& desc) = 0;

	virtual std::unique_ptr<IPhysicsJoint> CreatePrismaticJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const PrismaticJointDesc& desc) = 0;

	virtual std::unique_ptr<IPhysicsJoint> CreateDistanceJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const DistanceJointDesc& desc) = 0;

	virtual std::unique_ptr<IPhysicsJoint> CreateSphericalJoint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const SphericalJointDesc& desc) = 0;

	virtual std::unique_ptr<IPhysicsJoint> CreateD6Joint(
		const IPhysicsActor& a,
		const IPhysicsActor& b,
		const D6JointDesc& desc) = 0;

	// ------------------------------
	// Queries
	// ------------------------------
	// Old-style raycast kept for compatibility: only filters by layerMask (plus trigger toggle).
	virtual bool Raycast(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		RaycastHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const = 0;

	// New: filters by layerMask AND queryMask
	virtual bool RaycastEx(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		RaycastHit& outHit,
		uint32_t layerMask,
		uint32_t queryMask,
		bool hitTriggers = false) const
	{
		// Default implementation: fall back to old raycast behavior.
		(void)queryMask;
		return Raycast(origin, dir, maxDist, outHit, layerMask, hitTriggers);
	}

	virtual uint32_t RaycastAll(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		std::vector<RaycastHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapBox(
		const Vec3& center,
		const Quat& rot,
		const Vec3& halfExtents,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapSphere(
		const Vec3& center,
		float radius,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapCapsule(
		const Vec3& center,
		const Quat& rot,
		float radius,
		float halfHeight,
		std::vector<OverlapHit>& outHits,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		uint32_t maxHits = 64,
		bool alignYAxis = true) const = 0;

	virtual bool SweepBox(
		const Vec3& origin,
		const Quat& rot,
		const Vec3& halfExtents,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const = 0;

	virtual bool SweepSphere(
		const Vec3& origin,
		float radius,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false) const = 0;

	virtual bool SweepCapsule(
		const Vec3& origin,
		const Quat& rot,
		float radius,
		float halfHeight,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		uint32_t layerMask = 0xFFFFFFFFu,
		uint32_t queryMask = 0xFFFFFFFFu,
		bool hitTriggers = false,
		bool alignYAxis = true) const = 0;

	// ------------------------------
	// Queries (Extended)
	// - ignore/self-filter 지원
	// - sweep multi-hit 지원
	// ------------------------------
	virtual bool RaycastQ(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		RaycastHit& outHit,
		const SceneQueryFilter& filter) const = 0;

	virtual uint32_t RaycastAllQ(
		const Vec3& origin,
		const Vec3& dir,
		float maxDist,
		std::vector<RaycastHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapBoxQ(
		const Vec3& center,
		const Quat& rot,
		const Vec3& halfExtents,
		std::vector<OverlapHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapSphereQ(
		const Vec3& center,
		float radius,
		std::vector<OverlapHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t OverlapCapsuleQ(
		const Vec3& center,
		const Quat& rot,
		float radius,
		float halfHeight,
		std::vector<OverlapHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64,
		bool alignYAxis = true) const = 0;

	virtual bool SweepBoxQ(
		const Vec3& origin,
		const Quat& rot,
		const Vec3& halfExtents,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		const SceneQueryFilter& filter) const = 0;

	virtual bool SweepSphereQ(
		const Vec3& origin,
		float radius,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		const SceneQueryFilter& filter) const = 0;

	virtual bool SweepCapsuleQ(
		const Vec3& origin,
		const Quat& rot,
		float radius,
		float halfHeight,
		const Vec3& dir,
		float maxDist,
		SweepHit& outHit,
		const SceneQueryFilter& filter,
		bool alignYAxis = true) const = 0;

	// Sweep All
	virtual uint32_t SweepBoxAllQ(
		const Vec3& origin,
		const Quat& rot,
		const Vec3& halfExtents,
		const Vec3& dir,
		float maxDist,
		std::vector<SweepHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t SweepSphereAllQ(
		const Vec3& origin,
		float radius,
		const Vec3& dir,
		float maxDist,
		std::vector<SweepHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64) const = 0;

	virtual uint32_t SweepCapsuleAllQ(
		const Vec3& origin,
		const Quat& rot,
		float radius,
		float halfHeight,
		const Vec3& dir,
		float maxDist,
		std::vector<SweepHit>& outHits,
		const SceneQueryFilter& filter,
		uint32_t maxHits = 64,
		bool alignYAxis = true) const = 0;

	// ------------------------------
	// Penetration (MTD) 헬퍼
	// ------------------------------
	virtual bool ComputePenetrationBoxVsShape(
		const Vec3& center,
		const Quat& rot,
		const Vec3& halfExtents,
		void* otherNativeActor,
		void* otherNativeShape,
		Vec3& outDirection,
		float& outDepth) const = 0;

	virtual bool ComputePenetrationSphereVsShape(
		const Vec3& center,
		float radius,
		void* otherNativeActor,
		void* otherNativeShape,
		Vec3& outDirection,
		float& outDepth) const = 0;

	virtual bool ComputePenetrationCapsuleVsShape(
		const Vec3& center,
		const Quat& rot,
		float radius,
		float halfHeight,
		bool alignYAxis,
		void* otherNativeActor,
		void* otherNativeShape,
		Vec3& outDirection,
		float& outDepth) const = 0;

	// ------------------------------
	// Events
	// ------------------------------
	// Optional: contact modify callback. Only called if the backend/world was created with
	// contact modify enabled (e.g., PhysXWorld::Desc.enableContactModify = true).
	virtual void SetContactModifyCallback(ContactModifyCallback cb, void* userContext = nullptr) = 0;

	virtual void DrainActiveTransforms(std::vector<ActiveTransform>& outTransforms) = 0;

	virtual void DrainEvents(std::vector<PhysicsEvent>& outEvents) = 0;
};
