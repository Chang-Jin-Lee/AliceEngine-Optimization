#pragma once

#include "../IPhysicsWorld.h"
#include <DirectXMath.h>

struct Phy_CCTComponent
{
    // --- 생성 파라미터 (Capsule 기준) ---
    float radius = 0.35f;
    float halfHeight = 0.9f; // "원통" halfHeight (hemisphere 제외)
    float stepOffset = 0.35f;
    float contactOffset = 0.05f;
    float slopeLimitRadians = 0.785398163f; // 45deg
    CCTNonWalkableMode nonWalkableMode = CCTNonWalkableMode::PreventClimbingAndForceSliding;
    CCTCapsuleClimbingMode climbingMode = CCTCapsuleClimbingMode::Constrained;
    float density = 10.0f;
    bool enableQueries = true;

    // --- 필터 ---
    uint32_t layerBits = 1u << 1;
    // collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거)
    uint32_t ignoreLayers = 0u; // 이그노어 레이어 비트마스크 (충돌/쿼리 모두 무시)
    bool hitTriggers = false;

    // --- 입력(게임플레이가 채움) ---
    // XZ는 수평 이동 속도(m/s), Y는 보통 안 씀(중력은 내부에서)
    DirectX::XMFLOAT3 desiredVelocity = { 0,0,0 };

    bool applyGravity = true;
    float gravity = -9.81f;
    float verticalVelocity = 0.0f;

    bool jumpRequested = false;
    float jumpSpeed = 20.0f;

    bool teleport = false; // true면 이번 틱에 Transform 위치로 강제 이동

    // --- 출력(시스템이 채움) ---
    bool onGround = false;
    DirectX::XMFLOAT3 groundNormal = { 0,1,0 };
    float groundDistance = 0.0f;
    uint8_t collisionFlags = 0; // CCTCollisionFlags bitmask

    // 내부 핸들
    // 직접 접근 금지: PhysicsSystem::ValidateAndGetController()를 통해 접근하세요
    ICharacterController* controllerHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};
