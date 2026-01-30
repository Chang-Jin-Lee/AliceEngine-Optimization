// PhysXContext.cpp
#include "PhysXContext.h"

#include <PhysX/PxPhysicsAPI.h>

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
#  if PHYSXWRAP_COOKING_INCLUDE_STYLE == 1
#    include <cooking/PxCooking.h>
#  elif PHYSXWRAP_COOKING_INCLUDE_STYLE == 2
#    include <physx/cooking/PxCooking.h>
#  endif
#endif

#include <stdexcept>

using namespace physx;

struct PhysXContext::Impl
{
	~Impl()
	{
		if (dispatcher) dispatcher->release();

		if (physics)
		{
			if (extensionsInited) PxCloseExtensions();
			physics->release();
		}

		if (pvd) pvd->release();

		if (pvdTransport)
		{
			// PxPvd::release() does NOT release transport.
			pvdTransport->release();
			pvdTransport = nullptr;
		}

		if (foundation) foundation->release();
	}

	PxDefaultAllocator allocator;
	PxDefaultErrorCallback errorCb;

	PxFoundation* foundation = nullptr;
	PxPhysics* physics = nullptr;
	PxPvd* pvd = nullptr;
	PxPvdTransport* pvdTransport = nullptr;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	PxCookingParams cookingParams{ PxTolerancesScale{} };
	bool cookingEnabled = false;
#endif

	PxDefaultCpuDispatcher* dispatcher = nullptr;

	bool extensionsInited = false;
	PxTolerancesScale scale{};
};

static PxPvd* CreatePvd(PxFoundation& foundation, PxPvdTransport*& outTransport,
	const char* host, int port, uint32_t timeoutMs)
{
	// Transport 생성 실패는 예외 던지지 않고 nullptr 반환
	// (PVD는 선택적 기능이므로 연결 실패해도 계속 진행)
	outTransport = PxDefaultPvdSocketTransportCreate(host, port, timeoutMs);
	if (!outTransport)
		return nullptr;

	PxPvd* pvd = PxCreatePvd(foundation);
	if (!pvd)
	{
		// Transport는 생성되었지만 PVD 생성 실패
		outTransport->release();
		outTransport = nullptr;
		return nullptr;
	}

	// 연결 실패는 치명적 오류가 아님 (PVD 없이 계속 진행)
	if (!pvd->connect(*outTransport, PxPvdInstrumentationFlag::eALL))
	{
		// 연결 실패 시 리소스 정리
		pvd->release();
		pvd = nullptr;
		outTransport->release();
		outTransport = nullptr;
		return nullptr;
	}

	return pvd;
}

PhysXContext::PhysXContext()
	: PhysXContext(PhysXContextDesc{})
{
}

PhysXContext::PhysXContext(const PhysXContextDesc& desc)
	: impl(std::make_unique<Impl>())
{
	impl->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, impl->allocator, impl->errorCb);
	if (!impl->foundation)
		throw std::runtime_error("PxCreateFoundation failed");

	// PVD 연결 시도 (실패해도 계속 진행)
	// PVD는 디버깅 도구일 뿐이므로 연결 실패해도 PhysX는 정상 작동 가능
	if (desc.enablePvd)
	{
		impl->pvd = CreatePvd(*impl->foundation, impl->pvdTransport, desc.pvdHost, desc.pvdPort, desc.pvdTimeoutMs);
		// CreatePvd 실패 시 nullptr 반환 (예외 없음)
		// PhysX는 pvd가 nullptr이어도 정상 작동
	}

	impl->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *impl->foundation, impl->scale, true, impl->pvd);
	if (!impl->physics)
		throw std::runtime_error("PxCreatePhysics failed");

	if (!PxInitExtensions(*impl->physics, impl->pvd))
		throw std::runtime_error("PxInitExtensions failed");
	impl->extensionsInited = true;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	if (desc.enableCooking)
	{
		impl->cookingParams = PxCookingParams(impl->physics->getTolerancesScale());

		// Keep mesh cleaning enabled by default for robustness.
		if (desc.weldVertices)
			impl->cookingParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;
		impl->cookingParams.meshWeldTolerance = desc.meshWeldTolerance;

		// Saves memory if you don't need the remap table (common in engines).
		impl->cookingParams.suppressTriangleMeshRemapTable = desc.suppressTriangleMeshRemapTable;

		// Optional extras.
		impl->cookingParams.buildTriangleAdjacencies = desc.buildTriangleAdjacencies;
		impl->cookingParams.buildGPUData = desc.buildGPUData;

		impl->cookingEnabled = true;
	}
#endif

	impl->dispatcher = PxDefaultCpuDispatcherCreate(desc.dispatcherThreads);
	if (!impl->dispatcher)
		throw std::runtime_error("PxDefaultCpuDispatcherCreate failed");
}

PhysXContext::~PhysXContext() = default;

PxPhysics* PhysXContext::GetPhysics() const noexcept { return impl ? impl->physics : nullptr; }
PxFoundation* PhysXContext::GetFoundation() const noexcept { return impl ? impl->foundation : nullptr; }
PxCpuDispatcher* PhysXContext::GetDispatcher() const noexcept { return impl ? impl->dispatcher : nullptr; }
PxPvd* PhysXContext::GetPvd() const noexcept { return impl ? impl->pvd : nullptr; }

const PxCookingParams* PhysXContext::GetCookingParams() const noexcept
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return (impl && impl->cookingEnabled) ? &impl->cookingParams : nullptr;
#else
	return nullptr;
#endif
}

bool PhysXContext::IsCookingAvailable() const noexcept
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return impl && impl->cookingEnabled;
#else
	return false;
#endif
}
