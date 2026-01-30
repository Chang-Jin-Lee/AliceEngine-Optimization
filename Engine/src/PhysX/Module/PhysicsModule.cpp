#include "PhysicsModule.h"

#include "../PhysXContext.h"
#include "../PhysXWorld.h"

#include <exception>
#include <utility>

struct PhysicsModule::Impl
{
    std::string lastError;
    std::shared_ptr<PhysXContext> ctx;
    ContextInitDesc lastDesc{};
};

static PhysXContextDesc ToPhysX(const PhysicsModule::ContextInitDesc& in)
{
    PhysXContextDesc out{};
    out.enablePvd = in.enablePvd;
    out.pvdHost = in.pvdHost;
    out.pvdPort = in.pvdPort;
    out.pvdTimeoutMs = in.pvdTimeoutMs;

    out.dispatcherThreads = in.dispatcherThreads;

    out.enableCooking = in.enableCooking;
    out.weldVertices = in.weldVertices;
    out.meshWeldTolerance = in.meshWeldTolerance;

    out.suppressTriangleMeshRemapTable = in.suppressTriangleMeshRemapTable;
    out.buildTriangleAdjacencies = in.buildTriangleAdjacencies;
    out.buildGPUData = in.buildGPUData;

    return out;
}

static PhysXWorld::Desc ToPhysX(const PhysicsModule::WorldDesc& in)
{
    PhysXWorld::Desc out{};
    out.gravity = in.gravity;

    out.enableSceneLocks = in.enableSceneLocks;
    out.enableActiveTransforms = in.enableActiveTransforms;

    out.enableContactEvents = in.enableContactEvents;
    out.enableContactPoints = in.enableContactPoints;
    out.enableContactModify = in.enableContactModify;
    out.enableCCD = in.enableCCD;

    return out;
}

PhysicsModule::PhysicsModule()
    : m(std::make_unique<Impl>())
{
}

PhysicsModule::~PhysicsModule() = default;

bool PhysicsModule::InitializeContext(const ContextInitDesc& desc)
{
    m->lastError.clear();
    m->lastDesc = desc;

    try
    {
        // 기존 ctx 교체
        m->ctx.reset();
        m->ctx = std::make_shared<PhysXContext>(ToPhysX(desc));
        return true;
    }
    catch (const std::exception& e)
    {
        m->lastError = e.what();
    }
    catch (...)
    {
        m->lastError = "Unknown exception during PhysicsModule::InitializeContext";
    }

    m->ctx.reset();
    return false;
}

void PhysicsModule::ShutdownContext()
{
    m->ctx.reset();
}

std::shared_ptr<IPhysicsWorld> PhysicsModule::CreateWorld(const WorldDesc& desc)
{
    m->lastError.clear();

    if (!m->ctx)
    {
        m->lastError = "CreateWorld failed: context is not initialized.";
        return nullptr;
    }

    try
    {
        auto ctxKeepAlive = m->ctx; // deleter가 캡쳐
        PhysXWorld::Desc wdesc = ToPhysX(desc);

        auto world = std::shared_ptr<PhysXWorld>(
            new PhysXWorld(*ctxKeepAlive, wdesc),
            [ctxKeepAlive](PhysXWorld* p)
            {
                delete p;
                // ctxKeepAlive는 마지막 월드가 죽을 때 같이 해제됨
            }
        );

        return std::static_pointer_cast<IPhysicsWorld>(world);
    }
    catch (const std::exception& e)
    {
        m->lastError = e.what();
    }
    catch (...)
    {
        m->lastError = "Unknown exception during PhysicsModule::CreateWorld";
    }

    return nullptr;
}

bool PhysicsModule::HasContext() const noexcept
{
    return (m->ctx != nullptr);
}

bool PhysicsModule::IsCookingAvailable() const noexcept
{
    return (m->ctx != nullptr) ? m->ctx->IsCookingAvailable() : false;
}

const std::string& PhysicsModule::GetLastError() const noexcept
{
    return m->lastError;
}
