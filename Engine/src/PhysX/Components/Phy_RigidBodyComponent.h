#pragma once

#include "../IPhysicsWorld.h"
#include <DirectXMath.h>

// RigidBody 컴포넌트
// 엔티티에 붙으면 자동으로 물리 바디가 생성됨
struct Phy_RigidBodyComponent
{
    // 기본 설정
    float density = 1.0f;
    float massOverride = 0.0f; // > 0이면 mass로 사용

    // Kinematic 여부 (true면 물리 시뮬레이션에 영향받지 않음, Transform으로 제어)
    bool isKinematic = false;

    // 중력 적용 여부
    bool gravityEnabled = true;

    // 시작 시 깨어있는지
    bool startAwake = true;

    // CCD (Continuous Collision Detection)
    bool enableCCD = false;
    bool enableSpeculativeCCD = false;

    // 축 잠금
    RigidBodyLockFlags lockFlags = RigidBodyLockFlags::None;

    // 댐핑
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;

    // 속도 제한
    float maxLinearVelocity = 0.0f;
    float maxAngularVelocity = 0.0f;

    // Solver iterations
    uint32_t solverPositionIterations = 4;
    uint32_t solverVelocityIterations = 1;

    // Sleep thresholds (< 0면 기본값 사용)
    float sleepThreshold = -1.0f;
    float stabilizationThreshold = -1.0f;

    // Dynamic 바디의 Transform 강제 동기화 (teleport)
    bool teleport = false;
    bool resetVelocityOnTeleport = true;

    // 내부 사용: 물리 액터 핸들 (PhysicsSystem이 관리)
    //  직접 접근 금지: PhysicsSystem::ValidateAndGetRigidBody()를 통해 접근하세요
    IRigidBody* physicsActorHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};
