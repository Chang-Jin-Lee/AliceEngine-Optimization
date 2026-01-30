// PhysXContext.h
#pragma once

#include <cstdint>
#include <memory>

// ------------------------------------------------------------
// PhysX Cooking header detection (PhysX 5.2+ expected)
//
// NOTE:
// - PhysX 5.x ships cooking headers under either:
//     <cooking/...> (older include layout)
//   or
//     <physx/cooking/...> (namespaced include layout)
// - We only need cooking for mesh creation/caching.
// ------------------------------------------------------------
#ifndef PHYSXWRAP_ENABLE_COOKING
#define PHYSXWRAP_ENABLE_COOKING 1
#endif

#ifndef PHYSXWRAP_HAS_COOKING_HEADERS
#if PHYSXWRAP_ENABLE_COOKING && defined(__has_include)
#  if __has_include(<cooking/PxCooking.h>) && __has_include(<cooking/PxTriangleMeshDesc.h>) && __has_include(<cooking/PxConvexMeshDesc.h>)
#    define PHYSXWRAP_HAS_COOKING_HEADERS 1
#    define PHYSXWRAP_COOKING_INCLUDE_STYLE 1
#  elif __has_include(<physx/cooking/PxCooking.h>) && __has_include(<physx/cooking/PxTriangleMeshDesc.h>) && __has_include(<physx/cooking/PxConvexMeshDesc.h>)
#    define PHYSXWRAP_HAS_COOKING_HEADERS 1
#    define PHYSXWRAP_COOKING_INCLUDE_STYLE 2
#  else
#    define PHYSXWRAP_HAS_COOKING_HEADERS 0
#    define PHYSXWRAP_COOKING_INCLUDE_STYLE 0
#  endif
#elif PHYSXWRAP_ENABLE_COOKING
	// Compiler does not support __has_include
#  define PHYSXWRAP_HAS_COOKING_HEADERS 0
#  define PHYSXWRAP_COOKING_INCLUDE_STYLE 0
#else
#  define PHYSXWRAP_HAS_COOKING_HEADERS 0
#  define PHYSXWRAP_COOKING_INCLUDE_STYLE 0
#endif
#endif

namespace physx
{
	class PxPhysics;
	class PxFoundation;
	class PxCpuDispatcher;
	class PxPvd;
	struct PxCookingParams;
}

struct PhysXContextDesc
{
	// PVD = PhysX Visual Debugger (useful, but optional)
	bool enablePvd = false;
	const char* pvdHost = "127.0.0.1";
	int pvdPort = 5425;
	uint32_t pvdTimeoutMs = 1000;  // 1초: PVD 서버 연결에 충분한 시간 제공

	// Worker threads for PhysX CPU dispatcher
	uint32_t dispatcherThreads = 2;

	// ------------------------------------------------------------
	// Cooking (PhysX 5.2+): PxCooking class is deprecated/removed.
	// This wrapper stores PxCookingParams and uses the immediate
	// cooking entry points declared in cooking/PxCooking.h:
	//   PxCreateTriangleMesh, PxCreateConvexMesh, PxValidate*, ...
	// ------------------------------------------------------------
	bool enableCooking = true;

	// Keep mesh cleaning enabled for robustness by default.
	bool weldVertices = true;
	float meshWeldTolerance = 0.001f;

	// Saves memory if you don't need the remap table (common in engines).
	bool suppressTriangleMeshRemapTable = true;

	// Optional extras (usually off unless you really need them)
	bool buildTriangleAdjacencies = false;
	bool buildGPUData = false;
};

class PhysXContext
{
public:
	PhysXContext(); // default settings
	explicit PhysXContext(const PhysXContextDesc& desc);
	~PhysXContext();

	PhysXContext(const PhysXContext&) = delete;
	PhysXContext& operator=(const PhysXContext&) = delete;

	physx::PxPhysics* GetPhysics() const noexcept;
	physx::PxFoundation* GetFoundation() const noexcept;
	physx::PxCpuDispatcher* GetDispatcher() const noexcept;
	physx::PxPvd* GetPvd() const noexcept;

	// Cooking parameters used by immediate cooking functions.
	// Returns nullptr if cooking is disabled or unavailable.
	const physx::PxCookingParams* GetCookingParams() const noexcept;

	bool IsCookingAvailable() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
