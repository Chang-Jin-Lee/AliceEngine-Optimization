#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../IPhysicsWorld.h" // Vec3/Quat 포함

class PhysicsModule
{
public:
    // Engine lifetime: PhysXContext 설정(PhysXContextDesc 미러)
    struct ContextInitDesc
    {
        bool enablePvd = false;
        const char* pvdHost = "127.0.0.1";
        int pvdPort = 5425;
        uint32_t pvdTimeoutMs = 1000;  // 1초: PVD 서버 연결에 충분한 시간 제공

        uint32_t dispatcherThreads = 2;

        bool enableCooking = true;
        bool weldVertices = true;
        float meshWeldTolerance = 0.001f;

        bool suppressTriangleMeshRemapTable = true;
        bool buildTriangleAdjacencies = false;
        bool buildGPUData = false;
    };

    // Scene lifetime: PhysXWorld::Desc 미러
    struct WorldDesc
    {
        Vec3 gravity = { 0.0f, -9.81f, 0.0f };

        bool enableSceneLocks = true;
        bool enableActiveTransforms = true;

        bool enableContactEvents = true;
        bool enableContactPoints = false;
        bool enableContactModify = false;
        bool enableCCD = false;
    };

public:
    PhysicsModule();
    ~PhysicsModule();

    PhysicsModule(const PhysicsModule&) = delete;
    PhysicsModule& operator=(const PhysicsModule&) = delete;

    // Engine lifetime
    bool InitializeContext(const ContextInitDesc& desc = ContextInitDesc{});
    void ShutdownContext();

    // Scene lifetime
    // - 월드는 shared_ptr로 리턴
    // - shared_ptr deleter가 Context를 캡쳐해서 수명 안전하게 보장
    std::shared_ptr<IPhysicsWorld> CreateWorld(const WorldDesc& desc = WorldDesc{});

    bool HasContext() const noexcept;
    bool IsCookingAvailable() const noexcept;

    const std::string& GetLastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
